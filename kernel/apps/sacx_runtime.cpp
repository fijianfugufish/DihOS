#include "apps/sacx_runtime.h"
#include "apps/sacx_api.h"
#include "apps/sacx_format.h"

#include <stddef.h>

extern "C"
{
#include "kwrappers/colors.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kimg.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/ktext.h"
#include "kwrappers/ktextbox.h"
#include "kwrappers/kwindow.h"
#include "kwrappers/string.h"
#include "memory/pmem.h"
#include "system/dihos_time.h"
#include "terminal/terminal_api.h"
}

#define SACX_MAX_TASKS 16u
#define SACX_MAX_TASK_FILES 24u
#define SACX_MAX_TASK_DIRS 24u
#define SACX_MAX_TASK_WINDOWS 16u
#define SACX_MAX_TASK_BUTTONS 64u
#define SACX_MAX_TASK_TEXTBOXES 32u
#define SACX_MAX_TASK_GFX_OBJECTS 512u
#define SACX_MAX_TASK_IMAGES 32u
#define SACX_MAX_SEGMENTS 128u
#define SACX_MAX_RELOCS 8192u
#define SACX_MAX_IMPORTS 256u
#define SACX_MAX_FILE_BYTES (32u * 1024u * 1024u)
#define SACX_APP_ARENA_BYTES (8u * 1024u * 1024u)
#define SACX_APP_ARENA_PAGES (SACX_APP_ARENA_BYTES / 4096u)
#define SACX_SCHED_DEFAULT_QUANTUM_TICKS 1u

typedef struct sacx_task sacx_task;

typedef struct sacx_file_slot
{
    KFile file;
    uint8_t used;
} sacx_file_slot;

typedef struct sacx_dir_slot
{
    KDir dir;
    uint8_t used;
} sacx_dir_slot;

typedef struct sacx_window_slot
{
    kwindow_handle handle;
    uint8_t used;
} sacx_window_slot;

typedef struct sacx_gfx_slot
{
    kgfx_obj_handle handle;
    uint8_t used;
} sacx_gfx_slot;

typedef struct sacx_button_slot
{
    kbutton_handle handle;
    uint8_t used;
    sacx_button_on_click_fn callback;
    void *callback_user;
    sacx_task *owner;
} sacx_button_slot;

typedef struct sacx_textbox_slot
{
    ktextbox_handle handle;
    uint8_t used;
    sacx_textbox_on_submit_fn callback;
    void *callback_user;
    sacx_task *owner;
} sacx_textbox_slot;

typedef struct sacx_image_slot
{
    kimg image;
    uint8_t used;
} sacx_image_slot;

typedef struct sacx_task
{
    uint32_t task_id;
    uint32_t state;
    uint32_t state_age;
    uint64_t wake_tick;

    int32_t exit_status;
    char exit_message[128];

    uint8_t *arena;
    uint32_t arena_size;
    uint8_t *image_base;
    uint32_t image_size;

    sacx_entry_fn entry;
    sacx_update_fn update_fn;
    sacx_api api;

    uint8_t started;
    uint8_t exit_requested;
    uint8_t sleep_requested;
    int32_t pending_exit_status;
    uint64_t pending_wake_tick;
    uint32_t sched_quantum_ticks;
    uint32_t sched_budget_left;
    uint32_t preempt_guard_depth;
    uint32_t preemptions;
    char pending_exit_message[128];

    sacx_runtime_io io;
    char friendly_path[256];

    sacx_file_slot files[SACX_MAX_TASK_FILES];
    sacx_dir_slot dirs[SACX_MAX_TASK_DIRS];
    sacx_window_slot windows[SACX_MAX_TASK_WINDOWS];
    sacx_button_slot buttons[SACX_MAX_TASK_BUTTONS];
    sacx_textbox_slot textboxes[SACX_MAX_TASK_TEXTBOXES];
    sacx_gfx_slot gfx_objects[SACX_MAX_TASK_GFX_OBJECTS];
    sacx_image_slot images[SACX_MAX_TASK_IMAGES];
} sacx_task;

static sacx_task G_tasks[SACX_MAX_TASKS];
static sacx_task *G_current_task = 0;
static uint32_t G_next_task_id = 1u;
static uint32_t G_rr_cursor = 0u;
static const kfont *G_runtime_font = 0;

static uint32_t sacx_cache_line_bytes_from_ctr_field(uint32_t field)
{
    uint32_t bytes = 4u << (field & 0xFu);
    if (bytes < 16u || bytes > 4096u)
        bytes = 64u;
    return bytes;
}

static void sacx_sync_executable_range(const void *base, uint32_t size)
{
#if defined(__aarch64__)
    uint64_t ctr = 0u;
    uint32_t ic_line = 64u;
    uint32_t dc_line = 64u;
    uintptr_t start = 0u;
    uintptr_t end = 0u;
    uintptr_t p = 0u;

    if (!base || size == 0u)
        return;

    start = (uintptr_t)base;
    end = start + (uintptr_t)size;
    if (end < start)
        return;

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr));
    ic_line = sacx_cache_line_bytes_from_ctr_field((uint32_t)(ctr & 0xFu));
    dc_line = sacx_cache_line_bytes_from_ctr_field((uint32_t)((ctr >> 16) & 0xFu));

    p = start & ~(uintptr_t)(dc_line - 1u);
    while (p < end)
    {
        __asm__ __volatile__("dc cvau, %0" ::"r"(p) : "memory");
        p += dc_line;
    }
    __asm__ __volatile__("dsb ish" ::: "memory");

    p = start & ~(uintptr_t)(ic_line - 1u);
    while (p < end)
    {
        __asm__ __volatile__("ic ivau, %0" ::"r"(p) : "memory");
        p += ic_line;
    }
    __asm__ __volatile__("dsb ish; isb" ::: "memory");
#else
    (void)base;
    (void)size;
#endif
}

static int sacx_ptr_in_image(const sacx_task *task, const void *ptr)
{
    uintptr_t lo = 0u;
    uintptr_t hi = 0u;
    uintptr_t p = 0u;

    if (!task || !task->image_base || task->image_size == 0u || !ptr)
        return 0;

    lo = (uintptr_t)task->image_base;
    hi = lo + (uintptr_t)task->image_size;
    p = (uintptr_t)ptr;
    if (hi < lo)
        return 0;
    if (p < lo || p >= hi)
        return 0;
    if ((p & 0x3u) != 0u)
        return 0;
    return 1;
}

static void sacx_copy_trunc(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0u;

    if (!dst || cap == 0u)
        return;
    if (!src)
        src = "";

    while (src[i] && i + 1u < cap)
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void sacx_i32_to_text(int32_t value, char *out, uint32_t cap)
{
    char tmp[16];
    uint32_t len = 0u;
    uint32_t i = 0u;
    uint32_t magnitude = 0u;

    if (!out || cap == 0u)
        return;

    if (value < 0)
    {
        out[0] = '-';
        if (cap <= 1u)
            return;
        out++;
        cap--;
        magnitude = (uint32_t)(-value);
    }
    else
    {
        magnitude = (uint32_t)value;
    }

    if (magnitude == 0u)
    {
        if (cap > 1u)
        {
            out[0] = '0';
            out[1] = 0;
        }
        else
        {
            out[0] = 0;
        }
        return;
    }

    while (magnitude && len < sizeof(tmp))
    {
        tmp[len++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    }

    while (len > 0u && i + 1u < cap)
        out[i++] = tmp[--len];
    out[i] = 0;
}

static uint32_t sacx_crc32_zero_range(const uint8_t *data, uint32_t size, uint32_t zero_off, uint32_t zero_len)
{
    uint32_t crc = 0xFFFFFFFFu;

    if (!data)
        return 0u;

    for (uint32_t i = 0u; i < size; ++i)
    {
        uint8_t byte = data[i];
        if (i >= zero_off && i < zero_off + zero_len)
            byte = 0u;

        crc ^= (uint32_t)byte;
        for (uint32_t k = 0u; k < 8u; ++k)
        {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }

    return ~crc;
}

static int sacx_range_ok(uint32_t off, uint32_t len, uint32_t total)
{
    uint64_t end = (uint64_t)off + (uint64_t)len;
    return end <= (uint64_t)total;
}

static int sacx_table_ok(uint32_t off, uint32_t count, uint32_t elem_size, uint32_t total)
{
    uint64_t len = (uint64_t)count * (uint64_t)elem_size;
    return sacx_range_ok(off, (uint32_t)len, total) && len <= 0xFFFFFFFFull;
}

static const char *G_known_imports[] = {
    "app_set_update",
    "app_exit",
    "app_yield",
    "app_sleep_ticks",
    "app_set_console_visible",
    "time_ticks",
    "time_seconds",
    "log",
    "file_open",
    "file_read",
    "file_write",
    "file_seek",
    "file_size",
    "file_close",
    "file_unlink",
    "file_rename",
    "file_mkdir",
    "window_create",
    "window_destroy",
    "window_set_visible",
    "window_set_title",
    "gfx_fill_rgb",
    "gfx_rect_rgb",
    "gfx_flush",
    "input_key_down",
    "input_key_pressed",
    "input_key_released",
    "dir_open",
    "dir_next",
    "dir_close",
    "window_create_ex",
    "window_visible",
    "window_raise",
    "window_set_work_area_bottom_inset",
    "window_root",
    "window_point_can_receive_input",
    "gfx_obj_add_rect",
    "gfx_obj_add_circle",
    "gfx_obj_add_text",
    "gfx_obj_add_image_from_img",
    "gfx_obj_destroy",
    "gfx_obj_set_visible",
    "gfx_obj_visible",
    "gfx_obj_set_z",
    "gfx_obj_z",
    "gfx_obj_set_parent",
    "gfx_obj_clear_parent",
    "gfx_obj_set_clip_to_parent",
    "gfx_obj_set_fill_rgb",
    "gfx_obj_set_alpha",
    "gfx_obj_set_outline_rgb",
    "gfx_obj_set_outline_width",
    "gfx_obj_set_outline_alpha",
    "gfx_obj_set_rect",
    "gfx_obj_set_circle",
    "gfx_text_set",
    "gfx_text_set_align",
    "gfx_text_set_spacing",
    "gfx_text_set_scale",
    "gfx_text_set_pos",
    "gfx_image_set_size",
    "gfx_image_set_scale_pct",
    "gfx_image_set_sample_mode",
    "button_add_rect",
    "button_destroy",
    "button_root",
    "button_set_callback",
    "button_set_style",
    "button_set_enabled",
    "button_enabled",
    "button_hovered",
    "button_pressed",
    "textbox_add_rect",
    "textbox_destroy",
    "textbox_root",
    "textbox_set_callback",
    "textbox_set_enabled",
    "textbox_enabled",
    "textbox_set_focus",
    "textbox_clear_focus",
    "textbox_focused",
    "textbox_set_bounds",
    "textbox_set_text",
    "textbox_clear",
    "textbox_text_copy",
    "input_mouse_dx",
    "input_mouse_dy",
    "input_mouse_wheel",
    "input_mouse_buttons",
    "input_mouse_consume",
    "mouse_set_cursor",
    "mouse_current_cursor",
    "mouse_set_sensitivity_pct",
    "mouse_sensitivity_pct",
    "mouse_x",
    "mouse_y",
    "mouse_dx",
    "mouse_dy",
    "mouse_wheel",
    "mouse_buttons",
    "mouse_visible",
    "mouse_get_state",
    "text_draw",
    "text_draw_align",
    "text_draw_outline_align",
    "text_measure_line_px",
    "text_line_height",
    "text_scale_mul_px",
    "img_load",
    "img_load_bmp",
    "img_load_png",
    "img_load_jpg",
    "img_draw",
    "img_destroy",
    "img_size",
    "sched_preempt_guard_enter",
    "sched_preempt_guard_leave",
    "sched_quantum_ticks",
    "sched_preemptions",
};

static int sacx_import_known(const char *name)
{
    for (uint32_t i = 0u; i < (uint32_t)(sizeof(G_known_imports) / sizeof(G_known_imports[0])); ++i)
    {
        if (strcmp(name, G_known_imports[i]) == 0)
            return 1;
    }
    return 0;
}

static const char *sacx_string_at(const char *base, uint32_t size, uint32_t off)
{
    uint32_t i = 0u;

    if (!base || off >= size)
        return 0;

    i = off;
    while (i < size)
    {
        if (base[i] == 0)
            return base + off;
        ++i;
    }

    return 0;
}

static void sacx_task_log(sacx_task *task, const char *text)
{
    if (!task || !text)
        return;

    if (task->io.print)
    {
        task->io.print(text, task->io.user);
        return;
    }

    terminal_print(text);
}

static kcolor sacx_to_kcolor(sacx_color c)
{
    kcolor out = {c.r, c.g, c.b};
    return out;
}

static void sacx_copy_kdirent(const kdirent *in, sacx_dirent *out)
{
    if (!in || !out)
        return;
    memset(out, 0, sizeof(*out));
    sacx_copy_trunc(out->name, sizeof(out->name), in->name);
    out->is_dir = in->is_dir ? 1u : 0u;
    out->size = in->size;
}

static ktext_align sacx_to_text_align(uint32_t align)
{
    if (align == SACX_TEXT_ALIGN_CENTER)
        return KTEXT_ALIGN_CENTER;
    if (align == SACX_TEXT_ALIGN_RIGHT)
        return KTEXT_ALIGN_RIGHT;
    return KTEXT_ALIGN_LEFT;
}

static kmouse_cursor sacx_to_mouse_cursor(uint32_t cursor)
{
    if (cursor >= SACX_MOUSE_CURSOR_COUNT)
        return KMOUSE_CURSOR_ARROW;
    return (kmouse_cursor)cursor;
}

static kgfx_image_sample_mode sacx_to_sample_mode(uint32_t mode)
{
    if (mode == SACX_GFX_IMAGE_SAMPLE_BILINEAR)
        return KGFX_IMAGE_SAMPLE_BILINEAR;
    return KGFX_IMAGE_SAMPLE_NEAREST;
}

static void sacx_button_style_to_native(const sacx_button_style *in, kbutton_style *out)
{
    if (!out)
        return;

    *out = kbutton_style_default();
    if (!in)
        return;

    out->fill = sacx_to_kcolor(in->fill);
    out->hover_fill = sacx_to_kcolor(in->hover_fill);
    out->pressed_fill = sacx_to_kcolor(in->pressed_fill);
    out->outline = sacx_to_kcolor(in->outline);
    out->alpha = in->alpha;
    out->outline_alpha = in->outline_alpha;
    out->outline_width = in->outline_width;
}

static void sacx_textbox_style_to_native(const sacx_textbox_style *in, ktextbox_style *out)
{
    if (!out)
        return;

    *out = ktextbox_style_default();
    if (!in)
        return;

    out->fill = sacx_to_kcolor(in->fill);
    out->hover_fill = sacx_to_kcolor(in->hover_fill);
    out->focus_fill = sacx_to_kcolor(in->focus_fill);
    out->outline = sacx_to_kcolor(in->outline);
    out->focus_outline = sacx_to_kcolor(in->focus_outline);
    out->text_color = sacx_to_kcolor(in->text_color);
    out->alpha = in->alpha;
    out->outline_alpha = in->outline_alpha;
    out->outline_width = in->outline_width;
    out->padding_x = in->padding_x;
    out->padding_y = in->padding_y;
    out->text_scale = in->text_scale ? in->text_scale : 1u;
}

static void sacx_window_style_to_native(const sacx_window_style *in, kwindow_style *out)
{
    if (!out)
        return;

    *out = kwindow_style_default();
    if (!in)
        return;

    out->body_fill = sacx_to_kcolor(in->body_fill);
    out->body_outline = sacx_to_kcolor(in->body_outline);
    out->titlebar_fill = sacx_to_kcolor(in->titlebar_fill);
    out->title_color = sacx_to_kcolor(in->title_color);
    out->close_text_color = sacx_to_kcolor(in->close_text_color);
    out->fullscreen_text_color = sacx_to_kcolor(in->fullscreen_text_color);
    sacx_button_style_to_native(&in->close_button_style, &out->close_button_style);
    sacx_button_style_to_native(&in->fullscreen_button_style, &out->fullscreen_button_style);
    out->body_outline_width = in->body_outline_width;
    out->titlebar_height = in->titlebar_height;
    out->close_button_width = in->close_button_width;
    out->close_button_height = in->close_button_height;
    out->fullscreen_button_width = in->fullscreen_button_width;
    out->fullscreen_button_height = in->fullscreen_button_height;
    out->title_scale = in->title_scale;
    out->close_glyph_scale = in->close_glyph_scale;
    out->fullscreen_glyph_scale = in->fullscreen_glyph_scale;
}

static sacx_dir_slot *sacx_dir_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_DIRS)
        return 0;
    if (!task->dirs[handle - 1u].used)
        return 0;
    return &task->dirs[handle - 1u];
}

static sacx_window_slot *sacx_window_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_WINDOWS)
        return 0;
    if (!task->windows[handle - 1u].used)
        return 0;
    return &task->windows[handle - 1u];
}

static sacx_gfx_slot *sacx_gfx_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_GFX_OBJECTS)
        return 0;
    if (!task->gfx_objects[handle - 1u].used)
        return 0;
    return &task->gfx_objects[handle - 1u];
}

static int sacx_gfx_register_existing(sacx_task *task, kgfx_obj_handle obj, uint32_t *out_handle)
{
    if (!task || !out_handle || obj.idx < 0 || !kgfx_obj_ref(obj))
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_GFX_OBJECTS; ++i)
    {
        if (!task->gfx_objects[i].used)
            continue;
        if (task->gfx_objects[i].handle.idx == obj.idx)
        {
            *out_handle = i + 1u;
            return 0;
        }
    }

    for (uint32_t i = 0u; i < SACX_MAX_TASK_GFX_OBJECTS; ++i)
    {
        if (task->gfx_objects[i].used)
            continue;
        task->gfx_objects[i].used = 1u;
        task->gfx_objects[i].handle = obj;
        *out_handle = i + 1u;
        return 0;
    }

    return -1;
}

static sacx_button_slot *sacx_button_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_BUTTONS)
        return 0;
    if (!task->buttons[handle - 1u].used)
        return 0;
    return &task->buttons[handle - 1u];
}

static sacx_textbox_slot *sacx_textbox_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_TEXTBOXES)
        return 0;
    if (!task->textboxes[handle - 1u].used)
        return 0;
    return &task->textboxes[handle - 1u];
}

static sacx_image_slot *sacx_image_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_IMAGES)
        return 0;
    if (!task->images[handle - 1u].used)
        return 0;
    return &task->images[handle - 1u];
}

static void sacx_button_click_trampoline(kbutton_handle button, void *user)
{
    sacx_button_slot *slot = (sacx_button_slot *)user;
    sacx_task *saved = G_current_task;
    uint32_t app_handle = 0u;

    (void)button;

    if (!slot || !slot->used || !slot->owner || !slot->callback)
        return;

    app_handle = (uint32_t)((slot - slot->owner->buttons) + 1u);
    G_current_task = slot->owner;
    slot->callback(app_handle, slot->callback_user);
    G_current_task = saved;
}

static void sacx_textbox_submit_trampoline(ktextbox_handle textbox, const char *text, void *user)
{
    sacx_textbox_slot *slot = (sacx_textbox_slot *)user;
    sacx_task *saved = G_current_task;
    uint32_t app_handle = 0u;

    (void)textbox;

    if (!slot || !slot->used || !slot->owner || !slot->callback)
        return;

    app_handle = (uint32_t)((slot - slot->owner->textboxes) + 1u);
    G_current_task = slot->owner;
    slot->callback(app_handle, text ? text : "", slot->callback_user);
    G_current_task = saved;
}

static void sacx_task_close_files(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_FILES; ++i)
    {
        if (!task->files[i].used)
            continue;
        kfile_close(&task->files[i].file);
        task->files[i].used = 0u;
    }
}

static void sacx_task_close_dirs(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_DIRS; ++i)
    {
        if (!task->dirs[i].used)
            continue;
        kdir_close(&task->dirs[i].dir);
        task->dirs[i].used = 0u;
    }
}

static void sacx_task_destroy_textboxes(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_TEXTBOXES; ++i)
    {
        if (!task->textboxes[i].used)
            continue;
        (void)ktextbox_destroy(task->textboxes[i].handle);
        task->textboxes[i].used = 0u;
        task->textboxes[i].handle.idx = -1;
        task->textboxes[i].callback = 0;
        task->textboxes[i].callback_user = 0;
        task->textboxes[i].owner = 0;
    }
}

static void sacx_task_destroy_buttons(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_BUTTONS; ++i)
    {
        if (!task->buttons[i].used)
            continue;
        (void)kbutton_destroy(task->buttons[i].handle);
        task->buttons[i].used = 0u;
        task->buttons[i].handle.idx = -1;
        task->buttons[i].callback = 0;
        task->buttons[i].callback_user = 0;
        task->buttons[i].owner = 0;
    }
}

static void sacx_task_destroy_windows(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_WINDOWS; ++i)
    {
        if (!task->windows[i].used)
            continue;
        (void)kwindow_destroy(task->windows[i].handle);
        task->windows[i].used = 0u;
        task->windows[i].handle.idx = -1;
    }
}

static void sacx_task_destroy_gfx_objects(sacx_task *task)
{
    if (!task)
        return;

    for (int32_t i = (int32_t)SACX_MAX_TASK_GFX_OBJECTS - 1; i >= 0; --i)
    {
        if (!task->gfx_objects[i].used)
            continue;
        (void)kgfx_obj_destroy(task->gfx_objects[i].handle);
        task->gfx_objects[i].used = 0u;
        task->gfx_objects[i].handle.idx = -1;
    }
}

static void sacx_task_destroy_images(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_IMAGES; ++i)
    {
        uint64_t bytes = 0u;
        uint64_t pages = 0u;

        if (!task->images[i].used)
            continue;

        if (task->images[i].image.px && task->images[i].image.w && task->images[i].image.h)
        {
            bytes = (uint64_t)task->images[i].image.w * (uint64_t)task->images[i].image.h * 4u;
            pages = (bytes + 4095u) / 4096u;
            if (pages)
                pmem_free_pages(task->images[i].image.px, pages);
        }

        task->images[i].used = 0u;
        task->images[i].image.w = 0u;
        task->images[i].image.h = 0u;
        task->images[i].image.px = 0;
    }
}

static void sacx_task_finish(sacx_task *task, int32_t status, const char *message, uint32_t new_state)
{
    char status_text[16];

    if (!task)
        return;

    sacx_task_close_files(task);
    sacx_task_close_dirs(task);
    sacx_task_destroy_textboxes(task);
    sacx_task_destroy_buttons(task);
    sacx_task_destroy_windows(task);
    sacx_task_destroy_gfx_objects(task);
    sacx_task_destroy_images(task);

    if (task->arena)
    {
        pmem_free_executable_pages(task->arena, task->arena_size / 4096u);
        task->arena = 0;
    }

    task->image_base = 0;
    task->image_size = 0u;
    task->entry = 0;
    task->update_fn = 0;
    task->started = 0u;
    task->exit_requested = 0u;
    task->sleep_requested = 0u;
    task->pending_wake_tick = 0u;
    task->sched_budget_left = 0u;
    task->preempt_guard_depth = 0u;

    task->exit_status = status;
    sacx_copy_trunc(task->exit_message, sizeof(task->exit_message), message ? message : "");
    task->state = new_state;
    task->state_age = 0u;
    task->wake_tick = dihos_time_ticks();

    if (new_state == SACX_TASK_EXITED)
    {
        char line[200];
        ksb b;
        ksb_init(&b, line, sizeof(line));
        ksb_puts(&b, "[sacx] task exited: ");
        sacx_i32_to_text(status, status_text, sizeof(status_text));
        ksb_puts(&b, status_text);
        if (task->exit_message[0])
        {
            ksb_puts(&b, " (");
            ksb_puts(&b, task->exit_message);
            ksb_putc(&b, ')');
        }
        sacx_task_log(task, line);
    }
    else
    {
        char line[200];
        ksb b;
        ksb_init(&b, line, sizeof(line));
        ksb_puts(&b, "[sacx] task faulted: ");
        if (task->exit_message[0])
            ksb_puts(&b, task->exit_message);
        else
            ksb_puts(&b, "unknown fault");
        sacx_task_log(task, line);
    }
}

static void sacx_task_reset(sacx_task *task)
{
    if (!task)
        return;
    memset(task, 0, sizeof(*task));
    task->state = SACX_TASK_UNUSED;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_WINDOWS; ++i)
        task->windows[i].handle.idx = -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_BUTTONS; ++i)
        task->buttons[i].handle.idx = -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_TEXTBOXES; ++i)
        task->textboxes[i].handle.idx = -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_GFX_OBJECTS; ++i)
        task->gfx_objects[i].handle.idx = -1;
    task->sched_quantum_ticks = SACX_SCHED_DEFAULT_QUANTUM_TICKS;
    task->sched_budget_left = SACX_SCHED_DEFAULT_QUANTUM_TICKS;
}

static sacx_task *sacx_find_task_by_id(uint32_t task_id)
{
    if (!task_id)
        return 0;

    for (uint32_t i = 0u; i < SACX_MAX_TASKS; ++i)
    {
        if (G_tasks[i].task_id == task_id && G_tasks[i].state != SACX_TASK_UNUSED)
            return &G_tasks[i];
    }

    return 0;
}

static sacx_task *sacx_alloc_task_slot(void)
{
    for (uint32_t i = 0u; i < SACX_MAX_TASKS; ++i)
    {
        if (G_tasks[i].state == SACX_TASK_UNUSED)
            return &G_tasks[i];
    }
    return 0;
}

static int sacx_read_file_all(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    KFile file;
    uint64_t size64 = 0u;
    uint32_t size = 0u;
    uint32_t read = 0u;
    uint32_t pages = 0u;
    uint8_t *buf = 0;

    if (!path || !out_buf || !out_size)
        return -1;

    *out_buf = 0;
    *out_size = 0u;

    if (kfile_open(&file, path, KFILE_READ) != 0)
        return -1;

    size64 = kfile_size(&file);
    if (size64 == 0u || size64 > SACX_MAX_FILE_BYTES)
    {
        kfile_close(&file);
        return -1;
    }

    size = (uint32_t)size64;
    pages = (size + 4095u) / 4096u;
    buf = (uint8_t *)pmem_alloc_pages((uint64_t)pages);
    if (!buf)
    {
        kfile_close(&file);
        return -1;
    }

    if (kfile_read(&file, buf, size, &read) != 0 || read != size)
    {
        pmem_free_pages(buf, pages);
        kfile_close(&file);
        return -1;
    }

    kfile_close(&file);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

static int sacx_load_image(sacx_task *task, const uint8_t *file_data, uint32_t file_size, const char *friendly_path)
{
    const sacx_header *hdr = 0;
    const uint8_t *segment_bytes = 0;
    const uint8_t *reloc_bytes = 0;
    const uint8_t *import_bytes = 0;
    const char *strings = 0;
    const uint8_t *image_blob = 0;
    uint32_t image_blob_size = 0u;

    if (!task || !file_data || file_size < sizeof(sacx_header))
        return -1;

    hdr = (const sacx_header *)file_data;
    if (hdr->magic != SACX_MAGIC ||
        hdr->version != SACX_VERSION ||
        hdr->header_size < sizeof(sacx_header))
        return -1;

    if (hdr->segment_count > SACX_MAX_SEGMENTS ||
        hdr->reloc_count > SACX_MAX_RELOCS ||
        hdr->import_count > SACX_MAX_IMPORTS)
        return -1;

    if (!sacx_table_ok(hdr->segment_offset, hdr->segment_count, sizeof(sacx_segment), file_size) ||
        !sacx_table_ok(hdr->reloc_offset, hdr->reloc_count, sizeof(sacx_reloc), file_size) ||
        !sacx_table_ok(hdr->import_offset, hdr->import_count, sizeof(sacx_import), file_size) ||
        !sacx_range_ok(hdr->strings_offset, hdr->strings_size, file_size) ||
        !sacx_range_ok(hdr->image_offset, 0u, file_size))
        return -1;

    if (hdr->image_size == 0u || hdr->image_size > SACX_APP_ARENA_BYTES || hdr->entry_rva >= hdr->image_size)
        return -1;

    if (sacx_crc32_zero_range(file_data, file_size, (uint32_t)offsetof(sacx_header, crc32), 4u) != hdr->crc32)
        return -1;

    segment_bytes = file_data + hdr->segment_offset;
    reloc_bytes = file_data + hdr->reloc_offset;
    import_bytes = file_data + hdr->import_offset;
    strings = (const char *)(file_data + hdr->strings_offset);
    image_blob = file_data + hdr->image_offset;
    image_blob_size = file_size - hdr->image_offset;

    for (uint32_t i = 0u; i < hdr->import_count; ++i)
    {
        sacx_import imp;
        const char *name = 0;
        memcpy(&imp, import_bytes + (uint64_t)i * sizeof(sacx_import), sizeof(imp));
        name = sacx_string_at(strings, hdr->strings_size, imp.name_offset);
        if (!name || !sacx_import_known(name))
            return -1;
    }

    task->arena = (uint8_t *)pmem_alloc_executable_pages(SACX_APP_ARENA_PAGES);
    if (!task->arena)
        return -1;
    task->arena_size = SACX_APP_ARENA_BYTES;
    memset(task->arena, 0, task->arena_size);
    task->image_base = task->arena;
    task->image_size = hdr->image_size;

    for (uint32_t i = 0u; i < hdr->segment_count; ++i)
    {
        sacx_segment seg;
        uint8_t *dst = 0;
        const uint8_t *src = 0;

        memcpy(&seg, segment_bytes + (uint64_t)i * sizeof(sacx_segment), sizeof(seg));

        if (seg.mem_size < seg.file_size)
            return -1;
        if (!sacx_range_ok(seg.rva, seg.mem_size, hdr->image_size))
            return -1;
        if (!sacx_range_ok(seg.file_offset, seg.file_size, image_blob_size))
            return -1;

        dst = task->image_base + seg.rva;
        src = image_blob + seg.file_offset;
        if (seg.file_size)
            memcpy(dst, src, seg.file_size);
        if (seg.mem_size > seg.file_size)
            memset(dst + seg.file_size, 0, seg.mem_size - seg.file_size);
    }

    for (uint32_t i = 0u; i < hdr->reloc_count; ++i)
    {
        sacx_reloc rel;
        uint64_t *where = 0;

        memcpy(&rel, reloc_bytes + (uint64_t)i * sizeof(sacx_reloc), sizeof(rel));

        if (rel.type != SACX_RELOC_RELATIVE64)
            return -1;
        if (!sacx_range_ok(rel.target_rva, 8u, hdr->image_size))
            return -1;
        if (rel.addend >= hdr->image_size)
            return -1;

        where = (uint64_t *)(task->image_base + rel.target_rva);
        *where = (uint64_t)(uintptr_t)(task->image_base + rel.addend);
    }

    sacx_sync_executable_range(task->image_base, task->image_size);
    task->entry = (sacx_entry_fn)(uintptr_t)(task->image_base + hdr->entry_rva);
    sacx_copy_trunc(task->friendly_path, sizeof(task->friendly_path), friendly_path ? friendly_path : "");
    return 0;
}

static int sacx_api_set_update(sacx_update_fn fn)
{
    if (!G_current_task)
        return -1;
    if (fn && !sacx_ptr_in_image(G_current_task, (const void *)fn))
        return -1;
    G_current_task->update_fn = fn;
    return 0;
}

static int sacx_api_exit(int status, const char *text)
{
    if (!G_current_task)
        return -1;

    G_current_task->exit_requested = 1u;
    G_current_task->pending_exit_status = status;
    sacx_copy_trunc(G_current_task->pending_exit_message, sizeof(G_current_task->pending_exit_message), text);
    return status;
}

static int sacx_api_yield(void)
{
    if (!G_current_task)
        return -1;
    G_current_task->sleep_requested = 1u;
    G_current_task->pending_wake_tick = dihos_time_ticks() + 1u;
    return 0;
}

static int sacx_api_sleep_ticks(uint64_t ticks)
{
    if (!G_current_task)
        return -1;
    if (ticks == 0u)
        ticks = 1u;
    G_current_task->sleep_requested = 1u;
    G_current_task->pending_wake_tick = dihos_time_ticks() + ticks;
    return 0;
}

static int sacx_api_set_console_visible(uint32_t visible)
{
    if (!G_current_task)
        return -1;
    if (G_current_task->io.set_console_visible)
        G_current_task->io.set_console_visible(visible ? 1u : 0u, G_current_task->io.user);
    return 0;
}

static int sacx_api_sched_preempt_guard_enter(void)
{
    if (!G_current_task)
        return -1;
    if (G_current_task->preempt_guard_depth < 0xFFFFFFFFu)
        G_current_task->preempt_guard_depth++;
    return (int)G_current_task->preempt_guard_depth;
}

static int sacx_api_sched_preempt_guard_leave(void)
{
    if (!G_current_task)
        return -1;
    if (G_current_task->preempt_guard_depth > 0u)
        G_current_task->preempt_guard_depth--;
    return (int)G_current_task->preempt_guard_depth;
}

static uint32_t sacx_api_sched_quantum_ticks(void)
{
    if (!G_current_task || G_current_task->sched_quantum_ticks == 0u)
        return SACX_SCHED_DEFAULT_QUANTUM_TICKS;
    return G_current_task->sched_quantum_ticks;
}

static uint32_t sacx_api_sched_preemptions(void)
{
    if (!G_current_task)
        return 0u;
    return G_current_task->preemptions;
}

static uint64_t sacx_api_time_ticks(void)
{
    return dihos_time_ticks();
}

static uint64_t sacx_api_time_seconds(void)
{
    return dihos_time_seconds();
}

static void sacx_api_log(const char *text)
{
    if (!text)
        return;
    if (G_current_task)
        sacx_task_log(G_current_task, text);
    else
        terminal_print(text);
}

static sacx_file_slot *sacx_file_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_FILES)
        return 0;
    if (!task->files[handle - 1u].used)
        return 0;
    return &task->files[handle - 1u];
}

static int sacx_api_file_open(const char *path, uint32_t flags, uint32_t *out_handle)
{
    uint32_t i = 0u;
    uint32_t kflags = 0u;

    if (!G_current_task || !path || !out_handle)
        return -1;

    if (flags & SACX_FILE_READ)
        kflags |= KFILE_READ;
    if (flags & SACX_FILE_WRITE)
        kflags |= KFILE_WRITE;
    if (flags & SACX_FILE_CREATE)
        kflags |= KFILE_CREATE;
    if (flags & SACX_FILE_TRUNC)
        kflags |= KFILE_TRUNC;
    if (flags & SACX_FILE_APPEND)
        kflags |= KFILE_APPEND;
    if (kflags == 0u)
        kflags = KFILE_READ;

    for (i = 0u; i < SACX_MAX_TASK_FILES; ++i)
    {
        if (G_current_task->files[i].used)
            continue;
        if (kfile_open(&G_current_task->files[i].file, path, kflags) != 0)
            return -1;
        G_current_task->files[i].used = 1u;
        *out_handle = i + 1u;
        return 0;
    }

    return -1;
}

static int sacx_api_file_read(uint32_t handle, void *buf, uint32_t n, uint32_t *out_read)
{
    sacx_file_slot *slot = sacx_file_from_handle(G_current_task, handle);
    if (!slot || !buf || !out_read)
        return -1;
    return kfile_read(&slot->file, buf, n, out_read);
}

static int sacx_api_file_write(uint32_t handle, const void *buf, uint32_t n, uint32_t *out_written)
{
    sacx_file_slot *slot = sacx_file_from_handle(G_current_task, handle);
    if (!slot || !buf || !out_written)
        return -1;
    return kfile_write(&slot->file, buf, n, out_written);
}

static int sacx_api_file_seek(uint32_t handle, uint64_t offs)
{
    sacx_file_slot *slot = sacx_file_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    return kfile_seek(&slot->file, offs);
}

static uint64_t sacx_api_file_size(uint32_t handle)
{
    sacx_file_slot *slot = sacx_file_from_handle(G_current_task, handle);
    if (!slot)
        return 0u;
    return kfile_size(&slot->file);
}

static int sacx_api_file_close(uint32_t handle)
{
    sacx_file_slot *slot = sacx_file_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    kfile_close(&slot->file);
    slot->used = 0u;
    return 0;
}

static int sacx_api_file_unlink(const char *path)
{
    if (!path)
        return -1;
    return kfile_unlink(path);
}

static int sacx_api_file_rename(const char *src, const char *dst)
{
    if (!src || !dst)
        return -1;
    return kfile_rename(src, dst);
}

static int sacx_api_file_mkdir(const char *path)
{
    if (!path)
        return -1;
    return kfile_mkdir(path);
}

static int sacx_api_dir_open(const char *path, uint32_t *out_handle)
{
    if (!G_current_task || !path || !out_handle)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_DIRS; ++i)
    {
        if (G_current_task->dirs[i].used)
            continue;
        if (kdir_open(&G_current_task->dirs[i].dir, path) != 0)
            return -1;
        G_current_task->dirs[i].used = 1u;
        *out_handle = i + 1u;
        return 0;
    }

    return -1;
}

static int sacx_api_dir_next(uint32_t handle, sacx_dirent *out_entry)
{
    sacx_dir_slot *slot = sacx_dir_from_handle(G_current_task, handle);
    kdirent ent;
    int rc = 0;

    if (!slot || !out_entry)
        return -1;

    memset(&ent, 0, sizeof(ent));
    rc = kdir_next(&slot->dir, &ent);
    if (rc == 1)
        sacx_copy_kdirent(&ent, out_entry);
    else
        memset(out_entry, 0, sizeof(*out_entry));
    return rc;
}

static int sacx_api_dir_close(uint32_t handle)
{
    sacx_dir_slot *slot = sacx_dir_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    kdir_close(&slot->dir);
    slot->used = 0u;
    return 0;
}

static int sacx_api_window_create_internal(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                                           const char *title, const sacx_window_style *style, uint32_t *out_handle)
{
    kwindow_handle hnd = {0};
    kwindow_style native_style;

    if (!G_current_task || !out_handle || !G_runtime_font)
        return -1;

    sacx_window_style_to_native(style, &native_style);
    hnd = kwindow_create(x, y, w, h, z, G_runtime_font, title ? title : "SACX App", &native_style);
    if (hnd.idx < 0)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_WINDOWS; ++i)
    {
        uint32_t ignored = 0u;

        if (G_current_task->windows[i].used)
            continue;
        G_current_task->windows[i].used = 1u;
        G_current_task->windows[i].handle = hnd;
        *out_handle = i + 1u;
        (void)sacx_gfx_register_existing(G_current_task, kwindow_root(hnd), &ignored);
        return 0;
    }

    (void)kwindow_destroy(hnd);
    return -1;
}

static int sacx_api_window_create(int32_t x, int32_t y, uint32_t w, uint32_t h, const char *title, uint32_t *out_handle)
{
    return sacx_api_window_create_internal(x, y, w, h, 30, title, 0, out_handle);
}

static int sacx_api_window_create_ex(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                                     const char *title, const sacx_window_style *style, uint32_t *out_handle)
{
    return sacx_api_window_create_internal(x, y, w, h, z, title, style, out_handle);
}

static int sacx_api_window_destroy(uint32_t handle)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    (void)kwindow_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    return 0;
}

static int sacx_api_window_set_visible(uint32_t handle, uint32_t visible)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    kwindow_set_visible(slot->handle, visible ? 1u : 0u);
    return 0;
}

static int sacx_api_window_set_title(uint32_t handle, const char *title)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot || !title)
        return -1;
    kwindow_set_title(slot->handle, title);
    return 0;
}

static int sacx_api_window_visible(uint32_t handle)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return 0;
    return kwindow_visible(slot->handle);
}

static int sacx_api_window_raise(uint32_t handle)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    return kwindow_raise(slot->handle) ? 0 : -1;
}

static void sacx_api_window_set_work_area_bottom_inset(uint32_t px)
{
    kwindow_set_work_area_bottom_inset(px);
}

static int sacx_api_window_root(uint32_t window_handle, uint32_t *out_obj_handle)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, window_handle);
    if (!slot || !out_obj_handle)
        return -1;
    return sacx_gfx_register_existing(G_current_task, kwindow_root(slot->handle), out_obj_handle);
}

static int sacx_api_window_point_can_receive_input(uint32_t window_handle, int32_t x, int32_t y)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, window_handle);
    if (!slot)
        return 0;
    return kwindow_point_can_receive_input(slot->handle, x, y);
}

static void sacx_api_gfx_fill_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    kcolor c = {r, g, b};
    kgfx_fill(c);
}

static void sacx_api_gfx_rect_rgb(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
{
    kcolor c = {r, g, b};
    kgfx_rect(x, y, w, h, c);
}

static void sacx_api_gfx_flush(void)
{
    kgfx_flush();
}

static int sacx_api_input_key_down(uint8_t usage)
{
    return kinput_key_down(usage);
}

static int sacx_api_input_key_pressed(uint8_t usage)
{
    return kinput_key_pressed(usage);
}

static int sacx_api_input_key_released(uint8_t usage)
{
    return kinput_key_released(usage);
}

static kgfx_obj *sacx_api_obj_ref(uint32_t obj_handle)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return 0;
    return kgfx_obj_ref(slot->handle);
}

static int sacx_api_gfx_obj_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                                     sacx_color fill, uint32_t visible, uint32_t *out_obj_handle)
{
    kgfx_obj_handle obj;
    if (!G_current_task || !out_obj_handle)
        return -1;
    obj = kgfx_obj_add_rect(x, y, w, h, z, sacx_to_kcolor(fill), visible ? 1u : 0u);
    if (obj.idx < 0)
        return -1;
    if (sacx_gfx_register_existing(G_current_task, obj, out_obj_handle) != 0)
    {
        (void)kgfx_obj_destroy(obj);
        return -1;
    }
    return 0;
}

static int sacx_api_gfx_obj_add_circle(int32_t cx, int32_t cy, uint32_t r, int32_t z,
                                       sacx_color fill, uint32_t visible, uint32_t *out_obj_handle)
{
    kgfx_obj_handle obj;
    if (!G_current_task || !out_obj_handle)
        return -1;
    obj = kgfx_obj_add_circle(cx, cy, r, z, sacx_to_kcolor(fill), visible ? 1u : 0u);
    if (obj.idx < 0)
        return -1;
    if (sacx_gfx_register_existing(G_current_task, obj, out_obj_handle) != 0)
    {
        (void)kgfx_obj_destroy(obj);
        return -1;
    }
    return 0;
}

static int sacx_api_gfx_obj_add_text(const char *text, int32_t x, int32_t y, int32_t z,
                                     sacx_color color, uint8_t alpha, uint32_t scale,
                                     int32_t char_spacing, int32_t line_spacing, uint32_t align,
                                     uint32_t visible, uint32_t *out_obj_handle)
{
    kgfx_obj_handle obj;
    if (!G_current_task || !out_obj_handle || !G_runtime_font || !text || scale == 0u)
        return -1;
    obj = kgfx_obj_add_text(G_runtime_font, text, x, y, z, sacx_to_kcolor(color), alpha,
                            scale, char_spacing, line_spacing, sacx_to_text_align(align),
                            visible ? 1u : 0u);
    if (obj.idx < 0)
        return -1;
    if (sacx_gfx_register_existing(G_current_task, obj, out_obj_handle) != 0)
    {
        (void)kgfx_obj_destroy(obj);
        return -1;
    }
    return 0;
}

static int sacx_api_gfx_obj_add_image_from_img(uint32_t image_handle, int32_t x, int32_t y, uint32_t *out_obj_handle)
{
    sacx_image_slot *img = sacx_image_from_handle(G_current_task, image_handle);
    kgfx_obj_handle obj;
    if (!img || !img->image.px || !out_obj_handle)
        return -1;
    obj = kgfx_obj_add_image(img->image.px, img->image.w, img->image.h, x, y, img->image.w);
    if (obj.idx < 0)
        return -1;
    if (sacx_gfx_register_existing(G_current_task, obj, out_obj_handle) != 0)
    {
        (void)kgfx_obj_destroy(obj);
        return -1;
    }
    return 0;
}

static int sacx_api_gfx_obj_destroy(uint32_t obj_handle)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    (void)kgfx_obj_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    return 0;
}

static int sacx_api_gfx_obj_set_visible(uint32_t obj_handle, uint32_t visible)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj)
        return -1;
    obj->visible = visible ? 1u : 0u;
    return 0;
}

static int sacx_api_gfx_obj_visible(uint32_t obj_handle)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj)
        return 0;
    return obj->visible ? 1 : 0;
}

static int sacx_api_gfx_obj_set_z(uint32_t obj_handle, int32_t z)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj)
        return -1;
    obj->z = z;
    return 0;
}

static int sacx_api_gfx_obj_z(uint32_t obj_handle, int32_t *out_z)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || !out_z)
        return -1;
    *out_z = obj->z;
    return 0;
}

static int sacx_api_gfx_obj_set_parent(uint32_t child_obj_handle, uint32_t parent_obj_handle)
{
    sacx_gfx_slot *child = sacx_gfx_from_handle(G_current_task, child_obj_handle);
    sacx_gfx_slot *parent = sacx_gfx_from_handle(G_current_task, parent_obj_handle);
    if (!child || !parent)
        return -1;
    kgfx_obj_set_parent(child->handle, parent->handle);
    return 0;
}

static int sacx_api_gfx_obj_clear_parent(uint32_t child_obj_handle)
{
    sacx_gfx_slot *child = sacx_gfx_from_handle(G_current_task, child_obj_handle);
    if (!child)
        return -1;
    kgfx_obj_clear_parent(child->handle);
    return 0;
}

static int sacx_api_gfx_obj_set_clip_to_parent(uint32_t obj_handle, uint32_t enabled)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_obj_set_clip_to_parent(slot->handle, enabled ? 1u : 0u);
    return 0;
}

static int sacx_api_gfx_obj_set_fill_rgb(uint32_t obj_handle, uint8_t r, uint8_t g, uint8_t b)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    kcolor c = {r, g, b};
    if (!slot)
        return -1;
    kgfx_obj_set_fill(slot->handle, c);
    return 0;
}

static int sacx_api_gfx_obj_set_alpha(uint32_t obj_handle, uint8_t alpha)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_obj_set_alpha(slot->handle, alpha);
    return 0;
}

static int sacx_api_gfx_obj_set_outline_rgb(uint32_t obj_handle, uint8_t r, uint8_t g, uint8_t b)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj)
        return -1;
    obj->outline = (kcolor){r, g, b};
    return 0;
}

static int sacx_api_gfx_obj_set_outline_width(uint32_t obj_handle, uint32_t width)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj)
        return -1;
    obj->outline_width = (uint16_t)width;
    return 0;
}

static int sacx_api_gfx_obj_set_outline_alpha(uint32_t obj_handle, uint8_t alpha)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_obj_set_outline_alpha(slot->handle, alpha);
    return 0;
}

static int sacx_api_gfx_obj_set_rect(uint32_t obj_handle, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || obj->kind != KGFX_OBJ_RECT)
        return -1;
    obj->u.rect.x = x;
    obj->u.rect.y = y;
    obj->u.rect.w = w;
    obj->u.rect.h = h;
    return 0;
}

static int sacx_api_gfx_obj_set_circle(uint32_t obj_handle, int32_t cx, int32_t cy, uint32_t r)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || obj->kind != KGFX_OBJ_CIRCLE)
        return -1;
    obj->u.circle.cx = cx;
    obj->u.circle.cy = cy;
    obj->u.circle.r = r;
    return 0;
}

static int sacx_api_gfx_text_set(uint32_t obj_handle, const char *text)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot || !text)
        return -1;
    kgfx_text_set(slot->handle, text);
    return 0;
}

static int sacx_api_gfx_text_set_align(uint32_t obj_handle, uint32_t align)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_text_set_align(slot->handle, sacx_to_text_align(align));
    return 0;
}

static int sacx_api_gfx_text_set_spacing(uint32_t obj_handle, int32_t char_spacing, int32_t line_spacing)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_text_set_spacing(slot->handle, char_spacing, line_spacing);
    return 0;
}

static int sacx_api_gfx_text_set_scale(uint32_t obj_handle, uint32_t scale)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot || scale == 0u)
        return -1;
    kgfx_text_set_scale(slot->handle, scale);
    return 0;
}

static int sacx_api_gfx_text_set_pos(uint32_t obj_handle, int32_t x, int32_t y)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || obj->kind != KGFX_OBJ_TEXT)
        return -1;
    obj->u.text.x = x;
    obj->u.text.y = y;
    return 0;
}

static int sacx_api_gfx_image_set_size(uint32_t obj_handle, uint32_t w, uint32_t h)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot || w == 0u || h == 0u)
        return -1;
    kgfx_image_set_size(slot->handle, w, h);
    return 0;
}

static int sacx_api_gfx_image_set_scale_pct(uint32_t obj_handle, uint32_t scale_pct)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot || scale_pct == 0u)
        return -1;
    kgfx_image_set_scale(slot->handle, scale_pct);
    return 0;
}

static int sacx_api_gfx_image_set_sample_mode(uint32_t obj_handle, uint32_t sample_mode)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_image_set_sample_mode(slot->handle, sacx_to_sample_mode(sample_mode));
    return 0;
}

static int sacx_api_button_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                                    const sacx_button_style *style, sacx_button_on_click_fn on_click,
                                    void *user, uint32_t *out_button_handle)
{
    kbutton_style native_style;
    kbutton_handle button;

    if (!G_current_task || !out_button_handle)
        return -1;

    sacx_button_style_to_native(style, &native_style);
    button = kbutton_add_rect(x, y, w, h, z, &native_style, 0, 0);
    if (button.idx < 0)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_BUTTONS; ++i)
    {
        uint32_t ignored = 0u;

        if (G_current_task->buttons[i].used)
            continue;

        G_current_task->buttons[i].used = 1u;
        G_current_task->buttons[i].handle = button;
        G_current_task->buttons[i].callback = on_click;
        G_current_task->buttons[i].callback_user = user;
        G_current_task->buttons[i].owner = G_current_task;
        *out_button_handle = i + 1u;

        kbutton_set_callback(button, on_click ? sacx_button_click_trampoline : 0, &G_current_task->buttons[i]);
        if (sacx_gfx_register_existing(G_current_task, kbutton_root(button), &ignored) != 0)
        {
            (void)kbutton_destroy(button);
            G_current_task->buttons[i].used = 0u;
            G_current_task->buttons[i].handle.idx = -1;
            return -1;
        }
        return 0;
    }

    (void)kbutton_destroy(button);
    return -1;
}

static int sacx_api_button_destroy(uint32_t button_handle)
{
    sacx_button_slot *slot = sacx_button_from_handle(G_current_task, button_handle);
    if (!slot)
        return -1;
    (void)kbutton_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    slot->callback = 0;
    slot->callback_user = 0;
    slot->owner = 0;
    return 0;
}

static int sacx_api_button_root(uint32_t button_handle, uint32_t *out_obj_handle)
{
    sacx_button_slot *slot = sacx_button_from_handle(G_current_task, button_handle);
    if (!slot || !out_obj_handle)
        return -1;
    return sacx_gfx_register_existing(G_current_task, kbutton_root(slot->handle), out_obj_handle);
}

static int sacx_api_button_set_callback(uint32_t button_handle, sacx_button_on_click_fn on_click, void *user)
{
    sacx_button_slot *slot = sacx_button_from_handle(G_current_task, button_handle);
    if (!slot)
        return -1;
    slot->callback = on_click;
    slot->callback_user = user;
    kbutton_set_callback(slot->handle, on_click ? sacx_button_click_trampoline : 0, slot);
    return 0;
}

static int sacx_api_button_set_style(uint32_t button_handle, const sacx_button_style *style)
{
    sacx_button_slot *slot = sacx_button_from_handle(G_current_task, button_handle);
    kbutton_style native_style;
    if (!slot || !style)
        return -1;
    sacx_button_style_to_native(style, &native_style);
    kbutton_set_style(slot->handle, &native_style);
    return 0;
}

static int sacx_api_button_set_enabled(uint32_t button_handle, uint32_t enabled)
{
    sacx_button_slot *slot = sacx_button_from_handle(G_current_task, button_handle);
    if (!slot)
        return -1;
    kbutton_set_enabled(slot->handle, enabled ? 1u : 0u);
    return 0;
}

static int sacx_api_button_enabled(uint32_t button_handle)
{
    sacx_button_slot *slot = sacx_button_from_handle(G_current_task, button_handle);
    if (!slot)
        return 0;
    return kbutton_enabled(slot->handle);
}

static int sacx_api_button_hovered(uint32_t button_handle)
{
    sacx_button_slot *slot = sacx_button_from_handle(G_current_task, button_handle);
    if (!slot)
        return 0;
    return kbutton_hovered(slot->handle);
}

static int sacx_api_button_pressed(uint32_t button_handle)
{
    sacx_button_slot *slot = sacx_button_from_handle(G_current_task, button_handle);
    if (!slot)
        return 0;
    return kbutton_pressed(slot->handle);
}

static int sacx_api_textbox_add_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t z,
                                     const sacx_textbox_style *style, sacx_textbox_on_submit_fn on_submit,
                                     void *user, uint32_t *out_textbox_handle)
{
    ktextbox_style native_style;
    ktextbox_handle textbox;

    if (!G_current_task || !out_textbox_handle || !G_runtime_font)
        return -1;

    sacx_textbox_style_to_native(style, &native_style);
    textbox = ktextbox_add_rect(x, y, w, h, z, G_runtime_font, &native_style, 0, 0);
    if (textbox.idx < 0)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_TEXTBOXES; ++i)
    {
        uint32_t ignored = 0u;

        if (G_current_task->textboxes[i].used)
            continue;

        G_current_task->textboxes[i].used = 1u;
        G_current_task->textboxes[i].handle = textbox;
        G_current_task->textboxes[i].callback = on_submit;
        G_current_task->textboxes[i].callback_user = user;
        G_current_task->textboxes[i].owner = G_current_task;
        *out_textbox_handle = i + 1u;

        ktextbox_set_callback(textbox, on_submit ? sacx_textbox_submit_trampoline : 0, &G_current_task->textboxes[i]);
        if (sacx_gfx_register_existing(G_current_task, ktextbox_root(textbox), &ignored) != 0)
        {
            (void)ktextbox_destroy(textbox);
            G_current_task->textboxes[i].used = 0u;
            G_current_task->textboxes[i].handle.idx = -1;
            return -1;
        }
        return 0;
    }

    (void)ktextbox_destroy(textbox);
    return -1;
}

static int sacx_api_textbox_destroy(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    (void)ktextbox_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    slot->callback = 0;
    slot->callback_user = 0;
    slot->owner = 0;
    return 0;
}

static int sacx_api_textbox_root(uint32_t textbox_handle, uint32_t *out_obj_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot || !out_obj_handle)
        return -1;
    return sacx_gfx_register_existing(G_current_task, ktextbox_root(slot->handle), out_obj_handle);
}

static int sacx_api_textbox_set_callback(uint32_t textbox_handle, sacx_textbox_on_submit_fn on_submit, void *user)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    slot->callback = on_submit;
    slot->callback_user = user;
    ktextbox_set_callback(slot->handle, on_submit ? sacx_textbox_submit_trampoline : 0, slot);
    return 0;
}

static int sacx_api_textbox_set_enabled(uint32_t textbox_handle, uint32_t enabled)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    ktextbox_set_enabled(slot->handle, enabled ? 1u : 0u);
    return 0;
}

static int sacx_api_textbox_enabled(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return 0;
    return ktextbox_enabled(slot->handle);
}

static int sacx_api_textbox_set_focus(uint32_t textbox_handle, uint32_t focused)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    ktextbox_set_focus(slot->handle, focused ? 1u : 0u);
    return 0;
}

static void sacx_api_textbox_clear_focus(void)
{
    ktextbox_clear_focus();
}

static int sacx_api_textbox_focused(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return 0;
    return ktextbox_focused(slot->handle);
}

static int sacx_api_textbox_set_bounds(uint32_t textbox_handle, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    ktextbox_set_bounds(slot->handle, x, y, w, h);
    return 0;
}

static int sacx_api_textbox_set_text(uint32_t textbox_handle, const char *text)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot || !text)
        return -1;
    ktextbox_set_text(slot->handle, text);
    return 0;
}

static int sacx_api_textbox_clear(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    ktextbox_clear(slot->handle);
    return 0;
}

static int sacx_api_textbox_text_copy(uint32_t textbox_handle, char *dst, uint32_t cap)
{
    const char *src = 0;
    uint32_t copied = 0u;
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot || !dst || cap == 0u)
        return -1;
    src = ktextbox_text(slot->handle);
    sacx_copy_trunc(dst, cap, src ? src : "");
    while (copied < cap && dst[copied])
        ++copied;
    return (int)copied;
}

static int32_t sacx_api_input_mouse_dx(void)
{
    return kinput_mouse_dx();
}

static int32_t sacx_api_input_mouse_dy(void)
{
    return kinput_mouse_dy();
}

static int32_t sacx_api_input_mouse_wheel(void)
{
    return kinput_mouse_wheel();
}

static uint8_t sacx_api_input_mouse_buttons(void)
{
    return kinput_mouse_buttons();
}

static int sacx_api_input_mouse_consume(sacx_mouse_state *out_state)
{
    kinput_mouse_state raw;

    if (!out_state)
        return -1;

    memset(&raw, 0, sizeof(raw));
    kinput_mouse_consume(&raw);
    memset(out_state, 0, sizeof(*out_state));
    out_state->x = kmouse_x();
    out_state->y = kmouse_y();
    out_state->dx = raw.dx;
    out_state->dy = raw.dy;
    out_state->wheel = raw.wheel;
    out_state->buttons = raw.buttons;
    out_state->visible = kmouse_visible();
    return 0;
}

static int sacx_api_mouse_set_cursor(uint32_t cursor)
{
    return kmouse_set_cursor(sacx_to_mouse_cursor(cursor));
}

static uint32_t sacx_api_mouse_current_cursor(void)
{
    return (uint32_t)kmouse_current_cursor();
}

static void sacx_api_mouse_set_sensitivity_pct(uint32_t pct)
{
    kmouse_set_sensitivity_pct(pct);
}

static uint32_t sacx_api_mouse_sensitivity_pct(void)
{
    return kmouse_sensitivity_pct();
}

static int32_t sacx_api_mouse_x(void)
{
    return kmouse_x();
}

static int32_t sacx_api_mouse_y(void)
{
    return kmouse_y();
}

static int32_t sacx_api_mouse_dx(void)
{
    return kmouse_dx();
}

static int32_t sacx_api_mouse_dy(void)
{
    return kmouse_dy();
}

static int32_t sacx_api_mouse_wheel(void)
{
    return kmouse_wheel();
}

static uint8_t sacx_api_mouse_buttons(void)
{
    return kmouse_buttons();
}

static uint8_t sacx_api_mouse_visible(void)
{
    return kmouse_visible();
}

static int sacx_api_mouse_get_state(sacx_mouse_state *out_state)
{
    kmouse_state in;
    if (!out_state)
        return -1;
    memset(&in, 0, sizeof(in));
    kmouse_get_state(&in);
    out_state->x = in.x;
    out_state->y = in.y;
    out_state->dx = in.dx;
    out_state->dy = in.dy;
    out_state->wheel = in.wheel;
    out_state->buttons = in.buttons;
    out_state->visible = in.visible;
    return 0;
}

static int sacx_api_text_draw(int32_t x, int32_t y, const char *text,
                              sacx_color color, uint8_t alpha, uint32_t scale,
                              int32_t char_spacing, int32_t line_spacing)
{
    if (!G_runtime_font || !text || scale == 0u)
        return -1;
    ktext_draw_str_ex(G_runtime_font, x, y, text, sacx_to_kcolor(color), alpha, scale, char_spacing, line_spacing);
    return 0;
}

static int sacx_api_text_draw_align(int32_t anchor_x, int32_t y, const char *text,
                                    sacx_color color, uint8_t alpha, uint32_t scale,
                                    int32_t char_spacing, int32_t line_spacing, uint32_t align)
{
    if (!G_runtime_font || !text || scale == 0u)
        return -1;
    ktext_draw_str_align(G_runtime_font, anchor_x, y, text, sacx_to_kcolor(color), alpha, scale,
                         char_spacing, line_spacing, sacx_to_text_align(align));
    return 0;
}

static int sacx_api_text_draw_outline_align(int32_t anchor_x, int32_t y, const char *text,
                                            sacx_color fill, uint8_t fill_alpha, uint32_t scale,
                                            int32_t char_spacing, int32_t line_spacing, uint32_t align,
                                            uint32_t outline_width, sacx_color outline, uint8_t outline_alpha)
{
    if (!G_runtime_font || !text || scale == 0u)
        return -1;
    ktext_draw_str_align_outline(G_runtime_font, anchor_x, y, text,
                                 sacx_to_kcolor(fill), fill_alpha, scale,
                                 char_spacing, line_spacing, sacx_to_text_align(align),
                                 outline_width, sacx_to_kcolor(outline), outline_alpha);
    return 0;
}

static uint32_t sacx_api_text_measure_line_px(const char *text, uint32_t scale, int32_t char_spacing)
{
    if (!G_runtime_font || !text || scale == 0u)
        return 0u;
    return ktext_measure_line_px(G_runtime_font, text, scale, char_spacing);
}

static uint32_t sacx_api_text_line_height(uint32_t scale, int32_t line_spacing)
{
    if (!G_runtime_font || scale == 0u)
        return 0u;
    return ktext_line_height(G_runtime_font, scale, line_spacing);
}

static uint32_t sacx_api_text_scale_mul_px(uint32_t px, uint32_t scale)
{
    return ktext_scale_mul_px(px, scale ? scale : 1u);
}

static int sacx_api_img_register_loaded(kimg *image, uint32_t *out_image_handle)
{
    if (!G_current_task || !image || !image->px || !out_image_handle)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_IMAGES; ++i)
    {
        if (G_current_task->images[i].used)
            continue;
        G_current_task->images[i].used = 1u;
        G_current_task->images[i].image = *image;
        *out_image_handle = i + 1u;
        return 0;
    }
    return -1;
}

static int sacx_api_img_load(const char *path, uint32_t *out_image_handle)
{
    kimg image = {0};
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!path || !out_image_handle)
        return -1;
    if (kimg_load(&image, path) != 0)
        return -1;
    if (sacx_api_img_register_loaded(&image, out_image_handle) != 0)
    {
        bytes = (uint64_t)image.w * (uint64_t)image.h * 4u;
        pages = (bytes + 4095u) / 4096u;
        if (image.px && pages)
            pmem_free_pages(image.px, pages);
        return -1;
    }
    return 0;
}

static int sacx_api_img_load_bmp(const char *path, uint32_t *out_image_handle)
{
    kimg image = {0};
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!path || !out_image_handle)
        return -1;
    if (kimg_load_bmp(&image, path) != 0)
        return -1;
    if (sacx_api_img_register_loaded(&image, out_image_handle) != 0)
    {
        bytes = (uint64_t)image.w * (uint64_t)image.h * 4u;
        pages = (bytes + 4095u) / 4096u;
        if (image.px && pages)
            pmem_free_pages(image.px, pages);
        return -1;
    }
    return 0;
}

static int sacx_api_img_load_png(const char *path, uint32_t *out_image_handle)
{
    kimg image = {0};
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!path || !out_image_handle)
        return -1;
    if (kimg_load_png(&image, path) != 0)
        return -1;
    if (sacx_api_img_register_loaded(&image, out_image_handle) != 0)
    {
        bytes = (uint64_t)image.w * (uint64_t)image.h * 4u;
        pages = (bytes + 4095u) / 4096u;
        if (image.px && pages)
            pmem_free_pages(image.px, pages);
        return -1;
    }
    return 0;
}

static int sacx_api_img_load_jpg(const char *path, uint32_t *out_image_handle)
{
    kimg image = {0};
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!path || !out_image_handle)
        return -1;
    if (kimg_load_jpg(&image, path) != 0)
        return -1;
    if (sacx_api_img_register_loaded(&image, out_image_handle) != 0)
    {
        bytes = (uint64_t)image.w * (uint64_t)image.h * 4u;
        pages = (bytes + 4095u) / 4096u;
        if (image.px && pages)
            pmem_free_pages(image.px, pages);
        return -1;
    }
    return 0;
}

static int sacx_api_img_draw(uint32_t image_handle, int32_t x, int32_t y, uint8_t alpha)
{
    sacx_image_slot *slot = sacx_image_from_handle(G_current_task, image_handle);
    uint64_t idx = 0u;
    uint8_t sr = 0u;
    uint8_t sg = 0u;
    uint8_t sb = 0u;
    uint8_t sa = 0u;
    uint8_t a = 0u;
    kcolor c;
    if (!slot || !slot->image.px)
        return -1;

    for (uint32_t iy = 0u; iy < slot->image.h; ++iy)
    {
        for (uint32_t ix = 0u; ix < slot->image.w; ++ix)
        {
            uint32_t p = slot->image.px[idx++];

            sa = (uint8_t)(p >> 24);
            if (sa == 0u)
                continue;

            sr = (uint8_t)(p >> 16);
            sg = (uint8_t)(p >> 8);
            sb = (uint8_t)p;
            a = (uint8_t)(((uint16_t)sa * (uint16_t)alpha) / 255u);
            if (a == 0u)
                continue;

            c = (kcolor){sr, sg, sb};
            kgfx_put_px_blend(x + (int32_t)ix, y + (int32_t)iy, c, a);
        }
    }

    return 0;
}

static int sacx_api_img_destroy(uint32_t image_handle)
{
    sacx_image_slot *slot = sacx_image_from_handle(G_current_task, image_handle);
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!slot)
        return -1;

    if (slot->image.px && slot->image.w && slot->image.h)
    {
        bytes = (uint64_t)slot->image.w * (uint64_t)slot->image.h * 4u;
        pages = (bytes + 4095u) / 4096u;
        if (pages)
            pmem_free_pages(slot->image.px, pages);
    }

    slot->used = 0u;
    slot->image.w = 0u;
    slot->image.h = 0u;
    slot->image.px = 0;
    return 0;
}

static int sacx_api_img_size(uint32_t image_handle, uint32_t *out_w, uint32_t *out_h)
{
    sacx_image_slot *slot = sacx_image_from_handle(G_current_task, image_handle);
    if (!slot || !out_w || !out_h)
        return -1;
    *out_w = slot->image.w;
    *out_h = slot->image.h;
    return 0;
}

static void sacx_task_init_api(sacx_task *task)
{
    if (!task)
        return;

    memset(&task->api, 0, sizeof(task->api));
    task->api.abi_version = SACX_API_ABI_VERSION;
    task->api.struct_size = sizeof(task->api);
    task->api.app_set_update = sacx_api_set_update;
    task->api.app_exit = sacx_api_exit;
    task->api.app_yield = sacx_api_yield;
    task->api.app_sleep_ticks = sacx_api_sleep_ticks;
    task->api.time_ticks = sacx_api_time_ticks;
    task->api.time_seconds = sacx_api_time_seconds;
    task->api.log = sacx_api_log;

    task->api.file_open = sacx_api_file_open;
    task->api.file_read = sacx_api_file_read;
    task->api.file_write = sacx_api_file_write;
    task->api.file_seek = sacx_api_file_seek;
    task->api.file_size = sacx_api_file_size;
    task->api.file_close = sacx_api_file_close;
    task->api.file_unlink = sacx_api_file_unlink;
    task->api.file_rename = sacx_api_file_rename;
    task->api.file_mkdir = sacx_api_file_mkdir;

    task->api.window_create = sacx_api_window_create;
    task->api.window_destroy = sacx_api_window_destroy;
    task->api.window_set_visible = sacx_api_window_set_visible;
    task->api.window_set_title = sacx_api_window_set_title;

    task->api.gfx_fill_rgb = sacx_api_gfx_fill_rgb;
    task->api.gfx_rect_rgb = sacx_api_gfx_rect_rgb;
    task->api.gfx_flush = sacx_api_gfx_flush;

    task->api.input_key_down = sacx_api_input_key_down;
    task->api.input_key_pressed = sacx_api_input_key_pressed;
    task->api.input_key_released = sacx_api_input_key_released;

    task->api.dir_open = sacx_api_dir_open;
    task->api.dir_next = sacx_api_dir_next;
    task->api.dir_close = sacx_api_dir_close;

    task->api.window_create_ex = sacx_api_window_create_ex;
    task->api.window_visible = sacx_api_window_visible;
    task->api.window_raise = sacx_api_window_raise;
    task->api.window_set_work_area_bottom_inset = sacx_api_window_set_work_area_bottom_inset;
    task->api.window_root = sacx_api_window_root;
    task->api.window_point_can_receive_input = sacx_api_window_point_can_receive_input;

    task->api.gfx_obj_add_rect = sacx_api_gfx_obj_add_rect;
    task->api.gfx_obj_add_circle = sacx_api_gfx_obj_add_circle;
    task->api.gfx_obj_add_text = sacx_api_gfx_obj_add_text;
    task->api.gfx_obj_add_image_from_img = sacx_api_gfx_obj_add_image_from_img;
    task->api.gfx_obj_destroy = sacx_api_gfx_obj_destroy;
    task->api.gfx_obj_set_visible = sacx_api_gfx_obj_set_visible;
    task->api.gfx_obj_visible = sacx_api_gfx_obj_visible;
    task->api.gfx_obj_set_z = sacx_api_gfx_obj_set_z;
    task->api.gfx_obj_z = sacx_api_gfx_obj_z;
    task->api.gfx_obj_set_parent = sacx_api_gfx_obj_set_parent;
    task->api.gfx_obj_clear_parent = sacx_api_gfx_obj_clear_parent;
    task->api.gfx_obj_set_clip_to_parent = sacx_api_gfx_obj_set_clip_to_parent;
    task->api.gfx_obj_set_fill_rgb = sacx_api_gfx_obj_set_fill_rgb;
    task->api.gfx_obj_set_alpha = sacx_api_gfx_obj_set_alpha;
    task->api.gfx_obj_set_outline_rgb = sacx_api_gfx_obj_set_outline_rgb;
    task->api.gfx_obj_set_outline_width = sacx_api_gfx_obj_set_outline_width;
    task->api.gfx_obj_set_outline_alpha = sacx_api_gfx_obj_set_outline_alpha;
    task->api.gfx_obj_set_rect = sacx_api_gfx_obj_set_rect;
    task->api.gfx_obj_set_circle = sacx_api_gfx_obj_set_circle;
    task->api.gfx_text_set = sacx_api_gfx_text_set;
    task->api.gfx_text_set_align = sacx_api_gfx_text_set_align;
    task->api.gfx_text_set_spacing = sacx_api_gfx_text_set_spacing;
    task->api.gfx_text_set_scale = sacx_api_gfx_text_set_scale;
    task->api.gfx_text_set_pos = sacx_api_gfx_text_set_pos;
    task->api.gfx_image_set_size = sacx_api_gfx_image_set_size;
    task->api.gfx_image_set_scale_pct = sacx_api_gfx_image_set_scale_pct;
    task->api.gfx_image_set_sample_mode = sacx_api_gfx_image_set_sample_mode;

    task->api.button_add_rect = sacx_api_button_add_rect;
    task->api.button_destroy = sacx_api_button_destroy;
    task->api.button_root = sacx_api_button_root;
    task->api.button_set_callback = sacx_api_button_set_callback;
    task->api.button_set_style = sacx_api_button_set_style;
    task->api.button_set_enabled = sacx_api_button_set_enabled;
    task->api.button_enabled = sacx_api_button_enabled;
    task->api.button_hovered = sacx_api_button_hovered;
    task->api.button_pressed = sacx_api_button_pressed;

    task->api.textbox_add_rect = sacx_api_textbox_add_rect;
    task->api.textbox_destroy = sacx_api_textbox_destroy;
    task->api.textbox_root = sacx_api_textbox_root;
    task->api.textbox_set_callback = sacx_api_textbox_set_callback;
    task->api.textbox_set_enabled = sacx_api_textbox_set_enabled;
    task->api.textbox_enabled = sacx_api_textbox_enabled;
    task->api.textbox_set_focus = sacx_api_textbox_set_focus;
    task->api.textbox_clear_focus = sacx_api_textbox_clear_focus;
    task->api.textbox_focused = sacx_api_textbox_focused;
    task->api.textbox_set_bounds = sacx_api_textbox_set_bounds;
    task->api.textbox_set_text = sacx_api_textbox_set_text;
    task->api.textbox_clear = sacx_api_textbox_clear;
    task->api.textbox_text_copy = sacx_api_textbox_text_copy;

    task->api.input_mouse_dx = sacx_api_input_mouse_dx;
    task->api.input_mouse_dy = sacx_api_input_mouse_dy;
    task->api.input_mouse_wheel = sacx_api_input_mouse_wheel;
    task->api.input_mouse_buttons = sacx_api_input_mouse_buttons;
    task->api.input_mouse_consume = sacx_api_input_mouse_consume;

    task->api.mouse_set_cursor = sacx_api_mouse_set_cursor;
    task->api.mouse_current_cursor = sacx_api_mouse_current_cursor;
    task->api.mouse_set_sensitivity_pct = sacx_api_mouse_set_sensitivity_pct;
    task->api.mouse_sensitivity_pct = sacx_api_mouse_sensitivity_pct;
    task->api.mouse_x = sacx_api_mouse_x;
    task->api.mouse_y = sacx_api_mouse_y;
    task->api.mouse_dx = sacx_api_mouse_dx;
    task->api.mouse_dy = sacx_api_mouse_dy;
    task->api.mouse_wheel = sacx_api_mouse_wheel;
    task->api.mouse_buttons = sacx_api_mouse_buttons;
    task->api.mouse_visible = sacx_api_mouse_visible;
    task->api.mouse_get_state = sacx_api_mouse_get_state;

    task->api.text_draw = sacx_api_text_draw;
    task->api.text_draw_align = sacx_api_text_draw_align;
    task->api.text_draw_outline_align = sacx_api_text_draw_outline_align;
    task->api.text_measure_line_px = sacx_api_text_measure_line_px;
    task->api.text_line_height = sacx_api_text_line_height;
    task->api.text_scale_mul_px = sacx_api_text_scale_mul_px;

    task->api.img_load = sacx_api_img_load;
    task->api.img_load_bmp = sacx_api_img_load_bmp;
    task->api.img_load_png = sacx_api_img_load_png;
    task->api.img_load_jpg = sacx_api_img_load_jpg;
    task->api.img_draw = sacx_api_img_draw;
    task->api.img_destroy = sacx_api_img_destroy;
    task->api.img_size = sacx_api_img_size;

    task->api.sched_preempt_guard_enter = sacx_api_sched_preempt_guard_enter;
    task->api.sched_preempt_guard_leave = sacx_api_sched_preempt_guard_leave;
    task->api.sched_quantum_ticks = sacx_api_sched_quantum_ticks;
    task->api.sched_preemptions = sacx_api_sched_preemptions;
    task->api.app_set_console_visible = sacx_api_set_console_visible;
}

extern "C" int sacx_runtime_init(const kfont *font)
{
    for (uint32_t i = 0u; i < SACX_MAX_TASKS; ++i)
        sacx_task_reset(&G_tasks[i]);
    G_runtime_font = font;
    G_next_task_id = 1u;
    G_rr_cursor = 0u;
    G_current_task = 0;
    return 0;
}

extern "C" void sacx_runtime_set_font(const kfont *font)
{
    G_runtime_font = font;
}

extern "C" int sacx_runtime_launch(const char *raw_path,
                                   const char *friendly_path,
                                   const sacx_runtime_io *io,
                                   uint32_t *out_task_id)
{
    sacx_task *task = 0;
    uint8_t *file_data = 0;
    uint32_t file_size = 0u;
    int rc = -1;

    if (!raw_path || !raw_path[0])
        return -1;

    task = sacx_alloc_task_slot();
    if (!task)
        return -1;

    sacx_task_reset(task);
    task->task_id = G_next_task_id++;
    if (!G_next_task_id)
        G_next_task_id = 1u;
    task->state = SACX_TASK_READY;
    task->wake_tick = dihos_time_ticks();
    if (io)
        task->io = *io;

    if (sacx_read_file_all(raw_path, &file_data, &file_size) != 0)
    {
        sacx_task_reset(task);
        return -1;
    }

    rc = sacx_load_image(task, file_data, file_size, friendly_path ? friendly_path : raw_path);
    pmem_free_pages(file_data, (file_size + 4095u) / 4096u);
    if (rc != 0)
    {
        if (task->arena)
            pmem_free_executable_pages(task->arena, task->arena_size / 4096u);
        sacx_task_reset(task);
        return -1;
    }

    sacx_task_init_api(task);
    if (out_task_id)
        *out_task_id = task->task_id;

    {
        char msg[320];
        ksb b;
        ksb_init(&b, msg, sizeof(msg));
        ksb_puts(&b, "[sacx] launched ");
        ksb_puts(&b, task->friendly_path[0] ? task->friendly_path : raw_path);
        sacx_task_log(task, msg);
    }

    return 0;
}

extern "C" void sacx_runtime_update(void)
{
    uint64_t now = dihos_time_ticks();

    for (uint32_t i = 0u; i < SACX_MAX_TASKS; ++i)
    {
        sacx_task *task = &G_tasks[i];
        if (task->state == SACX_TASK_SLEEPING && now >= task->wake_tick)
            task->state = SACX_TASK_READY;
        if (task->state != SACX_TASK_UNUSED)
            task->state_age++;
    }

    for (uint32_t pass = 0u; pass < SACX_MAX_TASKS; ++pass)
    {
        uint32_t idx = (G_rr_cursor + pass) % SACX_MAX_TASKS;
        sacx_task *task = &G_tasks[idx];
        uint32_t quantum = 0u;
        uint8_t preempted = 0u;
        int rc = 0;
        const char *fault_message = 0;

        if (task->state != SACX_TASK_READY)
            continue;

        G_current_task = task;
        task->sleep_requested = 0u;
        task->pending_wake_tick = 0u;
        task->exit_requested = 0u;
        task->pending_exit_message[0] = 0;
        task->pending_exit_status = 0;

        if (!task->started)
        {
            if (!task->entry || !sacx_ptr_in_image(task, (const void *)task->entry))
            {
                rc = -1;
                fault_message = "invalid entry pointer";
            }
            else
            {
                task->started = 1u;
                rc = task->entry(&task->api);
            }
        }
        else if (task->update_fn)
        {
            if (!sacx_ptr_in_image(task, (const void *)task->update_fn))
            {
                rc = -1;
                fault_message = "invalid update pointer";
            }
            else
            {
                rc = task->update_fn(&task->api);
            }
        }

        G_current_task = 0;

        if (task->exit_requested)
        {
            sacx_task_finish(task, task->pending_exit_status, task->pending_exit_message, SACX_TASK_EXITED);
        }
        else if (task->sleep_requested)
        {
            task->state = SACX_TASK_SLEEPING;
            task->wake_tick = task->pending_wake_tick ? task->pending_wake_tick : (now + 1u);
            task->sched_budget_left = task->sched_quantum_ticks ? task->sched_quantum_ticks : SACX_SCHED_DEFAULT_QUANTUM_TICKS;
        }
        else if (rc != 0)
        {
            sacx_task_finish(task, rc, fault_message ? fault_message : "",
                             fault_message ? SACX_TASK_FAULTED : SACX_TASK_EXITED);
        }
        else if (task->started && !task->update_fn)
        {
            sacx_task_finish(task, 0, "", SACX_TASK_EXITED);
        }
        else
        {
            quantum = task->sched_quantum_ticks ? task->sched_quantum_ticks : SACX_SCHED_DEFAULT_QUANTUM_TICKS;
            if (task->sched_budget_left == 0u || task->sched_budget_left > quantum)
                task->sched_budget_left = quantum;

            if (task->preempt_guard_depth == 0u)
            {
                if (task->sched_budget_left > 0u)
                    task->sched_budget_left--;
                if (task->sched_budget_left == 0u)
                {
                    preempted = 1u;
                    task->preemptions++;
                    task->sched_budget_left = quantum;
                }
            }

            if (preempted)
            {
                task->state = SACX_TASK_SLEEPING;
                task->wake_tick = now + 1u;
            }
            else
            {
                task->state = SACX_TASK_READY;
            }
        }

        G_rr_cursor = (idx + 1u) % SACX_MAX_TASKS;
    }
}

extern "C" int sacx_runtime_task_status(uint32_t task_id, sacx_task_status *out_status)
{
    sacx_task *task = sacx_find_task_by_id(task_id);

    if (!task || !out_status)
        return -1;

    memset(out_status, 0, sizeof(*out_status));
    out_status->task_id = task->task_id;
    out_status->state = task->state;
    out_status->exit_status = task->exit_status;
    out_status->wake_tick = task->wake_tick;
    sacx_copy_trunc(out_status->message, sizeof(out_status->message), task->exit_message);
    return 0;
}

extern "C" int sacx_runtime_task_release(uint32_t task_id)
{
    sacx_task *task = sacx_find_task_by_id(task_id);
    if (!task)
        return -1;
    if (task->state != SACX_TASK_EXITED && task->state != SACX_TASK_FAULTED)
        return -1;
    sacx_task_reset(task);
    return 0;
}
