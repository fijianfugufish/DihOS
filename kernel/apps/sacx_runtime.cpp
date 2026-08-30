#include "apps/sacx_runtime.h"
#include "apps/sacx_api.h"
#include "apps/sacx_format.h"
#include "apps/file_explorer_api.h"
#include "asm/asm.h"

#include <stddef.h>

extern "C"
{
#include "kwrappers/colors.h"
#include "kwrappers/kbutton.h"
#include "kwrappers/k3d.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/kimg.h"
#include "kwrappers/kinput.h"
#include "kwrappers/kmouse.h"
#include "kwrappers/ktext.h"
#include "kwrappers/ktextbox.h"
#include "kwrappers/kui.h"
#include "kwrappers/kwindow.h"
#include "kwrappers/string.h"
#include "bootinfo.h"
#include "memory/pmem.h"
#include "net/knet_usb.h"
#include "system/dihos_time.h"
#include "system/kbusy.h"
#include "system/kwork.h"
#include "system/smp.h"
#include "system/kimage_clipboard.h"
#include "terminal/terminal_api.h"

extern const boot_info *k_bootinfo_ptr;
}

#define SACX_MAX_TASKS 16u
#define SACX_MAX_TASK_FILES 24u
#define SACX_MAX_TASK_DIRS 24u
#define SACX_MAX_TASK_WINDOWS 16u
#define SACX_MAX_TASK_BUTTONS 64u
#define SACX_MAX_TASK_TEXTBOXES 32u
#define SACX_MAX_TASK_UI_VIEWS 32u
#define SACX_MAX_TASK_UI_DROPDOWNS 32u
#define SACX_MAX_TASK_UI_RADIOS 32u
#define SACX_MAX_TASK_UI_TOGGLES 32u
#define SACX_MAX_TASK_GFX_OBJECTS 512u
#define SACX_MAX_TASK_IMAGES 128u
#define SACX_MAX_TASK_3D_SCENES 8u
#define SACX_MAX_TASK_3D_PLAYERS 8u
#define SACX_MAX_TASK_WORKERS 8u
#define SACX_MAX_TASK_ASYNC_IMAGE_SAVES 1u
#define SACX_MAX_TASK_NET_REQUESTS 4u
#define SACX_MAX_TASK_MEMORY_ALLOCS 16u
#define SACX_MAX_TASK_MEMORY_BYTES (128u * 1024u * 1024u)
#define SACX_NET_DEFAULT_BYTES (2u * 1024u * 1024u)
#define SACX_NET_MAX_BYTES (8u * 1024u * 1024u)
#define SACX_MAX_SEGMENTS 128u
#define SACX_MAX_RELOCS 8192u
#define SACX_MAX_IMPORTS 256u
#define SACX_MAX_SLICES 8u
#define SACX_MAX_FILE_BYTES (32u * 1024u * 1024u)
#define SACX_APP_ARENA_BYTES (128u * 1024u * 1024u)
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
    uint8_t destroy_on_finish;
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

typedef struct sacx_ui_view_slot
{
    kui_view_handle handle;
    uint8_t used;
} sacx_ui_view_slot;

typedef struct sacx_ui_dropdown_slot
{
    kui_dropdown_handle handle;
    uint8_t used;
    sacx_ui_on_change_fn callback;
    void *callback_user;
    sacx_task *owner;
} sacx_ui_dropdown_slot;

typedef struct sacx_ui_radio_slot
{
    kui_radio_handle handle;
    uint8_t used;
    sacx_ui_on_change_fn callback;
    void *callback_user;
    sacx_task *owner;
} sacx_ui_radio_slot;

typedef struct sacx_ui_toggle_slot
{
    kui_toggle_handle handle;
    uint8_t used;
    sacx_ui_on_change_fn callback;
    void *callback_user;
    sacx_task *owner;
} sacx_ui_toggle_slot;

typedef struct sacx_image_slot
{
    kimg image;
    uint8_t used;
} sacx_image_slot;

typedef struct sacx_3d_scene_slot
{
    k3d_scene_handle handle;
    uint32_t root_obj_handle;
    uint8_t used;
} sacx_3d_scene_slot;

typedef struct sacx_3d_player_slot
{
    k3d_player_handle handle;
    uint32_t scene_handle;
    uint32_t window_handle;
    uint8_t used;
} sacx_3d_player_slot;

typedef struct sacx_worker_slot
{
    uint8_t used;
    sacx_worker_fn fn;
    void *user;
    uint32_t job_id;
    sacx_task *owner;
} sacx_worker_slot;

typedef struct __attribute__((aligned(64))) sacx_async_image_save_state
{
    volatile int result;
    volatile uint32_t stage;
    volatile uint32_t encoded_size;
    uint32_t reserved[13];
} sacx_async_image_save_state;

typedef struct sacx_async_image_save_slot
{
    uint8_t used;
    uint8_t file_written;
    uint8_t write_open;
    uint8_t encoded_ready;
    uint32_t id;
    uint32_t job_id;
    uint32_t format;
    uint32_t quality;
    sacx_async_image_save_state state;
    uint8_t *encoded_data;
    uint32_t write_offset;
    uint64_t encoded_pages;
    KFile write_file;
    kimg image;
    char path[256];
} sacx_async_image_save_slot;

typedef struct __attribute__((aligned(64))) sacx_net_request_slot
{
    uint8_t used;
    uint8_t truncated;
    uint16_t reserved0;
    uint32_t id;
    uint32_t job_id;
    volatile uint32_t status;
    volatile uint32_t cancelled;
    uint32_t capacity;
    uint32_t redirect_limit;
    uint32_t timeout_ms;
    uint32_t raw_size;
    uint32_t body_size;
    uint64_t pages;
    uint8_t *buffer;
    sacx_net_response_info info;
    char url[768];
    char method[8];
    char content_type[96];
    uint32_t request_body_size;
    uint8_t request_body[4096];
} sacx_net_request_slot;

typedef struct sacx_memory_slot
{
    uint8_t used;
    uint8_t reserved[7];
    uint64_t pages;
    void *ptr;
} sacx_memory_slot;

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
    uint32_t loaded_arch;
    uint8_t loaded_from_fat;

    sacx_entry_fn entry;
    sacx_update_fn update_fn;
    sacx_file_dialog_fn dialog_callback;
    void *dialog_user;
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
    char launch_arg_raw[256];
    char launch_arg_friendly[256];
    uint32_t launch_arg_image;

    sacx_file_slot files[SACX_MAX_TASK_FILES];
    sacx_dir_slot dirs[SACX_MAX_TASK_DIRS];
    sacx_window_slot windows[SACX_MAX_TASK_WINDOWS];
    sacx_button_slot buttons[SACX_MAX_TASK_BUTTONS];
    sacx_textbox_slot textboxes[SACX_MAX_TASK_TEXTBOXES];
    sacx_ui_view_slot ui_views[SACX_MAX_TASK_UI_VIEWS];
    sacx_ui_dropdown_slot ui_dropdowns[SACX_MAX_TASK_UI_DROPDOWNS];
    sacx_ui_radio_slot ui_radios[SACX_MAX_TASK_UI_RADIOS];
    sacx_ui_toggle_slot ui_toggles[SACX_MAX_TASK_UI_TOGGLES];
    sacx_gfx_slot gfx_objects[SACX_MAX_TASK_GFX_OBJECTS];
    sacx_image_slot images[SACX_MAX_TASK_IMAGES];
    sacx_3d_scene_slot scenes3d[SACX_MAX_TASK_3D_SCENES];
    sacx_3d_player_slot players3d[SACX_MAX_TASK_3D_PLAYERS];
    sacx_worker_slot workers[SACX_MAX_TASK_WORKERS];
    sacx_async_image_save_slot image_saves[SACX_MAX_TASK_ASYNC_IMAGE_SAVES];
    sacx_net_request_slot net_requests[SACX_MAX_TASK_NET_REQUESTS];
    sacx_memory_slot memory_allocs[SACX_MAX_TASK_MEMORY_ALLOCS];
} sacx_task;

static sacx_task G_tasks[SACX_MAX_TASKS];
/*
 * SACX callbacks may run on the scheduler core or on a kwork core.  A single
 * current-task pointer made one app's API calls depend on which callback
 * happened to be executing on another CPU.  Keep the execution context local
 * to the logical CPU instead.  This isolates task bookkeeping; it does not
 * make global device drivers safe to call from arbitrary workers.
 */
static sacx_task *G_current_tasks[SMP_MAX_CORES];
static sacx_task **sacx_current_task_slot(void)
{
    uint32_t core = smp_current_logical_id();
    if (core >= SMP_MAX_CORES)
        core = 0u;
    return &G_current_tasks[core];
}
#define G_current_task (*sacx_current_task_slot())
static uint32_t G_next_task_id = 1u;
static uint32_t G_rr_cursor = 0u;
static const kfont *G_runtime_font = 0;

static kwindow_handle sacx_task_busy_window(sacx_task *task);
static void sacx_task_clean_worker_memory(sacx_task *task);

static void sacx_sync_executable_range(const void *base, uint32_t size)
{
    const uint8_t *bytes = (const uint8_t *)base;
    while (size)
    {
        uint32_t chunk = size > 65536u ? 65536u : size;
        asm_sync_executable_range(bytes, (uint64_t)chunk);
        bytes += chunk;
        size -= chunk;
        kbusy_pump();
    }
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

static int sacx_task_worker_active(const sacx_task *task)
{
    if (!task)
        return 0;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_WORKERS; ++i)
    {
        if (!task->workers[i].used)
            continue;
        uint32_t status = kwork_status(task->workers[i].job_id);
        if (status == KWORK_STATUS_QUEUED || status == KWORK_STATUS_RUNNING)
            return 1;
    }
    for (uint32_t i = 0u; i < SACX_MAX_TASK_ASYNC_IMAGE_SAVES; ++i)
    {
        if (!task->image_saves[i].used)
            continue;
        uint32_t status = kwork_status(task->image_saves[i].job_id);
        if (status == KWORK_STATUS_QUEUED || status == KWORK_STATUS_RUNNING)
            return 1;
    }
    for (uint32_t i = 0u; i < SACX_MAX_TASK_NET_REQUESTS; ++i)
    {
        if (!task->net_requests[i].used)
            continue;
        uint32_t status = kwork_status(task->net_requests[i].job_id);
        if (status == KWORK_STATUS_QUEUED || status == KWORK_STATUS_RUNNING)
            return 1;
    }
    return 0;
}

static sacx_worker_slot *sacx_task_alloc_worker_slot(sacx_task *task)
{
    if (!task)
        return 0;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_WORKERS; ++i)
    {
        uint32_t status = task->workers[i].used ? kwork_status(task->workers[i].job_id) : KWORK_STATUS_EMPTY;
        if (!task->workers[i].used ||
            status == KWORK_STATUS_EMPTY ||
            status == KWORK_STATUS_DONE ||
            status == KWORK_STATUS_FAILED)
        {
            task->workers[i] = (sacx_worker_slot){0};
            task->workers[i].used = 1u;
            task->workers[i].owner = task;
            return &task->workers[i];
        }
    }
    return 0;
}

static void sacx_worker_thunk(void *ctx)
{
    sacx_worker_slot *slot = (sacx_worker_slot *)ctx;
    sacx_task *task = slot ? slot->owner : 0;
    sacx_task *saved;
    if (!slot || !task || !slot->fn)
        return;
    if (task->state == SACX_TASK_UNUSED)
        return;
    if (!sacx_ptr_in_image(task, (const void *)slot->fn))
        return;
    saved = G_current_task;
    G_current_task = task;
    slot->fn(slot->user);
    G_current_task = saved;
    sacx_task_clean_worker_memory(task);
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

static uint8_t sacx_ascii_lower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

static int sacx_span_ieq(const uint8_t *text, uint32_t len, const char *wanted)
{
    uint32_t i = 0u;
    if (!text || !wanted)
        return 0;
    while (wanted[i])
    {
        if (i >= len || sacx_ascii_lower(text[i]) != sacx_ascii_lower((uint8_t)wanted[i]))
            return 0;
        ++i;
    }
    return i == len;
}

static uint32_t sacx_find_header_end(const uint8_t *data, uint32_t size)
{
    if (!data)
        return 0u;
    for (uint32_t i = 0u; i + 3u < size; ++i)
        if (data[i] == '\r' && data[i + 1u] == '\n' &&
            data[i + 2u] == '\r' && data[i + 3u] == '\n')
            return i + 4u;
    return 0u;
}

static uint32_t sacx_parse_hex(const uint8_t *text, uint32_t len, uint8_t *ok)
{
    uint32_t value = 0u;
    uint32_t digits = 0u;
    *ok = 0u;
    for (uint32_t i = 0u; i < len; ++i)
    {
        uint8_t c = text[i];
        uint32_t n;
        if (c == ';' || c == ' ' || c == '\t' || c == '\r')
            break;
        if (c >= '0' && c <= '9')
            n = c - '0';
        else if (c >= 'a' && c <= 'f')
            n = c - 'a' + 10u;
        else if (c >= 'A' && c <= 'F')
            n = c - 'A' + 10u;
        else
            return 0u;
        if (value > 0x0FFFFFFFu)
            return 0u;
        value = (value << 4) | n;
        ++digits;
    }
    *ok = digits ? 1u : 0u;
    return value;
}

static int sacx_url_resolve(const char *base, const char *relative, char *out, uint32_t cap)
{
    uint32_t n = 0u;
    uint32_t authority_end = 0u;
    uint32_t base_end = 0u;
    if (!base || !relative || !out || cap < 16u)
        return -1;
    if ((strncmp(relative, "http://", 7u) == 0) || (strncmp(relative, "https://", 8u) == 0))
    {
        sacx_copy_trunc(out, cap, relative);
        return 0;
    }
    if (!relative[0])
    {
        sacx_copy_trunc(out, cap, base);
        return 0;
    }
    while (base[authority_end] && base[authority_end] != ':')
        ++authority_end;
    if (!base[authority_end] || base[authority_end + 1u] != '/' || base[authority_end + 2u] != '/')
        return -1;
    authority_end += 3u;
    while (base[authority_end] && base[authority_end] != '/')
        ++authority_end;
    if (relative[0] == '/' && relative[1u] == '/')
    {
        while (base[n] && base[n] != ':' && n + 1u < cap)
        {
            out[n] = base[n];
            ++n;
        }
        if (base[n] == ':' && n + 1u < cap)
            out[n++] = ':';
        for (uint32_t i = 0u; relative[i] && n + 1u < cap; ++i)
            out[n++] = relative[i];
        out[n] = 0;
        return n + 1u < cap ? 0 : -1;
    }
    if (relative[0] == '?' || relative[0] == '#')
    {
        base_end = (uint32_t)strlen(base);
        for (uint32_t i = authority_end; i < base_end; ++i)
            if (base[i] == '#' || (relative[0] == '?' && base[i] == '?'))
            {
                base_end = i;
                break;
            }
    }
    else if (relative[0] == '/')
        base_end = authority_end;
    else
    {
        base_end = (uint32_t)strlen(base);
        for (uint32_t i = authority_end; i < base_end; ++i)
            if (base[i] == '?' || base[i] == '#')
            {
                base_end = i;
                break;
            }
        while (base_end > authority_end && base[base_end - 1u] != '/')
            --base_end;
    }
    while (n < base_end && n + 1u < cap)
    {
        out[n] = base[n];
        ++n;
    }
    for (uint32_t i = 0u; relative[i] && n + 1u < cap; ++i)
        out[n++] = relative[i];
    out[n] = 0;
    if (n + 1u >= cap)
        return -1;

    /* Remove path dot-segments without touching the scheme/authority. */
    for (uint32_t i = authority_end; out[i] && out[i] != '?' && out[i] != '#';)
    {
        if (out[i] == '/' && out[i + 1u] == '.' && out[i + 2u] == '/')
        {
            memmove(out + i, out + i + 2u, strlen(out + i + 2u) + 1u);
            continue;
        }
        if (out[i] == '/' && out[i + 1u] == '.' && out[i + 2u] == '.' &&
            (out[i + 3u] == '/' || out[i + 3u] == 0 || out[i + 3u] == '?' || out[i + 3u] == '#'))
        {
            uint32_t previous = i;
            uint32_t source = i + 3u + (out[i + 3u] == '/' ? 1u : 0u);
            while (previous > authority_end && out[previous - 1u] != '/')
                --previous;
            memmove(out + previous, out + source, strlen(out + source) + 1u);
            i = previous;
            continue;
        }
        ++i;
    }
    return 0;
}

static int sacx_net_parse_response(sacx_net_request_slot *slot, char *location, uint32_t location_cap)
{
    uint32_t header_end;
    uint32_t line = 0u;
    uint32_t content_length = 0u;
    uint8_t chunked = 0u;
    uint8_t has_content_length = 0u;
    if (!slot || !slot->buffer || !slot->raw_size)
        return -1;
    location[0] = 0;
    slot->info.content_type[0] = 0;
    header_end = sacx_find_header_end(slot->buffer, slot->raw_size);
    if (!header_end || header_end < 12u)
        return -1;
    if (slot->buffer[0] != 'H' || slot->buffer[1] != 'T' || slot->buffer[2] != 'T' || slot->buffer[3] != 'P')
        return -1;
    while (line < header_end && slot->buffer[line] != ' ')
        ++line;
    if (line + 3u >= header_end)
        return -1;
    if (slot->buffer[line + 1u] < '0' || slot->buffer[line + 1u] > '9' ||
        slot->buffer[line + 2u] < '0' || slot->buffer[line + 2u] > '9' ||
        slot->buffer[line + 3u] < '0' || slot->buffer[line + 3u] > '9')
        return -1;
    slot->info.http_status = (uint32_t)(slot->buffer[line + 1u] - '0') * 100u +
                             (uint32_t)(slot->buffer[line + 2u] - '0') * 10u +
                             (uint32_t)(slot->buffer[line + 3u] - '0');
    while (line + 1u < header_end && !(slot->buffer[line] == '\r' && slot->buffer[line + 1u] == '\n'))
        ++line;
    line += 2u;
    while (line + 2u < header_end)
    {
        uint32_t name = line;
        uint32_t colon;
        uint32_t value;
        uint32_t end;
        if (slot->buffer[line] == '\r' && slot->buffer[line + 1u] == '\n')
            break;
        colon = line;
        while (colon < header_end && slot->buffer[colon] != ':' && slot->buffer[colon] != '\r')
            ++colon;
        if (colon >= header_end || slot->buffer[colon] != ':')
            return -1;
        value = colon + 1u;
        while (value < header_end && (slot->buffer[value] == ' ' || slot->buffer[value] == '\t'))
            ++value;
        end = value;
        while (end + 1u < header_end && !(slot->buffer[end] == '\r' && slot->buffer[end + 1u] == '\n'))
            ++end;
        if (sacx_span_ieq(slot->buffer + name, colon - name, "content-type"))
        {
            uint32_t n = 0u;
            while (value < end && slot->buffer[value] != ';' && n + 1u < sizeof(slot->info.content_type))
                slot->info.content_type[n++] = (char)slot->buffer[value++];
            slot->info.content_type[n] = 0;
        }
        else if (sacx_span_ieq(slot->buffer + name, colon - name, "transfer-encoding"))
        {
            for (uint32_t i = value; i + 6u < end; ++i)
                if (sacx_span_ieq(slot->buffer + i, 7u, "chunked"))
                    chunked = 1u;
        }
        else if (sacx_span_ieq(slot->buffer + name, colon - name, "location"))
        {
            uint32_t n = 0u;
            while (value < end && n + 1u < location_cap)
                location[n++] = (char)slot->buffer[value++];
            location[n] = 0;
        }
        else if (sacx_span_ieq(slot->buffer + name, colon - name, "content-length"))
        {
            uint32_t parsed = 0u;
            if (value == end)
                return -1;
            for (uint32_t i = value; i < end; ++i)
            {
                uint32_t digit;
                if (slot->buffer[i] < '0' || slot->buffer[i] > '9')
                    return -1;
                digit = (uint32_t)(slot->buffer[i] - '0');
                if (parsed > (0xFFFFFFFFu - digit) / 10u)
                    return -1;
                parsed = parsed * 10u + digit;
            }
            content_length = parsed;
            has_content_length = 1u;
        }
        line = end + 2u;
    }

    if (chunked)
    {
        uint32_t src = header_end;
        uint32_t dst = 0u;
        while (src < slot->raw_size)
        {
            uint32_t line_end = src;
            uint32_t chunk;
            uint8_t ok;
            while (line_end + 1u < slot->raw_size &&
                   !(slot->buffer[line_end] == '\r' && slot->buffer[line_end + 1u] == '\n'))
                ++line_end;
            if (line_end + 1u >= slot->raw_size)
                return -1;
            chunk = sacx_parse_hex(slot->buffer + src, line_end - src, &ok);
            if (!ok)
                return -1;
            src = line_end + 2u;
            if (!chunk)
                break;
            if (chunk > slot->raw_size - src || dst > slot->capacity ||
                chunk > slot->capacity - dst)
                return -1;
            memmove(slot->buffer + dst, slot->buffer + src, chunk);
            dst += chunk;
            src += chunk;
            if (src + 1u >= slot->raw_size || slot->buffer[src] != '\r' || slot->buffer[src + 1u] != '\n')
                return -1;
            src += 2u;
        }
        slot->body_size = dst;
    }
    else
    {
        slot->body_size = slot->raw_size - header_end;
        if (has_content_length)
        {
            if (slot->body_size < content_length)
                return -1;
            slot->body_size = content_length;
        }
        memmove(slot->buffer, slot->buffer + header_end, slot->body_size);
    }
    slot->info.body_size = slot->body_size;
    slot->info.truncated = slot->truncated;
    return 0;
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
        if ((i & 0x3FFFu) == 0x3FFFu)
            kbusy_pump();
    }

    return ~crc;
}

static void sacx_memset_cooperative(void *dst, uint8_t value, uint32_t size)
{
    uint8_t *bytes = (uint8_t *)dst;
    while (size)
    {
        uint32_t chunk = size > 65536u ? 65536u : size;
        memset(bytes, value, chunk);
        bytes += chunk;
        size -= chunk;
        kbusy_pump();
    }
}

static void sacx_memcpy_cooperative(void *dst, const void *src, uint32_t size)
{
    uint8_t *out = (uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    while (size)
    {
        uint32_t chunk = size > 65536u ? 65536u : size;
        memcpy(out, in, chunk);
        out += chunk;
        in += chunk;
        size -= chunk;
        kbusy_pump();
    }
}

static int sacx_range_ok(uint32_t off, uint32_t len, uint32_t total)
{
    uint64_t end = (uint64_t)off + (uint64_t)len;
    return end <= (uint64_t)total;
}

static int sacx_table_ok(uint32_t off, uint32_t count, uint32_t elem_size, uint32_t total)
{
    uint64_t len = (uint64_t)count * (uint64_t)elem_size;
    if (len > 0xFFFFFFFFull)
        return 0;
    return sacx_range_ok(off, (uint32_t)len, total);
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
    "gfx_obj_get_rect",
    "gfx_obj_set_rotation_deg",
    "gfx_obj_rotation_deg",
    "gfx_obj_set_rotation_pivot",
    "gfx_obj_clear_rotation_pivot",
    "gfx_obj_set_circle",
    "gfx_text_set",
    "gfx_text_set_align",
    "gfx_text_set_spacing",
    "gfx_text_set_scale",
    "gfx_text_set_pos",
    "gfx_image_set_size",
    "gfx_image_set_pos",
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
    "app_arg_raw_path",
    "app_arg_friendly_path",
    "dialog_open_file",
    "dialog_active",
    "window_focused",
    "k3d_scene_create",
    "k3d_scene_destroy",
    "k3d_scene_render",
    "k3d_scene_resize",
    "k3d_scene_root_obj",
    "k3d_scene_set_camera",
    "k3d_scene_get_camera",
    "k3d_scene_set_ambient",
    "k3d_scene_set_directional_light",
    "k3d_scene_clear_point_lights",
    "k3d_scene_add_point_light",
    "k3d_scene_set_point_light",
    "k3d_scene_set_fog",
    "k3d_scene_new_cube",
    "k3d_scene_load_obj",
    "k3d_scene_add_image_surface",
    "k3d_scene_add_text_surface",
    "k3d_instance_set_pos",
    "k3d_instance_set_rotation",
    "k3d_instance_set_scale",
    "k3d_instance_set_visible",
    "k3d_instance_set_casts_shadow",
    "k3d_player_create",
    "k3d_player_destroy",
    "k3d_player_set_free_mode",
    "k3d_player_set_camera",
    "k3d_player_get_camera",
    "k3d_player_clear_colliders",
    "k3d_player_add_collider",
    "k3d_player_update",
    "k3d_scene_apply_default_world",
    "k3d_scene_add_room",
    "k3d_scene_add_obstacle_cube",
    "img_create",
    "img_clone",
    "img_pixels",
    "img_touch",
    "img_save",
    "img_save_async",
    "img_save_status",
    "img_save_release",
    "img_draw_text",
    "img_clipboard_set",
    "img_clipboard_get",
    "app_arg_image",
    "dialog_save_file",
    "window_set_close_deferred",
    "window_close_requested",
    "window_close_accept",
    "window_close_cancel",
    "textbox_select",
    "textbox_selection",
    "textbox_copy_selection",
    "textbox_cut_selection",
    "textbox_paste",
    "textbox_undo",
    "textbox_redo",
    "textbox_set_max_len",
    "textbox_max_len",
    "window_set_modal_child",
    "window_clear_modal_child",
    "window_has_active_modal",
    "window_center_on_parent",
    "dialog_open_file_for_window",
    "dialog_save_file_for_window",
    "ui_view_create_rect",
    "ui_window_view_create",
    "ui_view_destroy",
    "ui_view_root",
    "ui_view_add_obj",
    "ui_view_set_state",
    "ui_view_state",
    "ui_view_set_visible",
    "ui_view_set_layout",
    "ui_view_apply_layout",
    "ui_view_set_bounds",
    "ui_dropdown_create",
    "ui_dropdown_destroy",
    "ui_dropdown_root",
    "ui_dropdown_selected",
    "ui_dropdown_set_selected",
    "ui_dropdown_set_enabled",
    "ui_radio_create",
    "ui_radio_destroy",
    "ui_radio_root",
    "ui_radio_selected",
    "ui_radio_set_selected",
    "ui_radio_set_enabled",
    "ui_toggle_create",
    "ui_toggle_destroy",
    "ui_toggle_root",
    "ui_toggle_checked",
    "ui_toggle_set_checked",
    "ui_toggle_set_enabled",
    "work_submit",
    "work_status",
    "work_wait",
    "work_cancel",
    "net_request_start",
    "net_request_status",
    "net_response_info",
    "net_response_read",
    "net_request_cancel",
    "net_request_release",
    "img_load_memory",
    "mem_alloc",
    "mem_free",
    "net_request_start_ex",
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

static k3d_vec3 sacx_to_k3d_vec3(sacx3d_vec3 v)
{
    return (k3d_vec3){v.x, v.y, v.z};
}

static void sacx_to_k3d_camera(const sacx3d_camera *in, k3d_camera *out)
{
    if (!in || !out)
        return;
    out->pos = sacx_to_k3d_vec3(in->pos);
    out->yaw_deg = in->yaw_deg;
    out->pitch_deg = in->pitch_deg;
    out->roll_deg = in->roll_deg;
    out->fov_deg = in->fov_deg;
    out->near_z = in->near_z;
    out->far_z = in->far_z;
}

static void k3d_to_sacx_camera(const k3d_camera *in, sacx3d_camera *out)
{
    if (!in || !out)
        return;
    out->pos = (sacx3d_vec3){in->pos.x, in->pos.y, in->pos.z};
    out->yaw_deg = in->yaw_deg;
    out->pitch_deg = in->pitch_deg;
    out->roll_deg = in->roll_deg;
    out->fov_deg = in->fov_deg;
    out->near_z = in->near_z;
    out->far_z = in->far_z;
}

static void sacx_to_k3d_fog(const sacx3d_fog *in, k3d_fog *out)
{
    if (!in || !out)
        return;
    out->enabled = in->enabled ? 1u : 0u;
    out->color = sacx_to_kcolor(in->color);
    out->start = in->start;
    out->end = in->end;
}

static void sacx_to_k3d_point_light(const sacx3d_point_light *in, k3d_point_light *out)
{
    if (!in || !out)
        return;
    out->pos = sacx_to_k3d_vec3(in->pos);
    out->color = sacx_to_kcolor(in->color);
    out->radius = in->radius;
    out->intensity = in->intensity;
    out->enabled = in->enabled ? 1u : 0u;
}

static void sacx_to_k3d_box(const sacx3d_box *in, k3d_box *out)
{
    if (!in || !out)
        return;
    out->min_x = in->min_x;
    out->min_y = in->min_y;
    out->min_z = in->min_z;
    out->max_x = in->max_x;
    out->max_y = in->max_y;
    out->max_z = in->max_z;
}

static void sacx_to_k3d_player_desc(const sacx3d_player_desc *in, k3d_player_desc *out)
{
    if (!in || !out)
        return;
    sacx_to_k3d_camera(&in->camera, &out->camera);
    out->walk_speed = in->walk_speed;
    out->free_speed = in->free_speed;
    out->mouse_sensitivity = in->mouse_sensitivity;
    out->radius = in->radius;
    out->eye_height = in->eye_height;
    out->free_mode = in->free_mode ? 1u : 0u;
    out->drag_to_look = in->drag_to_look ? 1u : 0u;
}

static void sacx_to_k3d_room_desc(const sacx3d_room_desc *in, k3d_room_desc *out)
{
    if (!in || !out)
        return;
    out->center_x = in->center_x;
    out->floor_y = in->floor_y;
    out->center_z = in->center_z;
    out->w = in->w;
    out->h = in->h;
    out->d = in->d;
    out->wall_thickness = in->wall_thickness;
    out->floor_color = sacx_to_kcolor(in->floor_color);
    out->ceiling_color = sacx_to_kcolor(in->ceiling_color);
    out->left_wall_color = sacx_to_kcolor(in->left_wall_color);
    out->right_wall_color = sacx_to_kcolor(in->right_wall_color);
    out->front_wall_color = sacx_to_kcolor(in->front_wall_color);
    out->back_wall_color = sacx_to_kcolor(in->back_wall_color);
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

static int32_t sacx_ui_i32(int32_t px)
{
    return kwindow_ui_scale_i32(px);
}

static uint32_t sacx_ui_u32(uint32_t px)
{
    return kwindow_ui_scale_u32(px);
}

static int32_t sacx_ui_unscale_i32(int32_t px)
{
    uint32_t fp = kwindow_ui_scale_fp();
    int64_t scaled;
    if (fp == 0u)
        return px;
    scaled = (int64_t)px * 1024ll;
    if (scaled >= 0)
        scaled += (int64_t)fp / 2ll;
    else
        scaled -= (int64_t)fp / 2ll;
    return (int32_t)(scaled / (int64_t)fp);
}

static uint32_t sacx_ui_unscale_u32(uint32_t px)
{
    uint32_t fp = kwindow_ui_scale_fp();
    uint64_t scaled;
    if (fp == 0u)
        return px;
    scaled = (uint64_t)px * 1024ull + ((uint64_t)fp / 2ull);
    return (uint32_t)(scaled / (uint64_t)fp);
}

static uint32_t sacx_ui_text(uint32_t scale)
{
    return kwindow_ui_text_scale(scale ? scale : 1u);
}

static uint32_t sacx_ui_percent(uint32_t pct)
{
    uint64_t scaled = ((uint64_t)pct * (uint64_t)kwindow_ui_scale_fp() + 512ull) / 1024ull;
    if (pct && !scaled)
        scaled = 1ull;
    if (scaled > 0xFFFFFFFFull)
        scaled = 0xFFFFFFFFull;
    return (uint32_t)scaled;
}

static int32_t sacx_ui_spacing(int32_t px)
{
    return px ? kwindow_ui_scale_i32(px) : 0;
}

static void sacx_button_style_to_native(const sacx_button_style *in, kbutton_style *out)
{
    if (!out)
        return;

    *out = kbutton_style_default();
    if (in)
    {
        out->fill = sacx_to_kcolor(in->fill);
        out->hover_fill = sacx_to_kcolor(in->hover_fill);
        out->pressed_fill = sacx_to_kcolor(in->pressed_fill);
        out->outline = sacx_to_kcolor(in->outline);
        out->alpha = in->alpha;
        out->outline_alpha = in->outline_alpha;
        out->outline_width = in->outline_width;
    }

    out->outline_width = (uint16_t)sacx_ui_u32(out->outline_width);
    if (!out->outline_width)
        out->outline_width = 1u;
}

static void sacx_textbox_style_to_native(const sacx_textbox_style *in, ktextbox_style *out)
{
    if (!out)
        return;

    *out = ktextbox_style_default();
    if (in)
    {
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

    out->outline_width = (uint16_t)sacx_ui_u32(out->outline_width);
    out->padding_x = (uint16_t)sacx_ui_u32(out->padding_x);
    out->padding_y = (uint16_t)sacx_ui_u32(out->padding_y);
    out->text_scale = sacx_ui_text(out->text_scale ? out->text_scale : 1u);
    if (!out->outline_width)
        out->outline_width = 1u;
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

static void sacx_gfx_mark_owned(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_GFX_OBJECTS)
        return;
    if (!task->gfx_objects[handle - 1u].used)
        return;
    task->gfx_objects[handle - 1u].destroy_on_finish = 1u;
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
        task->gfx_objects[i].destroy_on_finish = 0u;
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

static sacx_ui_view_slot *sacx_ui_view_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_UI_VIEWS)
        return 0;
    if (!task->ui_views[handle - 1u].used)
        return 0;
    return &task->ui_views[handle - 1u];
}

static sacx_ui_dropdown_slot *sacx_ui_dropdown_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_UI_DROPDOWNS)
        return 0;
    if (!task->ui_dropdowns[handle - 1u].used)
        return 0;
    return &task->ui_dropdowns[handle - 1u];
}

static sacx_ui_radio_slot *sacx_ui_radio_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_UI_RADIOS)
        return 0;
    if (!task->ui_radios[handle - 1u].used)
        return 0;
    return &task->ui_radios[handle - 1u];
}

static sacx_ui_toggle_slot *sacx_ui_toggle_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_UI_TOGGLES)
        return 0;
    if (!task->ui_toggles[handle - 1u].used)
        return 0;
    return &task->ui_toggles[handle - 1u];
}

static sacx_image_slot *sacx_image_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_IMAGES)
        return 0;
    if (!task->images[handle - 1u].used)
        return 0;
    return &task->images[handle - 1u];
}

static sacx_3d_scene_slot *sacx_3d_scene_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_3D_SCENES)
        return 0;
    if (!task->scenes3d[handle - 1u].used)
        return 0;
    return &task->scenes3d[handle - 1u];
}

static sacx_3d_player_slot *sacx_3d_player_from_handle(sacx_task *task, uint32_t handle)
{
    if (!task || handle == 0u || handle > SACX_MAX_TASK_3D_PLAYERS)
        return 0;
    if (!task->players3d[handle - 1u].used)
        return 0;
    return &task->players3d[handle - 1u];
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

static void sacx_ui_dropdown_change_trampoline(uint32_t handle, int32_t value, void *user)
{
    sacx_ui_dropdown_slot *slot = (sacx_ui_dropdown_slot *)user;
    sacx_task *saved = G_current_task;
    uint32_t app_handle = 0u;

    (void)handle;

    if (!slot || !slot->used || !slot->owner || !slot->callback)
        return;

    app_handle = (uint32_t)((slot - slot->owner->ui_dropdowns) + 1u);
    G_current_task = slot->owner;
    slot->callback(app_handle, value, slot->callback_user);
    G_current_task = saved;
}

static void sacx_ui_radio_change_trampoline(uint32_t handle, int32_t value, void *user)
{
    sacx_ui_radio_slot *slot = (sacx_ui_radio_slot *)user;
    sacx_task *saved = G_current_task;
    uint32_t app_handle = 0u;

    (void)handle;

    if (!slot || !slot->used || !slot->owner || !slot->callback)
        return;

    app_handle = (uint32_t)((slot - slot->owner->ui_radios) + 1u);
    G_current_task = slot->owner;
    slot->callback(app_handle, value, slot->callback_user);
    G_current_task = saved;
}

static void sacx_ui_toggle_change_trampoline(uint32_t handle, int32_t value, void *user)
{
    sacx_ui_toggle_slot *slot = (sacx_ui_toggle_slot *)user;
    sacx_task *saved = G_current_task;
    uint32_t app_handle = 0u;

    (void)handle;

    if (!slot || !slot->used || !slot->owner || !slot->callback)
        return;

    app_handle = (uint32_t)((slot - slot->owner->ui_toggles) + 1u);
    G_current_task = slot->owner;
    slot->callback(app_handle, value, slot->callback_user);
    G_current_task = saved;
}

static void sacx_file_dialog_trampoline(int accepted, const char *raw_path, const char *friendly_path, void *user)
{
    sacx_task *task = (sacx_task *)user;
    sacx_task *saved = G_current_task;
    sacx_file_dialog_fn callback = 0;
    void *callback_user = 0;

    if (!task || task->state == SACX_TASK_UNUSED || !task->dialog_callback)
        return;
    if (!sacx_ptr_in_image(task, (const void *)task->dialog_callback))
    {
        task->dialog_callback = 0;
        task->dialog_user = 0;
        return;
    }

    callback = task->dialog_callback;
    callback_user = task->dialog_user;
    task->dialog_callback = 0;
    task->dialog_user = 0;

    G_current_task = task;
    callback(accepted ? 1 : 0, raw_path ? raw_path : "", friendly_path ? friendly_path : "", callback_user);
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

static void sacx_task_destroy_ui_widgets(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_DROPDOWNS; ++i)
    {
        if (!task->ui_dropdowns[i].used)
            continue;
        (void)kui_dropdown_destroy(task->ui_dropdowns[i].handle);
        task->ui_dropdowns[i].used = 0u;
        task->ui_dropdowns[i].handle.idx = -1;
        task->ui_dropdowns[i].callback = 0;
        task->ui_dropdowns[i].callback_user = 0;
        task->ui_dropdowns[i].owner = 0;
    }

    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_RADIOS; ++i)
    {
        if (!task->ui_radios[i].used)
            continue;
        (void)kui_radio_destroy(task->ui_radios[i].handle);
        task->ui_radios[i].used = 0u;
        task->ui_radios[i].handle.idx = -1;
        task->ui_radios[i].callback = 0;
        task->ui_radios[i].callback_user = 0;
        task->ui_radios[i].owner = 0;
    }

    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_TOGGLES; ++i)
    {
        if (!task->ui_toggles[i].used)
            continue;
        (void)kui_toggle_destroy(task->ui_toggles[i].handle);
        task->ui_toggles[i].used = 0u;
        task->ui_toggles[i].handle.idx = -1;
        task->ui_toggles[i].callback = 0;
        task->ui_toggles[i].callback_user = 0;
        task->ui_toggles[i].owner = 0;
    }
}

static void sacx_task_destroy_ui_views(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_VIEWS; ++i)
    {
        if (!task->ui_views[i].used)
            continue;
        (void)kui_view_destroy(task->ui_views[i].handle);
        task->ui_views[i].used = 0u;
        task->ui_views[i].handle.idx = -1;
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
        if (task->gfx_objects[i].destroy_on_finish)
            (void)kgfx_obj_destroy(task->gfx_objects[i].handle);
        task->gfx_objects[i].used = 0u;
        task->gfx_objects[i].destroy_on_finish = 0u;
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

static void sacx_task_clean_worker_memory(sacx_task *task)
{
    if (!task)
        return;
    if (task->arena && task->arena_size)
        asm_dma_clean_range(task->arena, task->arena_size);
    for (uint32_t i = 0u; i < SACX_MAX_TASK_IMAGES; ++i)
    {
        uint64_t bytes = 0u;
        if (!task->images[i].used || !task->images[i].image.px ||
            !task->images[i].image.w || !task->images[i].image.h)
            continue;
        bytes = (uint64_t)task->images[i].image.w * (uint64_t)task->images[i].image.h * 4ull;
        asm_dma_clean_range(task->images[i].image.px, bytes);
    }
}

static void sacx_task_invalidate_image_memory(sacx_task *task)
{
    if (!task)
        return;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_IMAGES; ++i)
    {
        uint64_t bytes = 0u;
        if (!task->images[i].used || !task->images[i].image.px ||
            !task->images[i].image.w || !task->images[i].image.h)
            continue;
        bytes = (uint64_t)task->images[i].image.w * (uint64_t)task->images[i].image.h * 4ull;
        asm_dma_invalidate_range(task->images[i].image.px, bytes);
    }
}

static void sacx_async_image_save_release_slot(sacx_async_image_save_slot *slot)
{
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!slot || !slot->used)
        return;
    if (slot->write_open)
    {
        kfile_close(&slot->write_file);
        slot->write_open = 0u;
    }
    if (slot->image.px && slot->image.w && slot->image.h)
    {
        bytes = (uint64_t)slot->image.w * (uint64_t)slot->image.h * 4ull;
        pages = (bytes + 4095ull) >> 12;
        if (pages)
            pmem_free_pages(slot->image.px, pages);
    }
    kimg_encode_free(slot->encoded_data, slot->encoded_pages);
    *slot = (sacx_async_image_save_slot){0};
}

static void sacx_task_destroy_async_image_saves(sacx_task *task)
{
    if (!task)
        return;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_ASYNC_IMAGE_SAVES; ++i)
        sacx_async_image_save_release_slot(&task->image_saves[i]);
}

static void sacx_task_destroy_net_requests(sacx_task *task)
{
    if (!task)
        return;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_NET_REQUESTS; ++i)
    {
        sacx_net_request_slot *slot = &task->net_requests[i];
        uint32_t status;
        if (!slot->used)
            continue;
        status = kwork_status(slot->job_id);
        if (status == KWORK_STATUS_QUEUED || status == KWORK_STATUS_RUNNING)
        {
            __atomic_store_n(&slot->cancelled, 1u, __ATOMIC_RELEASE);
            asm_dma_clean_range((const void *)&slot->cancelled, sizeof(slot->cancelled));
            (void)kwork_cancel(slot->job_id);
            continue;
        }
        if (slot->buffer && slot->pages)
            pmem_free_pages(slot->buffer, slot->pages);
        memset(slot, 0, sizeof(*slot));
    }
}

static void sacx_task_destroy_memory(sacx_task *task)
{
    if (!task)
        return;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_MEMORY_ALLOCS; ++i)
    {
        sacx_memory_slot *slot = &task->memory_allocs[i];
        if (slot->used && slot->ptr && slot->pages)
            pmem_free_pages(slot->ptr, slot->pages);
        *slot = (sacx_memory_slot){0};
    }
}

static void sacx_task_destroy_3d_scenes(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_3D_SCENES; ++i)
    {
        if (!task->scenes3d[i].used)
            continue;
        if (task->scenes3d[i].root_obj_handle > 0u &&
            task->scenes3d[i].root_obj_handle <= SACX_MAX_TASK_GFX_OBJECTS)
        {
            task->gfx_objects[task->scenes3d[i].root_obj_handle - 1u].used = 0u;
            task->gfx_objects[task->scenes3d[i].root_obj_handle - 1u].handle.idx = -1;
        }
        (void)k3d_scene_destroy(task->scenes3d[i].handle);
        task->scenes3d[i].used = 0u;
        task->scenes3d[i].handle.idx = -1;
        task->scenes3d[i].root_obj_handle = 0u;
    }
}

static void sacx_task_destroy_3d_players(sacx_task *task)
{
    if (!task)
        return;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_3D_PLAYERS; ++i)
    {
        if (!task->players3d[i].used)
            continue;
        (void)k3d_player_destroy(task->players3d[i].handle);
        task->players3d[i].used = 0u;
        task->players3d[i].handle.idx = -1;
        task->players3d[i].scene_handle = 0u;
        task->players3d[i].window_handle = 0u;
    }
}

static void sacx_task_finish(sacx_task *task, int32_t status, const char *message, uint32_t new_state)
{
    char status_text[16];
    int worker_active = 0;

    if (!task)
        return;

    worker_active = sacx_task_worker_active(task);

    sacx_task_close_files(task);
    sacx_task_close_dirs(task);
    sacx_task_destroy_3d_players(task);
    sacx_task_destroy_3d_scenes(task);
    sacx_task_destroy_gfx_objects(task);
    sacx_task_destroy_ui_widgets(task);
    sacx_task_destroy_ui_views(task);
    sacx_task_destroy_textboxes(task);
    sacx_task_destroy_buttons(task);
    sacx_task_destroy_windows(task);
    sacx_task_destroy_images(task);
    sacx_task_destroy_net_requests(task);
    sacx_task_destroy_memory(task);
    if (!worker_active)
        sacx_task_destroy_async_image_saves(task);

    if (task->arena && !worker_active)
    {
        pmem_free_executable_pages(task->arena, task->arena_size / 4096u);
        task->arena = 0;
    }

    if (!worker_active)
    {
        task->image_base = 0;
        task->image_size = 0u;
    }
    task->entry = 0;
    task->update_fn = 0;
    task->dialog_callback = 0;
    task->dialog_user = 0;
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
    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_VIEWS; ++i)
        task->ui_views[i].handle.idx = -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_DROPDOWNS; ++i)
        task->ui_dropdowns[i].handle.idx = -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_RADIOS; ++i)
        task->ui_radios[i].handle.idx = -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_TOGGLES; ++i)
        task->ui_toggles[i].handle.idx = -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_GFX_OBJECTS; ++i)
    {
        task->gfx_objects[i].handle.idx = -1;
        task->gfx_objects[i].destroy_on_finish = 0u;
    }
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
    uint32_t total_read = 0u;
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

    while (total_read < size)
    {
        uint32_t chunk = size - total_read;
        uint32_t read = 0u;
        if (chunk > 16384u)
            chunk = 16384u;
        if (kfile_read(&file, buf + total_read, chunk, &read) != 0 || read != chunk)
        {
            pmem_free_pages(buf, pages);
            kfile_close(&file);
            return -1;
        }
        total_read += read;
        kbusy_pump();
    }

    kfile_close(&file);
    *out_buf = buf;
    *out_size = size;
    return 0;
}

typedef struct sacx_selected_image
{
    const uint8_t *data;
    uint32_t size;
    uint32_t arch;
    uint8_t from_fat;
} sacx_selected_image;

static uint32_t sacx_runtime_arch(void)
{
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return SACX_ARCH_AA64;
#elif defined(__x86_64__) || defined(_M_X64)
    return SACX_ARCH_X64;
#else
    return SACX_ARCH_UNKNOWN;
#endif
}

static const char *sacx_arch_name(uint32_t arch)
{
    if (arch == SACX_ARCH_AA64)
        return "aa64";
    if (arch == SACX_ARCH_X64)
        return "x64";
    return "unknown";
}

static int sacx_select_fat_image(const uint8_t *file_data, uint32_t file_size, sacx_selected_image *out)
{
    const sacx_fat_header *fat = 0;
    const uint8_t *slice_bytes = 0;
    uint32_t runtime_arch = sacx_runtime_arch();

    if (!file_data || !out || file_size < sizeof(sacx_fat_header) || runtime_arch == SACX_ARCH_UNKNOWN)
    {
        terminal_warn("sacx: fat select precheck failed");
        return -1;
    }

    fat = (const sacx_fat_header *)file_data;
    if (fat->magic != SACX_MAGIC ||
        fat->version != SACX_FAT_VERSION ||
        fat->header_size < sizeof(sacx_fat_header) ||
        fat->slice_offset < fat->header_size ||
        fat->slice_count == 0u ||
        fat->slice_count > SACX_MAX_SLICES)
    {
        terminal_warn("sacx: fat header invalid");
        return -1;
    }

    if (!sacx_table_ok(fat->slice_offset, fat->slice_count, sizeof(sacx_slice), file_size))
    {
        terminal_warn("sacx: fat slice table invalid");
        return -1;
    }

    if (sacx_crc32_zero_range(file_data, file_size, (uint32_t)offsetof(sacx_fat_header, crc32), 4u) != fat->crc32)
    {
        terminal_warn("sacx: fat crc invalid");
        return -1;
    }

    slice_bytes = file_data + fat->slice_offset;
    for (uint32_t i = 0u; i < fat->slice_count; ++i)
    {
        sacx_slice slice;
        memcpy(&slice, slice_bytes + (uint64_t)i * sizeof(sacx_slice), sizeof(slice));

        if (slice.kind != SACX_SLICE_KIND_NATIVE)
            continue;
        if (slice.file_size < sizeof(sacx_header) || !sacx_range_ok(slice.file_offset, slice.file_size, file_size))
        {
            terminal_warn("sacx: fat slice range invalid");
            return -1;
        }
        if (slice.arch != runtime_arch)
            continue;

        out->data = file_data + slice.file_offset;
        out->size = slice.file_size;
        out->arch = slice.arch;
        out->from_fat = 1u;
        return 0;
    }

    terminal_print_inline("sacx: no slice for runtime arch=");
    terminal_print_inline_hex32(runtime_arch);
    terminal_print("");
    return -1;
}

static int sacx_select_image(const uint8_t *file_data, uint32_t file_size, sacx_selected_image *out)
{
    const sacx_header *hdr = 0;

    if (!file_data || !out || file_size < sizeof(sacx_header))
    {
        terminal_warn("sacx: file too small");
        return -1;
    }

    memset(out, 0, sizeof(*out));
    hdr = (const sacx_header *)file_data;
    if (hdr->magic != SACX_MAGIC)
    {
        terminal_warn("sacx: bad magic");
        return -1;
    }

    if (hdr->version == SACX_FAT_VERSION)
        return sacx_select_fat_image(file_data, file_size, out);

    if (hdr->version == SACX_VERSION)
    {
        /* Legacy v1 SACX files were produced as AArch64-only images. */
        if (sacx_runtime_arch() != SACX_ARCH_AA64)
        {
            terminal_warn("sacx: legacy aa64 app on non-aa64 runtime");
            return -1;
        }

        out->data = file_data;
        out->size = file_size;
        out->arch = SACX_ARCH_AA64;
        out->from_fat = 0u;
        return 0;
    }

    terminal_print_inline("sacx: unsupported version=");
    terminal_print_inline_hex32(hdr->version);
    terminal_print("");
    return -1;
}

static int sacx_load_image_payload(sacx_task *task, const uint8_t *file_data, uint32_t file_size, const char *friendly_path)
{
    const sacx_header *hdr = 0;
    const uint8_t *segment_bytes = 0;
    const uint8_t *reloc_bytes = 0;
    const uint8_t *import_bytes = 0;
    const char *strings = 0;
    const uint8_t *image_blob = 0;
    uint32_t image_blob_size = 0u;
    uint32_t arena_pages = 0u;

    if (!task || !file_data || file_size < sizeof(sacx_header))
    {
        terminal_warn("sacx: payload too small");
        return -1;
    }

    hdr = (const sacx_header *)file_data;
    if (hdr->magic != SACX_MAGIC ||
        hdr->version != SACX_VERSION ||
        hdr->header_size < sizeof(sacx_header))
    {
        terminal_warn("sacx: payload header invalid");
        return -1;
    }

    if (hdr->segment_count > SACX_MAX_SEGMENTS ||
        hdr->reloc_count > SACX_MAX_RELOCS ||
        hdr->import_count > SACX_MAX_IMPORTS)
    {
        terminal_warn("sacx: payload table count invalid");
        return -1;
    }

    if (!sacx_table_ok(hdr->segment_offset, hdr->segment_count, sizeof(sacx_segment), file_size) ||
        !sacx_table_ok(hdr->reloc_offset, hdr->reloc_count, sizeof(sacx_reloc), file_size) ||
        !sacx_table_ok(hdr->import_offset, hdr->import_count, sizeof(sacx_import), file_size) ||
        !sacx_range_ok(hdr->strings_offset, hdr->strings_size, file_size) ||
        !sacx_range_ok(hdr->image_offset, 0u, file_size))
    {
        terminal_warn("sacx: payload ranges invalid");
        return -1;
    }

    if (hdr->image_size == 0u || hdr->image_size > SACX_APP_ARENA_BYTES || hdr->entry_rva >= hdr->image_size)
    {
        terminal_warn("sacx: payload image size invalid");
        return -1;
    }

    if (sacx_crc32_zero_range(file_data, file_size, (uint32_t)offsetof(sacx_header, crc32), 4u) != hdr->crc32)
    {
        terminal_warn("sacx: payload crc invalid");
        return -1;
    }

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
        {
            terminal_print_inline("sacx: unknown import ");
            terminal_print(name ? name : "(bad)");
            return -1;
        }
    }

    arena_pages = (hdr->image_size + 4095u) / 4096u;
    task->arena = (uint8_t *)pmem_alloc_executable_pages(arena_pages);
    if (!task->arena)
    {
        terminal_print_inline("sacx: executable arena alloc failed pages=");
        terminal_print_inline_hex32(arena_pages);
        terminal_print("");
        if (k_bootinfo_ptr)
        {
            terminal_print_inline("sacx: exec pool base=");
            terminal_print_inline_hex64(k_bootinfo_ptr->sacx_exec_pool_base_phys);
            terminal_print_inline(" size=");
            terminal_print_inline_hex64(k_bootinfo_ptr->sacx_exec_pool_size_bytes);
            terminal_print("");
        }
        return -1;
    }
    task->arena_size = arena_pages * 4096u;
    sacx_memset_cooperative(task->arena, 0u, task->arena_size);
    task->image_base = task->arena;
    task->image_size = hdr->image_size;

    for (uint32_t i = 0u; i < hdr->segment_count; ++i)
    {
        sacx_segment seg;
        uint8_t *dst = 0;
        const uint8_t *src = 0;

        memcpy(&seg, segment_bytes + (uint64_t)i * sizeof(sacx_segment), sizeof(seg));

        if (seg.mem_size < seg.file_size)
        {
            terminal_warn("sacx: segment mem/file invalid");
            return -1;
        }
        if (!sacx_range_ok(seg.rva, seg.mem_size, hdr->image_size))
        {
            terminal_warn("sacx: segment memory range invalid");
            return -1;
        }
        if (!sacx_range_ok(seg.file_offset, seg.file_size, image_blob_size))
        {
            terminal_warn("sacx: segment file range invalid");
            return -1;
        }

        dst = task->image_base + seg.rva;
        src = image_blob + seg.file_offset;
        if (seg.file_size)
            sacx_memcpy_cooperative(dst, src, seg.file_size);
        if (seg.mem_size > seg.file_size)
            sacx_memset_cooperative(dst + seg.file_size, 0u, seg.mem_size - seg.file_size);
    }

    for (uint32_t i = 0u; i < hdr->reloc_count; ++i)
    {
        sacx_reloc rel;
        uint64_t *where = 0;

        memcpy(&rel, reloc_bytes + (uint64_t)i * sizeof(sacx_reloc), sizeof(rel));

        if (rel.type != SACX_RELOC_RELATIVE64)
        {
            terminal_warn("sacx: unsupported reloc type");
            return -1;
        }
        if (!sacx_range_ok(rel.target_rva, 8u, hdr->image_size))
        {
            terminal_warn("sacx: reloc target invalid");
            return -1;
        }
        if (rel.addend >= hdr->image_size)
        {
            terminal_warn("sacx: reloc addend invalid");
            return -1;
        }

        where = (uint64_t *)(task->image_base + rel.target_rva);
        *where = (uint64_t)(uintptr_t)(task->image_base + rel.addend);
    }

    sacx_sync_executable_range(task->image_base, task->image_size);
    task->entry = (sacx_entry_fn)(uintptr_t)(task->image_base + hdr->entry_rva);
    sacx_copy_trunc(task->friendly_path, sizeof(task->friendly_path), friendly_path ? friendly_path : "");
    return 0;
}

static int sacx_load_image(sacx_task *task, const uint8_t *file_data, uint32_t file_size, const char *friendly_path)
{
    sacx_selected_image selected;
    int rc = -1;

    if (sacx_select_image(file_data, file_size, &selected) != 0)
    {
        terminal_warn("sacx: image selection failed");
        return -1;
    }

    rc = sacx_load_image_payload(task, selected.data, selected.size, friendly_path);
    if (rc != 0)
        terminal_warn("sacx: payload load failed");
    if (rc == 0)
    {
        task->loaded_arch = selected.arch;
        task->loaded_from_fat = selected.from_fat;
    }
    return rc;
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

static int sacx_api_dialog_begin(file_explorer_dialog_mode mode,
                                 uint32_t owner_window_handle,
                                 const char *initial_dir,
                                 const char *suggested_name,
                                 sacx_file_dialog_fn on_result,
                                 void *user)
{
    kwindow_handle owner = {-1};

    if (!G_current_task || !on_result)
        return -1;
    if (!sacx_ptr_in_image(G_current_task, (const void *)on_result))
        return -1;
    if (file_explorer_dialog_active())
        return -1;
    if (owner_window_handle)
    {
        sacx_window_slot *slot = sacx_window_from_handle(G_current_task, owner_window_handle);
        if (!slot)
            return -1;
        owner = slot->handle;
    }

    G_current_task->dialog_callback = on_result;
    G_current_task->dialog_user = user;
    if (file_explorer_begin_dialog_for_window(mode, owner,
                                              (initial_dir && initial_dir[0]) ? initial_dir : "/",
                                              suggested_name,
                                              sacx_file_dialog_trampoline,
                                              G_current_task) != 0)
    {
        G_current_task->dialog_callback = 0;
        G_current_task->dialog_user = 0;
        return -1;
    }
    return 0;
}

static int sacx_api_dialog_open_file(const char *initial_dir,
                                     const char *suggested_name,
                                     sacx_file_dialog_fn on_result,
                                     void *user)
{
    return sacx_api_dialog_begin(FILE_EXPLORER_DIALOG_OPEN_FILE, 0u, initial_dir, suggested_name, on_result, user);
}

static int sacx_api_dialog_save_file(const char *initial_dir,
                                     const char *suggested_name,
                                     sacx_file_dialog_fn on_result,
                                     void *user)
{
    return sacx_api_dialog_begin(FILE_EXPLORER_DIALOG_SAVE_FILE, 0u, initial_dir, suggested_name, on_result, user);
}

static int sacx_api_dialog_open_file_for_window(uint32_t owner_window_handle,
                                                const char *initial_dir,
                                                const char *suggested_name,
                                                sacx_file_dialog_fn on_result,
                                                void *user)
{
    return sacx_api_dialog_begin(FILE_EXPLORER_DIALOG_OPEN_FILE, owner_window_handle,
                                 initial_dir, suggested_name, on_result, user);
}

static int sacx_api_dialog_save_file_for_window(uint32_t owner_window_handle,
                                                const char *initial_dir,
                                                const char *suggested_name,
                                                sacx_file_dialog_fn on_result,
                                                void *user)
{
    return sacx_api_dialog_begin(FILE_EXPLORER_DIALOG_SAVE_FILE, owner_window_handle,
                                 initial_dir, suggested_name, on_result, user);
}

static int sacx_api_dialog_active(void)
{
    return file_explorer_dialog_active();
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

static int sacx_api_work_submit(sacx_worker_fn fn, void *user, uint32_t *out_job_id)
{
    sacx_worker_slot *slot;
    uint32_t job_id = 0u;
    int rc;

    if (out_job_id)
        *out_job_id = 0u;
    if (!G_current_task || !fn || !out_job_id)
        return -1;
    if (!sacx_ptr_in_image(G_current_task, (const void *)fn))
        return -2;

    slot = sacx_task_alloc_worker_slot(G_current_task);
    if (!slot)
        return -3;
    slot->fn = fn;
    slot->user = user;

    sacx_task_clean_worker_memory(G_current_task);
    asm_dma_clean_range(slot, sizeof(*slot));
    rc = kwork_submit(sacx_worker_thunk, slot, KWORK_SUBMIT_REQUIRE_REMOTE, &job_id);
    if (rc != 0)
    {
        *slot = (sacx_worker_slot){0};
        return rc;
    }
    slot->job_id = job_id;
    *out_job_id = job_id;
    return 0;
}

static uint32_t sacx_api_work_status(uint32_t job_id)
{
    uint32_t status = kwork_status(job_id);
    if (G_current_task &&
        (status == KWORK_STATUS_DONE || status == KWORK_STATUS_FAILED))
    {
        if (G_current_task->arena && G_current_task->arena_size)
            asm_dma_invalidate_range(G_current_task->arena, G_current_task->arena_size);
        sacx_task_invalidate_image_memory(G_current_task);
    }
    return status;
}

static int sacx_api_work_wait(uint32_t job_id, uint64_t spin_limit)
{
    return kwork_wait(job_id, spin_limit);
}

static int sacx_api_work_cancel(uint32_t job_id)
{
    return kwork_cancel(job_id);
}

static sacx_net_request_slot *sacx_net_slot_from_id(sacx_task *task, uint32_t request_id)
{
    if (!task || !request_id || request_id > SACX_MAX_TASK_NET_REQUESTS)
        return 0;
    sacx_net_request_slot *slot = &task->net_requests[request_id - 1u];
    return slot->used ? slot : 0;
}

static void sacx_net_request_worker(void *ctx)
{
    sacx_net_request_slot *slot = (sacx_net_request_slot *)ctx;
    char current_url[768];
    char location[768];
    char next_url[768];
    uint32_t redirects = 0u;
    int rc = -1;
    if (!slot || !slot->used || !slot->buffer)
        return;
    sacx_copy_trunc(current_url, sizeof(current_url), slot->url);
    __atomic_store_n(&slot->status, SACX_NET_STATUS_LOADING, __ATOMIC_RELEASE);
    for (;;)
    {
        if (__atomic_load_n(&slot->cancelled, __ATOMIC_ACQUIRE))
        {
            __atomic_store_n(&slot->status, SACX_NET_STATUS_CANCELLED, __ATOMIC_RELEASE);
            asm_dma_clean_range(slot, sizeof(*slot));
            return;
        }
        slot->raw_size = 0u;
        slot->body_size = 0u;
        slot->truncated = 0u;
        memset(&slot->info, 0, sizeof(slot->info));
        knet_usb_set_worker_quiet(smp_current_logical_id() != 0u);
        rc = knet_usb_fetch_request(current_url, slot->method,
                                    slot->request_body, slot->request_body_size,
                                    slot->content_type, slot->buffer, slot->capacity,
                                    &slot->raw_size, &slot->truncated, &slot->cancelled,
                                    slot->timeout_ms);
        knet_usb_set_worker_quiet(0u);
        if (__atomic_load_n(&slot->cancelled, __ATOMIC_ACQUIRE))
        {
            __atomic_store_n(&slot->status, SACX_NET_STATUS_CANCELLED, __ATOMIC_RELEASE);
            asm_dma_clean_range(slot, sizeof(*slot));
            return;
        }
        if (rc != 0)
        {
            sacx_copy_trunc(slot->info.error, sizeof(slot->info.error),
                            rc == -4 ? "network request timed out" : "network request failed");
            slot->info.status = SACX_NET_STATUS_FAILED;
            __atomic_store_n(&slot->status, SACX_NET_STATUS_FAILED, __ATOMIC_RELEASE);
            asm_dma_clean_range(slot, sizeof(*slot));
            return;
        }
        if (sacx_net_parse_response(slot, location, sizeof(location)) != 0)
        {
            sacx_copy_trunc(slot->info.error, sizeof(slot->info.error), "malformed HTTP response");
            slot->info.status = SACX_NET_STATUS_FAILED;
            __atomic_store_n(&slot->status, SACX_NET_STATUS_FAILED, __ATOMIC_RELEASE);
            asm_dma_clean_range(slot, sizeof(*slot));
            return;
        }
        if ((slot->info.http_status == 301u || slot->info.http_status == 302u ||
             slot->info.http_status == 303u || slot->info.http_status == 307u ||
             slot->info.http_status == 308u) && location[0])
        {
            if (redirects++ >= slot->redirect_limit ||
                sacx_url_resolve(current_url, location, next_url, sizeof(next_url)) != 0)
            {
                sacx_copy_trunc(slot->info.error, sizeof(slot->info.error), "HTTP redirect limit or URL error");
                slot->info.status = SACX_NET_STATUS_FAILED;
                __atomic_store_n(&slot->status, SACX_NET_STATUS_FAILED, __ATOMIC_RELEASE);
                asm_dma_clean_range(slot, sizeof(*slot));
                return;
            }
            sacx_copy_trunc(current_url, sizeof(current_url), next_url);
            if ((slot->info.http_status == 301u || slot->info.http_status == 302u ||
                 slot->info.http_status == 303u) && strcmp(slot->method, "POST") == 0)
            {
                sacx_copy_trunc(slot->method, sizeof(slot->method), "GET");
                slot->request_body_size = 0u;
            }
            continue;
        }
        sacx_copy_trunc(slot->info.final_url, sizeof(slot->info.final_url), current_url);
        slot->info.tls_unverified = strncmp(current_url, "https://", 8u) == 0 ? 1u : 0u;
        slot->info.status = SACX_NET_STATUS_DONE;
        asm_dma_clean_range(slot->buffer, slot->body_size);
        __atomic_store_n(&slot->status, SACX_NET_STATUS_DONE, __ATOMIC_RELEASE);
        asm_dma_clean_range(slot, sizeof(*slot));
        return;
    }
}

static int sacx_api_net_request_start_ex(const sacx_net_request_desc_ex *desc, uint32_t *out_request_id)
{
    sacx_net_request_slot *slot = 0;
    uint32_t capacity;
    if (out_request_id)
        *out_request_id = 0u;
    if (!G_current_task || !desc || !desc->url || !desc->url[0] || !out_request_id ||
        desc->body_size > 4096u || (desc->body_size && !desc->body))
        return -1;
    for (uint32_t t = 0u; t < SACX_MAX_TASKS; ++t)
        for (uint32_t i = 0u; i < SACX_MAX_TASK_NET_REQUESTS; ++i)
        {
            uint32_t status = __atomic_load_n(&G_tasks[t].net_requests[i].status, __ATOMIC_ACQUIRE);
            if (G_tasks[t].net_requests[i].used &&
                (status == SACX_NET_STATUS_QUEUED || status == SACX_NET_STATUS_LOADING))
                return -2;
        }
    for (uint32_t i = 0u; i < SACX_MAX_TASK_NET_REQUESTS; ++i)
        if (!G_current_task->net_requests[i].used)
        {
            slot = &G_current_task->net_requests[i];
            memset(slot, 0, sizeof(*slot));
            slot->used = 1u;
            slot->id = i + 1u;
            break;
        }
    if (!slot)
        return -3;
    capacity = desc->max_response_bytes ? desc->max_response_bytes : SACX_NET_DEFAULT_BYTES;
    if (capacity < 4096u)
        capacity = 4096u;
    if (capacity > SACX_NET_MAX_BYTES)
        capacity = SACX_NET_MAX_BYTES;
    slot->pages = ((uint64_t)capacity + 4095ull) >> 12;
    slot->buffer = (uint8_t *)pmem_alloc_pages(slot->pages);
    if (!slot->buffer)
    {
        memset(slot, 0, sizeof(*slot));
        return -4;
    }
    slot->capacity = capacity;
    slot->redirect_limit = desc->redirect_limit > 10u ? 10u : desc->redirect_limit;
    if (!slot->redirect_limit)
        slot->redirect_limit = 5u;
    slot->timeout_ms = desc->timeout_ms;
    if (slot->timeout_ms < 1000u)
        slot->timeout_ms = 45000u;
    if (slot->timeout_ms > 120000u)
        slot->timeout_ms = 120000u;
    sacx_copy_trunc(slot->url, sizeof(slot->url), desc->url);
    sacx_copy_trunc(slot->method, sizeof(slot->method), desc->method && desc->method[0] ? desc->method : "GET");
    sacx_copy_trunc(slot->content_type, sizeof(slot->content_type),
                    desc->content_type && desc->content_type[0] ? desc->content_type : "application/x-www-form-urlencoded");
    slot->request_body_size = desc->body_size;
    if (desc->body_size)
        memcpy(slot->request_body, desc->body, desc->body_size);
    __atomic_store_n(&slot->status, SACX_NET_STATUS_QUEUED, __ATOMIC_RELEASE);
    asm_dma_clean_range(slot, sizeof(*slot));

    /*
     * xHCI/USB Ethernet is currently owned by core 0.  Running it through a
     * remote worker is not safe until the driver is genuinely SMP-safe.
     */
    slot->job_id = 0u;
    sacx_net_request_worker(slot);
    *out_request_id = slot->id;
    return 0;
}

static int sacx_api_net_request_start(const sacx_net_request_desc *desc, uint32_t *out_request_id)
{
    sacx_net_request_desc_ex extended;
    if (!desc)
        return -1;
    memset(&extended, 0, sizeof(extended));
    extended.url = desc->url;
    extended.max_response_bytes = desc->max_response_bytes;
    extended.timeout_ms = desc->timeout_ms;
    extended.redirect_limit = desc->redirect_limit;
    extended.method = "GET";
    return sacx_api_net_request_start_ex(&extended, out_request_id);
}

static uint32_t sacx_api_net_request_status(uint32_t request_id)
{
    sacx_net_request_slot *slot = sacx_net_slot_from_id(G_current_task, request_id);
    if (!slot)
        return SACX_NET_STATUS_EMPTY;
    if (slot->job_id)
    {
        uint32_t worker_status = kwork_status(slot->job_id);
        if (worker_status == KWORK_STATUS_DONE || worker_status == KWORK_STATUS_FAILED)
            asm_dma_invalidate_range(slot, sizeof(*slot));
    }
    return __atomic_load_n(&slot->status, __ATOMIC_ACQUIRE);
}

static int sacx_api_net_response_info(uint32_t request_id, sacx_net_response_info *out_info)
{
    sacx_net_request_slot *slot = sacx_net_slot_from_id(G_current_task, request_id);
    if (!slot || !out_info)
        return -1;
    if (slot->job_id)
    {
        uint32_t worker_status = kwork_status(slot->job_id);
        if (worker_status == KWORK_STATUS_DONE || worker_status == KWORK_STATUS_FAILED)
            asm_dma_invalidate_range(slot, sizeof(*slot));
    }
    *out_info = slot->info;
    out_info->status = __atomic_load_n(&slot->status, __ATOMIC_ACQUIRE);
    return 0;
}

static int sacx_api_net_response_read(uint32_t request_id, uint32_t offset, void *dst,
                                      uint32_t capacity, uint32_t *out_read)
{
    sacx_net_request_slot *slot = sacx_net_slot_from_id(G_current_task, request_id);
    uint32_t take;
    if (out_read)
        *out_read = 0u;
    if (!slot || !dst || !out_read ||
        __atomic_load_n(&slot->status, __ATOMIC_ACQUIRE) != SACX_NET_STATUS_DONE)
        return -1;
    if (offset >= slot->body_size)
        return 0;
    take = slot->body_size - offset;
    if (take > capacity)
        take = capacity;
    asm_dma_invalidate_range(slot->buffer + offset, take);
    memcpy(dst, slot->buffer + offset, take);
    *out_read = take;
    return 0;
}

static int sacx_api_net_request_cancel(uint32_t request_id)
{
    sacx_net_request_slot *slot = sacx_net_slot_from_id(G_current_task, request_id);
    if (!slot)
        return -1;
    __atomic_store_n(&slot->cancelled, 1u, __ATOMIC_RELEASE);
    asm_dma_clean_range((const void *)&slot->cancelled, sizeof(slot->cancelled));
    (void)kwork_cancel(slot->job_id);
    if (__atomic_load_n(&slot->status, __ATOMIC_ACQUIRE) == SACX_NET_STATUS_QUEUED)
    {
        __atomic_store_n(&slot->status, SACX_NET_STATUS_CANCELLED, __ATOMIC_RELEASE);
        asm_dma_clean_range((const void *)&slot->status, sizeof(slot->status));
    }
    return 0;
}

static int sacx_api_net_request_release(uint32_t request_id)
{
    sacx_net_request_slot *slot = sacx_net_slot_from_id(G_current_task, request_id);
    uint32_t status;
    if (!slot)
        return -1;
    status = __atomic_load_n(&slot->status, __ATOMIC_ACQUIRE);
    if (status == SACX_NET_STATUS_QUEUED || status == SACX_NET_STATUS_LOADING)
        return -2;
    if (slot->buffer && slot->pages)
        pmem_free_pages(slot->buffer, slot->pages);
    memset(slot, 0, sizeof(*slot));
    return 0;
}

static int sacx_api_mem_alloc(uint32_t size, void **out_ptr)
{
    uint64_t pages;
    uint64_t total_pages = 0u;
    sacx_memory_slot *free_slot = 0;
    void *ptr;
    if (out_ptr)
        *out_ptr = 0;
    if (!G_current_task || !out_ptr || !size || size > SACX_MAX_TASK_MEMORY_BYTES)
        return -1;
    pages = ((uint64_t)size + 4095ull) >> 12;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_MEMORY_ALLOCS; ++i)
    {
        sacx_memory_slot *slot = &G_current_task->memory_allocs[i];
        if (slot->used)
            total_pages += slot->pages;
        else if (!free_slot)
            free_slot = slot;
    }
    if (!free_slot || total_pages + pages > (SACX_MAX_TASK_MEMORY_BYTES >> 12))
        return -2;
    ptr = pmem_alloc_pages(pages);
    if (!ptr)
        return -3;
    memset(ptr, 0, (size_t)(pages << 12));
    free_slot->used = 1u;
    free_slot->pages = pages;
    free_slot->ptr = ptr;
    *out_ptr = ptr;
    return 0;
}

static int sacx_api_mem_free(void *ptr)
{
    if (!G_current_task || !ptr)
        return -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_MEMORY_ALLOCS; ++i)
    {
        sacx_memory_slot *slot = &G_current_task->memory_allocs[i];
        if (!slot->used || slot->ptr != ptr)
            continue;
        pmem_free_pages(slot->ptr, slot->pages);
        *slot = (sacx_memory_slot){0};
        return 0;
    }
    return -1;
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

static const char *sacx_api_app_arg_raw_path(void)
{
    if (!G_current_task)
        return "";
    return G_current_task->launch_arg_raw;
}

static const char *sacx_api_app_arg_friendly_path(void)
{
    if (!G_current_task)
        return "";
    return G_current_task->launch_arg_friendly;
}

static uint32_t sacx_api_app_arg_image(void)
{
    if (!G_current_task)
        return 0u;
    return G_current_task->launch_arg_image;
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
    kwindow_handle busy_window;
    int rc;
    if (!path)
        return -1;
    busy_window = sacx_task_busy_window(G_current_task);
    if (busy_window.idx >= 0)
        kbusy_begin_window(busy_window);
    else
        kbusy_begin();
    rc = kfile_unlink(path);
    kbusy_end();
    return rc;
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
    kwindow_handle hnd = {-1};
    kwindow_style native_style;

    if (!G_current_task || !out_handle || !G_runtime_font)
        return -1;

    sacx_window_style_to_native(style, &native_style);
    hnd = kwindow_create(sacx_ui_i32(x), sacx_ui_i32(y), sacx_ui_u32(w), sacx_ui_u32(h),
                         z, G_runtime_font, title ? title : "SACX App", &native_style);
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

static int sacx_api_window_focused(uint32_t handle)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return 0;
    return kwindow_focused(slot->handle);
}

static int sacx_api_window_set_close_deferred(uint32_t handle, uint32_t deferred)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    return kwindow_set_close_deferred(slot->handle, deferred ? 1u : 0u);
}

static int sacx_api_window_close_requested(uint32_t handle)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return 0;
    return kwindow_close_requested(slot->handle);
}

static int sacx_api_window_close_accept(uint32_t handle)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    return kwindow_close_accept(slot->handle);
}

static int sacx_api_window_close_cancel(uint32_t handle)
{
    sacx_window_slot *slot = sacx_window_from_handle(G_current_task, handle);
    if (!slot)
        return -1;
    return kwindow_close_cancel(slot->handle);
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
    return kwindow_point_can_receive_input(slot->handle, sacx_ui_i32(x), sacx_ui_i32(y));
}

static int sacx_api_window_set_modal_child(uint32_t parent_window_handle, uint32_t child_window_handle)
{
    sacx_window_slot *parent = sacx_window_from_handle(G_current_task, parent_window_handle);
    sacx_window_slot *child = sacx_window_from_handle(G_current_task, child_window_handle);
    if (!parent || !child)
        return -1;
    return kwindow_set_modal_child(parent->handle, child->handle);
}

static int sacx_api_window_clear_modal_child(uint32_t parent_window_handle)
{
    sacx_window_slot *parent = sacx_window_from_handle(G_current_task, parent_window_handle);
    if (!parent)
        return -1;
    return kwindow_clear_modal_child(parent->handle);
}

static int sacx_api_window_has_active_modal(uint32_t parent_window_handle)
{
    sacx_window_slot *parent = sacx_window_from_handle(G_current_task, parent_window_handle);
    if (!parent)
        return 0;
    return kwindow_has_active_modal(parent->handle);
}

static int sacx_api_window_center_on_parent(uint32_t child_window_handle, uint32_t parent_window_handle)
{
    sacx_window_slot *child = sacx_window_from_handle(G_current_task, child_window_handle);
    sacx_window_slot *parent = sacx_window_from_handle(G_current_task, parent_window_handle);
    if (!child || !parent)
        return -1;
    return kwindow_center_on_parent(child->handle, parent->handle);
}

static int sacx_ui_parent_from_obj_handle(uint32_t parent_obj_handle, kgfx_obj_handle *out_parent)
{
    sacx_gfx_slot *parent = 0;

    if (!out_parent)
        return -1;
    out_parent->idx = -1;
    if (parent_obj_handle == 0u)
        return 0;

    parent = sacx_gfx_from_handle(G_current_task, parent_obj_handle);
    if (!parent)
        return -1;
    *out_parent = parent->handle;
    return 0;
}

static void sacx_ui_layout_to_native(const sacx_ui_layout_desc *in, kui_layout_desc *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!in)
        return;

    out->kind = in->kind;
    out->padding_x = sacx_ui_u32(in->padding_x);
    out->padding_y = sacx_ui_u32(in->padding_y);
    out->gap_x = sacx_ui_u32(in->gap_x);
    out->gap_y = sacx_ui_u32(in->gap_y);
    out->columns = in->columns;
    out->flags = in->flags;
}

static int sacx_api_ui_view_create_with_parent(kgfx_obj_handle parent, int32_t x, int32_t y,
                                               uint32_t w, uint32_t h, int32_t z,
                                               sacx_color fill, uint32_t visible,
                                               uint32_t *out_view_handle, uint32_t *out_obj_handle)
{
    kui_view_handle view = {-1};
    kgfx_obj_handle root = {-1};
    uint32_t free_idx = SACX_MAX_TASK_UI_VIEWS;

    if (!G_current_task || !out_view_handle || !out_obj_handle)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_VIEWS; ++i)
    {
        if (!G_current_task->ui_views[i].used)
        {
            free_idx = i;
            break;
        }
    }
    if (free_idx >= SACX_MAX_TASK_UI_VIEWS)
        return -1;

    if (kui_view_create_rect(parent, sacx_ui_i32(x), sacx_ui_i32(y),
                             sacx_ui_u32(w), sacx_ui_u32(h), z,
                             sacx_to_kcolor(fill), visible, &view, &root) != 0)
        return -1;

    if (sacx_gfx_register_existing(G_current_task, root, out_obj_handle) != 0)
    {
        (void)kui_view_destroy(view);
        return -1;
    }

    G_current_task->ui_views[free_idx].used = 1u;
    G_current_task->ui_views[free_idx].handle = view;
    *out_view_handle = free_idx + 1u;
    return 0;
}

static int sacx_api_ui_view_create_rect(uint32_t parent_obj_handle, int32_t x, int32_t y,
                                        uint32_t w, uint32_t h, int32_t z,
                                        sacx_color fill, uint32_t visible,
                                        uint32_t *out_view_handle, uint32_t *out_obj_handle)
{
    kgfx_obj_handle parent = {-1};
    if (sacx_ui_parent_from_obj_handle(parent_obj_handle, &parent) != 0)
        return -1;
    return sacx_api_ui_view_create_with_parent(parent, x, y, w, h, z, fill, visible,
                                              out_view_handle, out_obj_handle);
}

static int sacx_api_ui_window_view_create(uint32_t window_handle, int32_t x, int32_t y,
                                          uint32_t w, uint32_t h, int32_t z,
                                          sacx_color fill, uint32_t visible,
                                          uint32_t *out_view_handle, uint32_t *out_obj_handle)
{
    sacx_window_slot *window = sacx_window_from_handle(G_current_task, window_handle);
    if (!window)
        return -1;
    return sacx_api_ui_view_create_with_parent(kwindow_root(window->handle), x, y, w, h, z, fill, visible,
                                              out_view_handle, out_obj_handle);
}

static int sacx_api_ui_view_destroy(uint32_t view_handle)
{
    sacx_ui_view_slot *slot = sacx_ui_view_from_handle(G_current_task, view_handle);
    if (!slot)
        return -1;
    (void)kui_view_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    return 0;
}

static int sacx_api_ui_view_root(uint32_t view_handle, uint32_t *out_obj_handle)
{
    sacx_ui_view_slot *slot = sacx_ui_view_from_handle(G_current_task, view_handle);
    if (!slot || !out_obj_handle)
        return -1;
    return sacx_gfx_register_existing(G_current_task, kui_view_root(slot->handle), out_obj_handle);
}

static int sacx_api_ui_view_add_obj(uint32_t view_handle, uint32_t state, uint32_t obj_handle)
{
    sacx_ui_view_slot *view = sacx_ui_view_from_handle(G_current_task, view_handle);
    sacx_gfx_slot *obj = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!view || !obj)
        return -1;
    return kui_view_add_obj(view->handle, state, obj->handle);
}

static int sacx_api_ui_view_set_state(uint32_t view_handle, uint32_t state)
{
    sacx_ui_view_slot *view = sacx_ui_view_from_handle(G_current_task, view_handle);
    if (!view)
        return -1;
    return kui_view_set_state(view->handle, state);
}

static uint32_t sacx_api_ui_view_state(uint32_t view_handle)
{
    sacx_ui_view_slot *view = sacx_ui_view_from_handle(G_current_task, view_handle);
    return view ? kui_view_state(view->handle) : 0u;
}

static int sacx_api_ui_view_set_visible(uint32_t view_handle, uint32_t visible)
{
    sacx_ui_view_slot *view = sacx_ui_view_from_handle(G_current_task, view_handle);
    if (!view)
        return -1;
    return kui_view_set_visible(view->handle, visible);
}

static int sacx_api_ui_view_set_layout(uint32_t view_handle, const sacx_ui_layout_desc *desc)
{
    sacx_ui_view_slot *view = sacx_ui_view_from_handle(G_current_task, view_handle);
    kui_layout_desc native_desc;
    if (!view)
        return -1;
    sacx_ui_layout_to_native(desc, &native_desc);
    return kui_view_set_layout(view->handle, &native_desc);
}

static int sacx_api_ui_view_apply_layout(uint32_t view_handle)
{
    sacx_ui_view_slot *view = sacx_ui_view_from_handle(G_current_task, view_handle);
    if (!view)
        return -1;
    return kui_view_apply_layout(view->handle);
}

static int sacx_api_ui_view_set_bounds(uint32_t view_handle, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    sacx_ui_view_slot *view = sacx_ui_view_from_handle(G_current_task, view_handle);
    if (!view)
        return -1;
    return kui_view_set_bounds(view->handle, sacx_ui_i32(x), sacx_ui_i32(y), sacx_ui_u32(w), sacx_ui_u32(h));
}

static int sacx_api_ui_dropdown_create(uint32_t parent_obj_handle, int32_t x, int32_t y, uint32_t w, uint32_t item_h,
                                       int32_t z, const char *const *items, uint32_t item_count, uint32_t selected,
                                       sacx_ui_on_change_fn on_change, void *user,
                                       uint32_t *out_dropdown_handle, uint32_t *out_obj_handle)
{
    kgfx_obj_handle parent = {-1};
    kgfx_obj_handle root = {-1};
    kui_dropdown_handle dropdown = {-1};

    if (!G_current_task || !out_dropdown_handle || !out_obj_handle)
        return -1;
    if (sacx_ui_parent_from_obj_handle(parent_obj_handle, &parent) != 0)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_DROPDOWNS; ++i)
    {
        if (G_current_task->ui_dropdowns[i].used)
            continue;

        G_current_task->ui_dropdowns[i].used = 1u;
        G_current_task->ui_dropdowns[i].handle.idx = -1;
        G_current_task->ui_dropdowns[i].callback = on_change;
        G_current_task->ui_dropdowns[i].callback_user = user;
        G_current_task->ui_dropdowns[i].owner = G_current_task;

        if (kui_dropdown_create(parent, sacx_ui_i32(x), sacx_ui_i32(y),
                                sacx_ui_u32(w), sacx_ui_u32(item_h), z, items, item_count, selected,
                                on_change ? sacx_ui_dropdown_change_trampoline : 0,
                                &G_current_task->ui_dropdowns[i], &dropdown, &root) != 0)
            goto fail;

        if (sacx_gfx_register_existing(G_current_task, root, out_obj_handle) != 0)
        {
            (void)kui_dropdown_destroy(dropdown);
            goto fail;
        }

        G_current_task->ui_dropdowns[i].handle = dropdown;
        *out_dropdown_handle = i + 1u;
        return 0;

    fail:
        G_current_task->ui_dropdowns[i].used = 0u;
        G_current_task->ui_dropdowns[i].handle.idx = -1;
        G_current_task->ui_dropdowns[i].callback = 0;
        G_current_task->ui_dropdowns[i].callback_user = 0;
        G_current_task->ui_dropdowns[i].owner = 0;
        return -1;
    }

    return -1;
}

static int sacx_api_ui_dropdown_destroy(uint32_t dropdown_handle)
{
    sacx_ui_dropdown_slot *slot = sacx_ui_dropdown_from_handle(G_current_task, dropdown_handle);
    if (!slot)
        return -1;
    (void)kui_dropdown_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    slot->callback = 0;
    slot->callback_user = 0;
    slot->owner = 0;
    return 0;
}

static int sacx_api_ui_dropdown_root(uint32_t dropdown_handle, uint32_t *out_obj_handle)
{
    sacx_ui_dropdown_slot *slot = sacx_ui_dropdown_from_handle(G_current_task, dropdown_handle);
    if (!slot || !out_obj_handle)
        return -1;
    return sacx_gfx_register_existing(G_current_task, kui_dropdown_root(slot->handle), out_obj_handle);
}

static int sacx_api_ui_dropdown_selected(uint32_t dropdown_handle)
{
    sacx_ui_dropdown_slot *slot = sacx_ui_dropdown_from_handle(G_current_task, dropdown_handle);
    return slot ? kui_dropdown_selected(slot->handle) : -1;
}

static int sacx_api_ui_dropdown_set_selected(uint32_t dropdown_handle, uint32_t selected)
{
    sacx_ui_dropdown_slot *slot = sacx_ui_dropdown_from_handle(G_current_task, dropdown_handle);
    if (!slot)
        return -1;
    return kui_dropdown_set_selected(slot->handle, selected);
}

static int sacx_api_ui_dropdown_set_enabled(uint32_t dropdown_handle, uint32_t enabled)
{
    sacx_ui_dropdown_slot *slot = sacx_ui_dropdown_from_handle(G_current_task, dropdown_handle);
    if (!slot)
        return -1;
    return kui_dropdown_set_enabled(slot->handle, enabled);
}

static int sacx_api_ui_radio_create(uint32_t parent_obj_handle, int32_t x, int32_t y, uint32_t w, uint32_t item_h,
                                    int32_t z, const char *const *items, uint32_t item_count, uint32_t selected,
                                    sacx_ui_on_change_fn on_change, void *user,
                                    uint32_t *out_radio_handle, uint32_t *out_obj_handle)
{
    kgfx_obj_handle parent = {-1};
    kgfx_obj_handle root = {-1};
    kui_radio_handle radio = {-1};

    if (!G_current_task || !out_radio_handle || !out_obj_handle)
        return -1;
    if (sacx_ui_parent_from_obj_handle(parent_obj_handle, &parent) != 0)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_RADIOS; ++i)
    {
        if (G_current_task->ui_radios[i].used)
            continue;

        G_current_task->ui_radios[i].used = 1u;
        G_current_task->ui_radios[i].handle.idx = -1;
        G_current_task->ui_radios[i].callback = on_change;
        G_current_task->ui_radios[i].callback_user = user;
        G_current_task->ui_radios[i].owner = G_current_task;

        if (kui_radio_create(parent, sacx_ui_i32(x), sacx_ui_i32(y),
                             sacx_ui_u32(w), sacx_ui_u32(item_h), z, items, item_count, selected,
                             on_change ? sacx_ui_radio_change_trampoline : 0,
                             &G_current_task->ui_radios[i], &radio, &root) != 0)
            goto fail;

        if (sacx_gfx_register_existing(G_current_task, root, out_obj_handle) != 0)
        {
            (void)kui_radio_destroy(radio);
            goto fail;
        }

        G_current_task->ui_radios[i].handle = radio;
        *out_radio_handle = i + 1u;
        return 0;

    fail:
        G_current_task->ui_radios[i].used = 0u;
        G_current_task->ui_radios[i].handle.idx = -1;
        G_current_task->ui_radios[i].callback = 0;
        G_current_task->ui_radios[i].callback_user = 0;
        G_current_task->ui_radios[i].owner = 0;
        return -1;
    }

    return -1;
}

static int sacx_api_ui_radio_destroy(uint32_t radio_handle)
{
    sacx_ui_radio_slot *slot = sacx_ui_radio_from_handle(G_current_task, radio_handle);
    if (!slot)
        return -1;
    (void)kui_radio_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    slot->callback = 0;
    slot->callback_user = 0;
    slot->owner = 0;
    return 0;
}

static int sacx_api_ui_radio_root(uint32_t radio_handle, uint32_t *out_obj_handle)
{
    sacx_ui_radio_slot *slot = sacx_ui_radio_from_handle(G_current_task, radio_handle);
    if (!slot || !out_obj_handle)
        return -1;
    return sacx_gfx_register_existing(G_current_task, kui_radio_root(slot->handle), out_obj_handle);
}

static int sacx_api_ui_radio_selected(uint32_t radio_handle)
{
    sacx_ui_radio_slot *slot = sacx_ui_radio_from_handle(G_current_task, radio_handle);
    return slot ? kui_radio_selected(slot->handle) : -1;
}

static int sacx_api_ui_radio_set_selected(uint32_t radio_handle, uint32_t selected)
{
    sacx_ui_radio_slot *slot = sacx_ui_radio_from_handle(G_current_task, radio_handle);
    if (!slot)
        return -1;
    return kui_radio_set_selected(slot->handle, selected);
}

static int sacx_api_ui_radio_set_enabled(uint32_t radio_handle, uint32_t enabled)
{
    sacx_ui_radio_slot *slot = sacx_ui_radio_from_handle(G_current_task, radio_handle);
    if (!slot)
        return -1;
    return kui_radio_set_enabled(slot->handle, enabled);
}

static int sacx_api_ui_toggle_create(uint32_t parent_obj_handle, int32_t x, int32_t y, uint32_t w, uint32_t h,
                                     int32_t z, const char *label, uint32_t checked,
                                     sacx_ui_on_change_fn on_change, void *user,
                                     uint32_t *out_toggle_handle, uint32_t *out_obj_handle)
{
    kgfx_obj_handle parent = {-1};
    kgfx_obj_handle root = {-1};
    kui_toggle_handle toggle = {-1};

    if (!G_current_task || !out_toggle_handle || !out_obj_handle)
        return -1;
    if (sacx_ui_parent_from_obj_handle(parent_obj_handle, &parent) != 0)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_UI_TOGGLES; ++i)
    {
        if (G_current_task->ui_toggles[i].used)
            continue;

        G_current_task->ui_toggles[i].used = 1u;
        G_current_task->ui_toggles[i].handle.idx = -1;
        G_current_task->ui_toggles[i].callback = on_change;
        G_current_task->ui_toggles[i].callback_user = user;
        G_current_task->ui_toggles[i].owner = G_current_task;

        if (kui_toggle_create(parent, sacx_ui_i32(x), sacx_ui_i32(y),
                              sacx_ui_u32(w), sacx_ui_u32(h), z, label, checked,
                              on_change ? sacx_ui_toggle_change_trampoline : 0,
                              &G_current_task->ui_toggles[i], &toggle, &root) != 0)
            goto fail;

        if (sacx_gfx_register_existing(G_current_task, root, out_obj_handle) != 0)
        {
            (void)kui_toggle_destroy(toggle);
            goto fail;
        }

        G_current_task->ui_toggles[i].handle = toggle;
        *out_toggle_handle = i + 1u;
        return 0;

    fail:
        G_current_task->ui_toggles[i].used = 0u;
        G_current_task->ui_toggles[i].handle.idx = -1;
        G_current_task->ui_toggles[i].callback = 0;
        G_current_task->ui_toggles[i].callback_user = 0;
        G_current_task->ui_toggles[i].owner = 0;
        return -1;
    }

    return -1;
}

static int sacx_api_ui_toggle_destroy(uint32_t toggle_handle)
{
    sacx_ui_toggle_slot *slot = sacx_ui_toggle_from_handle(G_current_task, toggle_handle);
    if (!slot)
        return -1;
    (void)kui_toggle_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    slot->callback = 0;
    slot->callback_user = 0;
    slot->owner = 0;
    return 0;
}

static int sacx_api_ui_toggle_root(uint32_t toggle_handle, uint32_t *out_obj_handle)
{
    sacx_ui_toggle_slot *slot = sacx_ui_toggle_from_handle(G_current_task, toggle_handle);
    if (!slot || !out_obj_handle)
        return -1;
    return sacx_gfx_register_existing(G_current_task, kui_toggle_root(slot->handle), out_obj_handle);
}

static int sacx_api_ui_toggle_checked(uint32_t toggle_handle)
{
    sacx_ui_toggle_slot *slot = sacx_ui_toggle_from_handle(G_current_task, toggle_handle);
    return slot ? kui_toggle_checked(slot->handle) : -1;
}

static int sacx_api_ui_toggle_set_checked(uint32_t toggle_handle, uint32_t checked)
{
    sacx_ui_toggle_slot *slot = sacx_ui_toggle_from_handle(G_current_task, toggle_handle);
    if (!slot)
        return -1;
    return kui_toggle_set_checked(slot->handle, checked);
}

static int sacx_api_ui_toggle_set_enabled(uint32_t toggle_handle, uint32_t enabled)
{
    sacx_ui_toggle_slot *slot = sacx_ui_toggle_from_handle(G_current_task, toggle_handle);
    if (!slot)
        return -1;
    return kui_toggle_set_enabled(slot->handle, enabled);
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
    obj = kgfx_obj_add_rect(sacx_ui_i32(x), sacx_ui_i32(y),
                            sacx_ui_u32(w), sacx_ui_u32(h),
                            z, sacx_to_kcolor(fill), visible ? 1u : 0u);
    if (obj.idx < 0)
        return -1;
    if (sacx_gfx_register_existing(G_current_task, obj, out_obj_handle) != 0)
    {
        (void)kgfx_obj_destroy(obj);
        return -1;
    }
    sacx_gfx_mark_owned(G_current_task, *out_obj_handle);
    return 0;
}

static int sacx_api_gfx_obj_add_circle(int32_t cx, int32_t cy, uint32_t r, int32_t z,
                                       sacx_color fill, uint32_t visible, uint32_t *out_obj_handle)
{
    kgfx_obj_handle obj;
    if (!G_current_task || !out_obj_handle)
        return -1;
    obj = kgfx_obj_add_circle(sacx_ui_i32(cx), sacx_ui_i32(cy),
                              sacx_ui_u32(r), z, sacx_to_kcolor(fill),
                              visible ? 1u : 0u);
    if (obj.idx < 0)
        return -1;
    if (sacx_gfx_register_existing(G_current_task, obj, out_obj_handle) != 0)
    {
        (void)kgfx_obj_destroy(obj);
        return -1;
    }
    sacx_gfx_mark_owned(G_current_task, *out_obj_handle);
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
    obj = kgfx_obj_add_text(G_runtime_font, text, sacx_ui_i32(x), sacx_ui_i32(y),
                            z, sacx_to_kcolor(color), alpha,
                            sacx_ui_text(scale),
                            sacx_ui_spacing(char_spacing),
                            sacx_ui_spacing(line_spacing), sacx_to_text_align(align),
                            visible ? 1u : 0u);
    if (obj.idx < 0)
        return -1;
    if (sacx_gfx_register_existing(G_current_task, obj, out_obj_handle) != 0)
    {
        (void)kgfx_obj_destroy(obj);
        return -1;
    }
    sacx_gfx_mark_owned(G_current_task, *out_obj_handle);
    return 0;
}

static int sacx_api_gfx_obj_add_image_from_img(uint32_t image_handle, int32_t x, int32_t y, uint32_t *out_obj_handle)
{
    sacx_image_slot *img = sacx_image_from_handle(G_current_task, image_handle);
    kgfx_obj_handle obj;
    if (!img || !img->image.px || !out_obj_handle)
        return -1;
    obj = kgfx_obj_add_image(img->image.px, img->image.w, img->image.h,
                             sacx_ui_i32(x), sacx_ui_i32(y), img->image.w);
    if (obj.idx < 0)
        return -1;
    kgfx_image_set_size(obj, sacx_ui_u32(img->image.w), sacx_ui_u32(img->image.h));
    if (sacx_gfx_register_existing(G_current_task, obj, out_obj_handle) != 0)
    {
        (void)kgfx_obj_destroy(obj);
        return -1;
    }
    sacx_gfx_mark_owned(G_current_task, *out_obj_handle);
    return 0;
}

static int sacx_api_gfx_obj_destroy(uint32_t obj_handle)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    if (slot->destroy_on_finish)
        (void)kgfx_obj_destroy(slot->handle);
    slot->used = 0u;
    slot->destroy_on_finish = 0u;
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
    obj->outline_width = (uint16_t)sacx_ui_u32(width);
    if (width && !obj->outline_width)
        obj->outline_width = 1u;
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
    obj->u.rect.x = sacx_ui_i32(x);
    obj->u.rect.y = sacx_ui_i32(y);
    obj->u.rect.w = sacx_ui_u32(w);
    obj->u.rect.h = sacx_ui_u32(h);
    return 0;
}

static int sacx_api_gfx_obj_get_rect(uint32_t obj_handle, int32_t *out_x, int32_t *out_y, uint32_t *out_w, uint32_t *out_h)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || !out_x || !out_y || !out_w || !out_h)
        return -1;

    if (obj->kind == KGFX_OBJ_RECT)
    {
        *out_x = sacx_ui_unscale_i32(obj->u.rect.x);
        *out_y = sacx_ui_unscale_i32(obj->u.rect.y);
        *out_w = sacx_ui_unscale_u32(obj->u.rect.w);
        *out_h = sacx_ui_unscale_u32(obj->u.rect.h);
        return 0;
    }
    if (obj->kind == KGFX_OBJ_IMAGE)
    {
        *out_x = sacx_ui_unscale_i32(obj->u.image.x);
        *out_y = sacx_ui_unscale_i32(obj->u.image.y);
        *out_w = sacx_ui_unscale_u32(obj->u.image.w);
        *out_h = sacx_ui_unscale_u32(obj->u.image.h);
        return 0;
    }
    return -1;
}

static int sacx_api_gfx_obj_set_rotation_deg(uint32_t obj_handle, int32_t deg)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_obj_set_rotation_deg(slot->handle, deg);
    return 0;
}

static int sacx_api_gfx_obj_rotation_deg(uint32_t obj_handle, int32_t *out_deg)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || !out_deg)
        return -1;
    *out_deg = obj->rotation_deg;
    return 0;
}

static int sacx_api_gfx_obj_set_rotation_pivot(uint32_t obj_handle, int32_t x, int32_t y)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_obj_set_rotation_pivot(slot->handle, sacx_ui_i32(x), sacx_ui_i32(y));
    return 0;
}

static int sacx_api_gfx_obj_clear_rotation_pivot(uint32_t obj_handle)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot)
        return -1;
    kgfx_obj_clear_rotation_pivot(slot->handle);
    return 0;
}

static int sacx_api_gfx_obj_set_circle(uint32_t obj_handle, int32_t cx, int32_t cy, uint32_t r)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || obj->kind != KGFX_OBJ_CIRCLE)
        return -1;
    obj->u.circle.cx = sacx_ui_i32(cx);
    obj->u.circle.cy = sacx_ui_i32(cy);
    obj->u.circle.r = sacx_ui_u32(r);
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
    kgfx_text_set_spacing(slot->handle, sacx_ui_spacing(char_spacing), sacx_ui_spacing(line_spacing));
    return 0;
}

static int sacx_api_gfx_text_set_scale(uint32_t obj_handle, uint32_t scale)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot || scale == 0u)
        return -1;
    kgfx_text_set_scale(slot->handle, sacx_ui_text(scale));
    return 0;
}

static int sacx_api_gfx_text_set_pos(uint32_t obj_handle, int32_t x, int32_t y)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || obj->kind != KGFX_OBJ_TEXT)
        return -1;
    obj->u.text.x = sacx_ui_i32(x);
    obj->u.text.y = sacx_ui_i32(y);
    return 0;
}

static int sacx_api_gfx_image_set_size(uint32_t obj_handle, uint32_t w, uint32_t h)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot || w == 0u || h == 0u)
        return -1;
    kgfx_image_set_size(slot->handle, sacx_ui_u32(w), sacx_ui_u32(h));
    return 0;
}

static int sacx_api_gfx_image_set_pos(uint32_t obj_handle, int32_t x, int32_t y)
{
    kgfx_obj *obj = sacx_api_obj_ref(obj_handle);
    if (!obj || obj->kind != KGFX_OBJ_IMAGE)
        return -1;
    obj->u.image.x = sacx_ui_i32(x);
    obj->u.image.y = sacx_ui_i32(y);
    return 0;
}

static int sacx_api_gfx_image_set_scale_pct(uint32_t obj_handle, uint32_t scale_pct)
{
    sacx_gfx_slot *slot = sacx_gfx_from_handle(G_current_task, obj_handle);
    if (!slot || scale_pct == 0u)
        return -1;
    kgfx_image_set_scale(slot->handle, sacx_ui_percent(scale_pct));
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
    button = kbutton_add_rect(sacx_ui_i32(x), sacx_ui_i32(y),
                              sacx_ui_u32(w), sacx_ui_u32(h),
                              z, &native_style, 0, 0);
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
    textbox = ktextbox_add_rect(sacx_ui_i32(x), sacx_ui_i32(y),
                                sacx_ui_u32(w), sacx_ui_u32(h),
                                z, G_runtime_font, &native_style, 0, 0);
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
    ktextbox_set_bounds(slot->handle, sacx_ui_i32(x), sacx_ui_i32(y),
                        sacx_ui_u32(w), sacx_ui_u32(h));
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

static int sacx_api_textbox_select(uint32_t textbox_handle, uint32_t start, uint32_t end)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    ktextbox_select(slot->handle, start, end);
    return 0;
}

static int sacx_api_textbox_selection(uint32_t textbox_handle, uint32_t *out_start, uint32_t *out_end)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot || !out_start || !out_end)
        return -1;
    return ktextbox_selection(slot->handle, out_start, out_end);
}

static int sacx_api_textbox_copy_selection(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    return (int)ktextbox_copy_selection(slot->handle);
}

static int sacx_api_textbox_cut_selection(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    return (int)ktextbox_cut_selection(slot->handle);
}

static int sacx_api_textbox_paste(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    return (int)ktextbox_paste(slot->handle);
}

static int sacx_api_textbox_undo(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    return ktextbox_undo(slot->handle);
}

static int sacx_api_textbox_redo(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    return ktextbox_redo(slot->handle);
}

static int sacx_api_textbox_set_max_len(uint32_t textbox_handle, uint32_t max_len)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return -1;
    ktextbox_set_max_len(slot->handle, max_len);
    return 0;
}

static uint32_t sacx_api_textbox_max_len(uint32_t textbox_handle)
{
    sacx_textbox_slot *slot = sacx_textbox_from_handle(G_current_task, textbox_handle);
    if (!slot)
        return 0u;
    return ktextbox_max_len(slot->handle);
}

static int32_t sacx_api_input_mouse_dx(void)
{
    return sacx_ui_unscale_i32(kmouse_dx());
}

static int32_t sacx_api_input_mouse_dy(void)
{
    return sacx_ui_unscale_i32(kmouse_dy());
}

static int32_t sacx_api_input_mouse_wheel(void)
{
    return kmouse_wheel();
}

static uint8_t sacx_api_input_mouse_buttons(void)
{
    return kmouse_buttons();
}

static int sacx_api_input_mouse_consume(sacx_mouse_state *out_state)
{
    kmouse_state mouse;

    if (!out_state)
        return -1;

    memset(&mouse, 0, sizeof(mouse));
    kmouse_get_state(&mouse);
    memset(out_state, 0, sizeof(*out_state));
    out_state->x = sacx_ui_unscale_i32(mouse.x);
    out_state->y = sacx_ui_unscale_i32(mouse.y);
    out_state->dx = sacx_ui_unscale_i32(mouse.dx);
    out_state->dy = sacx_ui_unscale_i32(mouse.dy);
    out_state->wheel = mouse.wheel;
    out_state->buttons = mouse.buttons;
    out_state->visible = mouse.visible;
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
    return sacx_ui_unscale_i32(kmouse_x());
}

static int32_t sacx_api_mouse_y(void)
{
    return sacx_ui_unscale_i32(kmouse_y());
}

static int32_t sacx_api_mouse_dx(void)
{
    return sacx_ui_unscale_i32(kmouse_dx());
}

static int32_t sacx_api_mouse_dy(void)
{
    return sacx_ui_unscale_i32(kmouse_dy());
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
    out_state->x = sacx_ui_unscale_i32(in.x);
    out_state->y = sacx_ui_unscale_i32(in.y);
    out_state->dx = sacx_ui_unscale_i32(in.dx);
    out_state->dy = sacx_ui_unscale_i32(in.dy);
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
    ktext_draw_str_ex(G_runtime_font, sacx_ui_i32(x), sacx_ui_i32(y), text,
                      sacx_to_kcolor(color), alpha, sacx_ui_text(scale),
                      sacx_ui_spacing(char_spacing), sacx_ui_spacing(line_spacing));
    return 0;
}

static int sacx_api_text_draw_align(int32_t anchor_x, int32_t y, const char *text,
                                    sacx_color color, uint8_t alpha, uint32_t scale,
                                    int32_t char_spacing, int32_t line_spacing, uint32_t align)
{
    if (!G_runtime_font || !text || scale == 0u)
        return -1;
    ktext_draw_str_align(G_runtime_font, sacx_ui_i32(anchor_x), sacx_ui_i32(y), text,
                         sacx_to_kcolor(color), alpha, sacx_ui_text(scale),
                         sacx_ui_spacing(char_spacing), sacx_ui_spacing(line_spacing),
                         sacx_to_text_align(align));
    return 0;
}

static int sacx_api_text_draw_outline_align(int32_t anchor_x, int32_t y, const char *text,
                                            sacx_color fill, uint8_t fill_alpha, uint32_t scale,
                                            int32_t char_spacing, int32_t line_spacing, uint32_t align,
                                            uint32_t outline_width, sacx_color outline, uint8_t outline_alpha)
{
    if (!G_runtime_font || !text || scale == 0u)
        return -1;
    ktext_draw_str_align_outline(G_runtime_font, sacx_ui_i32(anchor_x), sacx_ui_i32(y), text,
                                 sacx_to_kcolor(fill), fill_alpha, sacx_ui_text(scale),
                                 sacx_ui_spacing(char_spacing), sacx_ui_spacing(line_spacing),
                                 sacx_to_text_align(align),
                                 sacx_ui_u32(outline_width), sacx_to_kcolor(outline), outline_alpha);
    return 0;
}

static uint32_t sacx_api_text_measure_line_px(const char *text, uint32_t scale, int32_t char_spacing)
{
    if (!G_runtime_font || !text || scale == 0u)
        return 0u;
    return sacx_ui_unscale_u32(ktext_measure_line_px(G_runtime_font, text, sacx_ui_text(scale), sacx_ui_spacing(char_spacing)));
}

static uint32_t sacx_api_text_line_height(uint32_t scale, int32_t line_spacing)
{
    if (!G_runtime_font || scale == 0u)
        return 0u;
    return sacx_ui_unscale_u32(ktext_line_height(G_runtime_font, sacx_ui_text(scale), sacx_ui_spacing(line_spacing)));
}

static uint32_t sacx_api_text_scale_mul_px(uint32_t px, uint32_t scale)
{
    return sacx_ui_unscale_u32(ktext_scale_mul_px(px, sacx_ui_text(scale ? scale : 1u)));
}

static uint64_t sacx_task_image_bytes(const sacx_task *task);

static int sacx_api_img_register_loaded(kimg *image, uint32_t *out_image_handle)
{
    uint64_t bytes = 0u;
    if (!G_current_task || !image || !image->px || !out_image_handle)
        return -1;
    bytes = (uint64_t)image->w * image->h * 4ull;
    if (!bytes || image->w > 8192u || image->h > 8192u ||
        sacx_task_image_bytes(G_current_task) + bytes > 256ull * 1024ull * 1024ull)
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

static int sacx_task_register_image_copy(sacx_task *task, const kimg *source, uint32_t *out_image_handle)
{
    uint64_t bytes = 0u;
    uint64_t pages = 0u;
    uint32_t *pixels = 0;

    if (!task || !source || !source->px || !source->w || !source->h || !out_image_handle)
        return -1;
    bytes = (uint64_t)source->w * source->h * 4ull;
    if (!bytes || source->w > 8192u || source->h > 8192u ||
        sacx_task_image_bytes(task) + bytes > 256ull * 1024ull * 1024ull)
        return -1;
    pages = (bytes + 4095ull) >> 12;
    pixels = (uint32_t *)pmem_alloc_pages(pages);
    if (!pixels)
        return -1;
    memcpy(pixels, source->px, (size_t)bytes);

    for (uint32_t i = 0u; i < SACX_MAX_TASK_IMAGES; ++i)
    {
        if (task->images[i].used)
            continue;
        task->images[i].used = 1u;
        task->images[i].image.px = pixels;
        task->images[i].image.w = source->w;
        task->images[i].image.h = source->h;
        *out_image_handle = i + 1u;
        return 0;
    }

    pmem_free_pages(pixels, pages);
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

static int sacx_api_img_load_memory(const void *data, uint32_t size, uint32_t *out_image_handle)
{
    kimg image = {0};
    uint64_t bytes;
    uint64_t pages;
    if (!data || !size || !out_image_handle)
        return -1;
    if (kimg_load_memory(&image, data, size) != 0)
        return -1;
    if (sacx_api_img_register_loaded(&image, out_image_handle) == 0)
        return 0;
    bytes = (uint64_t)image.w * image.h * 4ull;
    pages = (bytes + 4095ull) >> 12;
    if (image.px && pages)
        pmem_free_pages(image.px, pages);
    return -1;
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

static uint64_t sacx_task_image_bytes(const sacx_task *task)
{
    uint64_t total = 0u;
    if (!task)
        return 0u;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_IMAGES; ++i)
    {
        if (task->images[i].used)
            total += (uint64_t)task->images[i].image.w * task->images[i].image.h * 4ull;
    }
    return total;
}

static int sacx_api_img_create(uint32_t w, uint32_t h, uint32_t argb, uint32_t *out_image_handle)
{
    kimg image = {0};
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!G_current_task || !out_image_handle || !w || !h || w > 8192u || h > 8192u)
        return -1;
    bytes = (uint64_t)w * h * 4ull;
    if (!bytes || bytes > 256ull * 1024ull * 1024ull ||
        sacx_task_image_bytes(G_current_task) + bytes > 256ull * 1024ull * 1024ull)
        return -1;
    pages = (bytes + 4095ull) >> 12;
    image.px = (uint32_t *)pmem_alloc_pages(pages);
    if (!image.px)
        return -1;
    image.w = w;
    image.h = h;
    for (uint64_t i = 0u; i < (uint64_t)w * h; ++i)
        image.px[i] = argb;
    if (sacx_api_img_register_loaded(&image, out_image_handle) != 0)
    {
        pmem_free_pages(image.px, pages);
        return -1;
    }
    return 0;
}

static int sacx_api_img_clone(uint32_t image_handle, uint32_t *out_image_handle)
{
    sacx_image_slot *source = sacx_image_from_handle(G_current_task, image_handle);
    kimg image = {0};
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!source || !source->image.px || !out_image_handle)
        return -1;
    bytes = (uint64_t)source->image.w * source->image.h * 4ull;
    if (!bytes || sacx_task_image_bytes(G_current_task) + bytes > 256ull * 1024ull * 1024ull)
        return -1;
    pages = (bytes + 4095ull) >> 12;
    image.px = (uint32_t *)pmem_alloc_pages(pages);
    if (!image.px)
        return -1;
    memcpy(image.px, source->image.px, (size_t)bytes);
    image.w = source->image.w;
    image.h = source->image.h;
    if (sacx_api_img_register_loaded(&image, out_image_handle) != 0)
    {
        pmem_free_pages(image.px, pages);
        return -1;
    }
    return 0;
}

static int sacx_api_img_pixels(uint32_t image_handle, uint32_t **out_argb, uint32_t *out_stride_px)
{
    sacx_image_slot *slot = sacx_image_from_handle(G_current_task, image_handle);
    if (!slot || !slot->image.px || !out_argb || !out_stride_px)
        return -1;
    *out_argb = slot->image.px;
    *out_stride_px = slot->image.w;
    return 0;
}

static int sacx_api_img_touch(uint32_t image_handle)
{
    sacx_image_slot *slot = sacx_image_from_handle(G_current_task, image_handle);
    if (!slot || !slot->image.px)
        return -1;

    /* SACX apps write image pixels with the CPU, then call img_touch before
       the compositor reads them.  Publish those dirty CPU cache lines; an
       invalidate here discards the app's new pixels on non-coherent AA64. */
    if (slot->image.w && slot->image.h)
        asm_dma_clean_range(slot->image.px,
                            (uint64_t)slot->image.w * (uint64_t)slot->image.h * 4ull);
    for (uint32_t i = 0u; i < SACX_MAX_TASK_GFX_OBJECTS; ++i)
    {
        kgfx_obj *obj = 0;
        if (!G_current_task->gfx_objects[i].used)
            continue;
        obj = kgfx_obj_ref(G_current_task->gfx_objects[i].handle);
        if (obj && obj->kind == KGFX_OBJ_IMAGE && obj->u.image.argb == slot->image.px)
            kgfx_image_touch(G_current_task->gfx_objects[i].handle);
    }
    return 0;
}

static kwindow_handle sacx_task_busy_window(sacx_task *task)
{
    kwindow_handle none = {-1};
    if (!task)
        return none;
    for (uint32_t pass = 0u; pass < 2u; ++pass)
    {
        for (uint32_t i = 0u; i < SACX_MAX_TASK_WINDOWS; ++i)
        {
            if (!task->windows[i].used || !kwindow_visible(task->windows[i].handle))
                continue;
            if (pass == 0u && !kwindow_focused(task->windows[i].handle))
                continue;
            return task->windows[i].handle;
        }
    }
    return none;
}

static int sacx_api_img_save(uint32_t image_handle, const char *path, uint32_t format, uint32_t quality)
{
    sacx_image_slot *slot = sacx_image_from_handle(G_current_task, image_handle);
    kwindow_handle busy_window;
    int rc;
    if (!slot || !slot->image.px || !path || !path[0])
        return -1;
    busy_window = sacx_task_busy_window(G_current_task);
    if (busy_window.idx >= 0)
        kbusy_begin_window(busy_window);
    else
        kbusy_begin();
    rc = kimg_save(&slot->image, path, format, quality);
    kbusy_end();
    return rc;
}

static void sacx_async_image_save_worker(void *ctx)
{
    sacx_async_image_save_slot *slot = (sacx_async_image_save_slot *)ctx;
    if (!slot || !slot->used || !slot->image.px || !slot->path[0])
        return;
    slot->state.stage = 1u;
    asm_dma_clean_range(&slot->state, sizeof(slot->state));
    slot->state.stage = 2u;
    asm_dma_clean_range(&slot->state, sizeof(slot->state));
    slot->state.result = kimg_encode_to_buffer(&slot->image, slot->format, slot->quality,
                                         slot->encoded_data, slot->encoded_pages << 12,
                                         (uint32_t *)&slot->state.encoded_size);
    slot->state.stage = slot->state.result == 0 ? 3u : 5u;
    if (slot->state.result == 0 && slot->encoded_data && slot->state.encoded_size)
        asm_dma_clean_range(slot->encoded_data, slot->state.encoded_size);
    asm_dma_clean_range(&slot->state, sizeof(slot->state));
}

static uint32_t sacx_async_image_save_write_step(sacx_async_image_save_slot *slot)
{
    /* Keep the main-core portion brief: each call is made from an app update
       and must yield back to painting/input between USB-backed writes. */
    uint32_t budget = 16u * 1024u;

    if (!slot || !slot->used)
        return KWORK_STATUS_EMPTY;
    if (slot->state.result != 0)
        return KWORK_STATUS_DONE;
    if (!slot->encoded_data || !slot->state.encoded_size)
    {
        slot->state.result = -1;
        return KWORK_STATUS_DONE;
    }
    if (!slot->write_open)
    {
        if (kfile_open(&slot->write_file, slot->path, KFILE_WRITE | KFILE_CREATE | KFILE_TRUNC) != 0)
        {
            slot->state.result = -1;
            return KWORK_STATUS_DONE;
        }
        slot->write_open = 1u;
        slot->write_offset = 0u;
    }

    while (budget && slot->write_offset < slot->state.encoded_size)
    {
        uint32_t written = 0u;
        uint32_t chunk = slot->state.encoded_size - slot->write_offset;
        if (chunk > 16384u)
            chunk = 16384u;
        if (chunk > budget)
            chunk = budget;
        if (kfile_write(&slot->write_file, slot->encoded_data + slot->write_offset,
                        chunk, &written) != 0 || written == 0u)
        {
            kfile_close(&slot->write_file);
            slot->write_open = 0u;
            slot->state.result = -1;
            return KWORK_STATUS_DONE;
        }
        slot->write_offset += written;
        budget -= written;
    }

    if (slot->write_offset < slot->state.encoded_size)
        return KWORK_STATUS_RUNNING;

    kfile_close(&slot->write_file);
    slot->write_open = 0u;
    slot->file_written = 1u;
    return KWORK_STATUS_DONE;
}

static int sacx_api_img_save_async(uint32_t image_handle, const char *path, uint32_t format,
                                   uint32_t quality, uint32_t *out_save_id)
{
    sacx_image_slot *source = sacx_image_from_handle(G_current_task, image_handle);
    sacx_async_image_save_slot *slot = 0;
    uint64_t bytes = 0u;
    uint64_t pages = 0u;
    uint32_t *pixels = 0;
    uint32_t encoded_bound = 0u;
    uint32_t job_id = 0u;

    if (out_save_id)
        *out_save_id = 0u;
    if (!G_current_task || !source || !source->image.px || !path || !path[0] || !out_save_id)
        return -1;
    if (!source->image.w || !source->image.h)
        return -1;

    slot = &G_current_task->image_saves[0];
    if (slot->used)
    {
        uint32_t status = kwork_status(slot->job_id);
        if (status == KWORK_STATUS_QUEUED || status == KWORK_STATUS_RUNNING)
            return -2;
        sacx_async_image_save_release_slot(slot);
    }

    *slot = (sacx_async_image_save_slot){0};
    slot->used = 1u;
    slot->id = 1u;
    slot->format = format;
    slot->quality = quality;
    slot->state.result = -1;
    bytes = (uint64_t)source->image.w * (uint64_t)source->image.h * 4ull;
    pages = (bytes + 4095ull) >> 12;
    if (!pages)
    {
        sacx_async_image_save_release_slot(slot);
        return -1;
    }
    pixels = (uint32_t *)pmem_alloc_pages(pages);
    if (!pixels)
    {
        sacx_async_image_save_release_slot(slot);
        return -3;
    }
    memcpy(pixels, source->image.px, (size_t)bytes);
    asm_dma_clean_range(pixels, bytes);
    slot->image.px = pixels;
    slot->image.w = source->image.w;
    slot->image.h = source->image.h;
    sacx_copy_trunc(slot->path, sizeof(slot->path), path);

    encoded_bound = kimg_encode_bound(&slot->image, format);
    if (!encoded_bound)
    {
        sacx_async_image_save_release_slot(slot);
        return -1;
    }
    slot->encoded_pages = ((uint64_t)encoded_bound + 4095ull) >> 12;
    slot->encoded_data = (uint8_t *)pmem_alloc_pages(slot->encoded_pages);
    if (!slot->encoded_data)
    {
        sacx_async_image_save_release_slot(slot);
        return -3;
    }
    asm_dma_clean_range(slot->encoded_data, slot->encoded_pages << 12);
    asm_dma_clean_range(slot, sizeof(*slot));
    if (kwork_submit(sacx_async_image_save_worker, slot, KWORK_SUBMIT_REQUIRE_REMOTE, &job_id) != 0 || !job_id)
    {
        sacx_async_image_save_release_slot(slot);
        return -1;
    }
    slot->job_id = job_id;
    *out_save_id = slot->id;
    return 0;
}

static uint32_t sacx_api_img_save_status(uint32_t save_id, int *out_result)
{
    sacx_async_image_save_slot *slot;
    uint32_t status;
    int result_hint = -1;
    if (out_result)
        *out_result = -1;
    if (!G_current_task || save_id == 0u || save_id > SACX_MAX_TASK_ASYNC_IMAGE_SAVES)
        return KWORK_STATUS_EMPTY;
    slot = &G_current_task->image_saves[save_id - 1u];
    if (!slot->used)
        return KWORK_STATUS_EMPTY;
    status = slot->job_id ? kwork_status(slot->job_id) : KWORK_STATUS_DONE;
    if (status == KWORK_STATUS_RUNNING)
    {
        asm_dma_invalidate_range(&slot->state, sizeof(slot->state));
        result_hint = -10 - (int)slot->state.stage;
    }
    if (status == KWORK_STATUS_DONE)
    {
        if (!slot->encoded_ready)
        {
            asm_dma_invalidate_range(&slot->state, sizeof(slot->state));
            slot->encoded_ready = 1u;
        }
        asm_dma_invalidate_range(&slot->state, sizeof(slot->state));
        if (slot->state.result == 0 && !slot->file_written)
        {
            if (slot->encoded_data && slot->state.encoded_size)
                asm_dma_invalidate_range(slot->encoded_data, slot->state.encoded_size);
            status = sacx_async_image_save_write_step(slot);
            if (status == KWORK_STATUS_RUNNING)
                result_hint = -2;
        }
    }
    if (out_result && (status == KWORK_STATUS_DONE || status == KWORK_STATUS_FAILED))
        *out_result = slot->state.result;
    else if (out_result && status == KWORK_STATUS_RUNNING)
        *out_result = result_hint;
    return status;
}

static int sacx_api_img_save_release(uint32_t save_id)
{
    sacx_async_image_save_slot *slot;
    uint32_t status;
    if (!G_current_task || save_id == 0u || save_id > SACX_MAX_TASK_ASYNC_IMAGE_SAVES)
        return -1;
    slot = &G_current_task->image_saves[save_id - 1u];
    if (!slot->used)
        return 0;
    status = kwork_status(slot->job_id);
    if (status == KWORK_STATUS_QUEUED || status == KWORK_STATUS_RUNNING)
        return -2;
    sacx_async_image_save_release_slot(slot);
    return 0;
}

static int sacx_api_img_draw_text(uint32_t image_handle, int32_t x, int32_t y, const char *text,
                                  sacx_color color, uint8_t alpha, uint32_t scale)
{
    sacx_image_slot *slot = sacx_image_from_handle(G_current_task, image_handle);
    if (!slot || !slot->image.px || !G_runtime_font || !text || !scale)
        return -1;
    if (kgfx_target_argb_begin(slot->image.px, slot->image.w, slot->image.h, slot->image.w) != 0)
        return -1;
    ktext_draw_str_ex(G_runtime_font, x, y, text, sacx_to_kcolor(color), alpha, scale, 0, 0);
    kgfx_target_argb_end();
    return sacx_api_img_touch(image_handle);
}

static int sacx_api_img_clipboard_set(uint32_t image_handle)
{
    sacx_image_slot *slot = sacx_image_from_handle(G_current_task, image_handle);
    if (!slot || !slot->image.px)
        return -1;
    return kimage_clipboard_set(&slot->image);
}

static int sacx_api_img_clipboard_get(uint32_t *out_image_handle)
{
    kimg image = {0};
    uint64_t bytes = 0u;
    uint64_t pages = 0u;

    if (!G_current_task || !out_image_handle || kimage_clipboard_copy(&image) != 0)
        return -1;
    bytes = (uint64_t)image.w * image.h * 4ull;
    if (sacx_task_image_bytes(G_current_task) + bytes > 256ull * 1024ull * 1024ull ||
        sacx_api_img_register_loaded(&image, out_image_handle) != 0)
    {
        pages = (bytes + 4095ull) >> 12;
        if (image.px && pages)
            pmem_free_pages(image.px, pages);
        return -1;
    }
    return 0;
}

static int sacx_api_k3d_scene_create(const sacx3d_viewport_desc *desc, uint32_t *out_scene_handle, uint32_t *out_obj_handle)
{
    k3d_viewport_desc native;
    k3d_scene_handle scene;
    kgfx_obj_handle obj;
    sacx_gfx_slot *parent = 0;
    uint32_t app_obj_handle = 0u;

    if (!G_current_task || !desc || !out_scene_handle || !out_obj_handle)
        return -1;
    parent = sacx_gfx_from_handle(G_current_task, desc->parent_obj_handle);
    if (!parent)
        return -1;

    native.parent = parent->handle;
    native.x = desc->x;
    native.y = desc->y;
    native.z = desc->z;
    native.w = desc->w;
    native.h = desc->h;
    native.internal_w = desc->internal_w;
    native.internal_h = desc->internal_h;

    if (k3d_scene_create(&native, &scene, &obj) != 0)
        return -1;
    if (sacx_gfx_register_existing(G_current_task, obj, &app_obj_handle) != 0)
    {
        (void)k3d_scene_destroy(scene);
        return -1;
    }

    for (uint32_t i = 0u; i < SACX_MAX_TASK_3D_SCENES; ++i)
    {
        if (G_current_task->scenes3d[i].used)
            continue;
        G_current_task->scenes3d[i].used = 1u;
        G_current_task->scenes3d[i].handle = scene;
        G_current_task->scenes3d[i].root_obj_handle = app_obj_handle;
        *out_scene_handle = i + 1u;
        *out_obj_handle = app_obj_handle;
        return 0;
    }

    G_current_task->gfx_objects[app_obj_handle - 1u].used = 0u;
    G_current_task->gfx_objects[app_obj_handle - 1u].handle.idx = -1;
    (void)k3d_scene_destroy(scene);
    return -1;
}

static int sacx_api_k3d_scene_destroy(uint32_t scene_handle)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    if (!slot)
        return -1;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_3D_PLAYERS; ++i)
    {
        if (G_current_task->players3d[i].used && G_current_task->players3d[i].scene_handle == scene_handle)
        {
            (void)k3d_player_destroy(G_current_task->players3d[i].handle);
            G_current_task->players3d[i].used = 0u;
            G_current_task->players3d[i].handle.idx = -1;
            G_current_task->players3d[i].scene_handle = 0u;
            G_current_task->players3d[i].window_handle = 0u;
        }
    }
    if (slot->root_obj_handle > 0u && slot->root_obj_handle <= SACX_MAX_TASK_GFX_OBJECTS)
    {
        G_current_task->gfx_objects[slot->root_obj_handle - 1u].used = 0u;
        G_current_task->gfx_objects[slot->root_obj_handle - 1u].handle.idx = -1;
    }
    (void)k3d_scene_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    slot->root_obj_handle = 0u;
    return 0;
}

static int sacx_api_k3d_scene_render(uint32_t scene_handle)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    if (!slot)
        return -1;
    return k3d_scene_render(slot->handle);
}

static int sacx_api_k3d_scene_resize(uint32_t scene_handle, uint32_t viewport_w, uint32_t viewport_h,
                                     uint32_t internal_w, uint32_t internal_h)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    if (!slot)
        return -1;
    return k3d_scene_resize(slot->handle, viewport_w, viewport_h, internal_w, internal_h);
}

static int sacx_api_k3d_scene_root_obj(uint32_t scene_handle, uint32_t *out_obj_handle)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    if (!slot || !out_obj_handle)
        return -1;
    *out_obj_handle = slot->root_obj_handle;
    return 0;
}

static int sacx_api_k3d_scene_set_camera(uint32_t scene_handle, const sacx3d_camera *camera)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_camera native;
    if (!slot || !camera)
        return -1;
    sacx_to_k3d_camera(camera, &native);
    return k3d_scene_set_camera(slot->handle, &native);
}

static int sacx_api_k3d_scene_get_camera(uint32_t scene_handle, sacx3d_camera *out_camera)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_camera native;
    if (!slot || !out_camera)
        return -1;
    if (k3d_scene_get_camera(slot->handle, &native) != 0)
        return -1;
    k3d_to_sacx_camera(&native, out_camera);
    return 0;
}

static int sacx_api_k3d_scene_set_ambient(uint32_t scene_handle, sacx_color color, float intensity)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    if (!slot)
        return -1;
    return k3d_scene_set_ambient(slot->handle, sacx_to_kcolor(color), intensity);
}

static int sacx_api_k3d_scene_set_directional_light(uint32_t scene_handle, sacx3d_vec3 dir, sacx_color color, float intensity)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    if (!slot)
        return -1;
    return k3d_scene_set_directional_light(slot->handle, sacx_to_k3d_vec3(dir), sacx_to_kcolor(color), intensity);
}

static int sacx_api_k3d_scene_clear_point_lights(uint32_t scene_handle)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    if (!slot)
        return -1;
    return k3d_scene_clear_point_lights(slot->handle);
}

static int sacx_api_k3d_scene_add_point_light(uint32_t scene_handle, const sacx3d_point_light *light, uint32_t *out_light)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_point_light native;
    if (!slot || !light || !out_light)
        return -1;
    sacx_to_k3d_point_light(light, &native);
    return k3d_scene_add_point_light(slot->handle, &native, out_light);
}

static int sacx_api_k3d_scene_set_point_light(uint32_t scene_handle, uint32_t light_idx, const sacx3d_point_light *light)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_point_light native;
    if (!slot || !light)
        return -1;
    sacx_to_k3d_point_light(light, &native);
    return k3d_scene_set_point_light(slot->handle, light_idx, &native);
}

static int sacx_api_k3d_scene_set_fog(uint32_t scene_handle, const sacx3d_fog *fog)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_fog native;
    if (!slot || !fog)
        return -1;
    sacx_to_k3d_fog(fog, &native);
    return k3d_scene_set_fog(slot->handle, &native);
}

static int sacx_api_k3d_scene_new_cube(uint32_t scene_handle, float w, float h, float d,
                                       float x, float y, float z, sacx_color color, uint32_t *out_instance)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_instance_handle inst;
    if (!slot || !out_instance)
        return -1;
    if (k3d_scene_new_cube(slot->handle, w, h, d, x, y, z, sacx_to_kcolor(color), &inst) != 0)
        return -1;
    *out_instance = (uint32_t)inst.idx + 1u;
    return 0;
}

static int sacx_api_k3d_scene_load_obj(uint32_t scene_handle, const char *path,
                                       float x, float y, float z, uint32_t *out_instance)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_instance_handle inst;
    if (!slot || !path || !out_instance)
        return -1;
    if (k3d_scene_load_obj(slot->handle, path, x, y, z, &inst) != 0)
        return -1;
    *out_instance = (uint32_t)inst.idx + 1u;
    return 0;
}

static int sacx_api_k3d_scene_add_image_surface(uint32_t scene_handle, uint32_t image_handle, uint32_t face,
                                                float x, float y, float z, float w, float h,
                                                uint32_t *out_instance)
{
    sacx_3d_scene_slot *scene_slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    sacx_image_slot *image_slot = sacx_image_from_handle(G_current_task, image_handle);
    k3d_instance_handle inst;

    if (!scene_slot || !image_slot || !out_instance)
        return -1;
    if (k3d_scene_add_surface_image(scene_slot->handle, &image_slot->image, face, x, y, z, w, h, &inst) != 0)
        return -1;
    *out_instance = (uint32_t)inst.idx + 1u;
    return 0;
}

static int sacx_api_k3d_scene_add_text_surface(uint32_t scene_handle, const char *text, uint32_t face,
                                               float x, float y, float z, float w, float h,
                                               sacx_color text_color, uint8_t text_alpha,
                                               sacx_color bg_color, uint8_t bg_alpha,
                                               uint32_t scale, uint32_t *out_instance)
{
    sacx_3d_scene_slot *scene_slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_instance_handle inst;

    if (!scene_slot || !G_runtime_font || !text || !out_instance)
        return -1;
    if (k3d_scene_add_surface_text(scene_slot->handle, G_runtime_font, text, face, x, y, z, w, h,
                                   sacx_to_kcolor(text_color), text_alpha,
                                   sacx_to_kcolor(bg_color), bg_alpha,
                                   scale ? scale : 1u, &inst) != 0)
        return -1;
    *out_instance = (uint32_t)inst.idx + 1u;
    return 0;
}

static int sacx_api_k3d_instance_set_pos(uint32_t scene_handle, uint32_t instance_handle, float x, float y, float z)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_instance_handle inst;
    if (!slot || instance_handle == 0u)
        return -1;
    inst.idx = (int)(instance_handle - 1u);
    return k3d_instance_set_pos(slot->handle, inst, x, y, z);
}

static int sacx_api_k3d_instance_set_rotation(uint32_t scene_handle, uint32_t instance_handle,
                                              float pitch_deg, float yaw_deg, float roll_deg)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_instance_handle inst;
    if (!slot || instance_handle == 0u)
        return -1;
    inst.idx = (int)(instance_handle - 1u);
    return k3d_instance_set_rotation(slot->handle, inst, pitch_deg, yaw_deg, roll_deg);
}

static int sacx_api_k3d_instance_set_scale(uint32_t scene_handle, uint32_t instance_handle, float sx, float sy, float sz)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_instance_handle inst;
    if (!slot || instance_handle == 0u)
        return -1;
    inst.idx = (int)(instance_handle - 1u);
    return k3d_instance_set_scale(slot->handle, inst, sx, sy, sz);
}

static int sacx_api_k3d_instance_set_visible(uint32_t scene_handle, uint32_t instance_handle, uint32_t visible)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_instance_handle inst;
    if (!slot || instance_handle == 0u)
        return -1;
    inst.idx = (int)(instance_handle - 1u);
    return k3d_instance_set_visible(slot->handle, inst, visible);
}

static int sacx_api_k3d_instance_set_casts_shadow(uint32_t scene_handle, uint32_t instance_handle, uint32_t casts_shadow)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_instance_handle inst;
    if (!slot || instance_handle == 0u)
        return -1;
    inst.idx = (int)(instance_handle - 1u);
    return k3d_instance_set_casts_shadow(slot->handle, inst, casts_shadow);
}

static int sacx_api_k3d_player_create(uint32_t scene_handle, uint32_t window_handle,
                                      const sacx3d_player_desc *desc, uint32_t *out_player_handle)
{
    sacx_3d_scene_slot *scene = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    k3d_player_desc native;
    k3d_player_handle player;
    if (!G_current_task || !scene || !out_player_handle)
        return -1;
    if (window_handle && !sacx_window_from_handle(G_current_task, window_handle))
        return -1;

    memset(&native, 0, sizeof(native));
    if (desc)
        sacx_to_k3d_player_desc(desc, &native);
    if (k3d_player_create(scene->handle, desc ? &native : 0, &player) != 0)
        return -1;

    for (uint32_t i = 0u; i < SACX_MAX_TASK_3D_PLAYERS; ++i)
    {
        if (G_current_task->players3d[i].used)
            continue;
        G_current_task->players3d[i].used = 1u;
        G_current_task->players3d[i].handle = player;
        G_current_task->players3d[i].scene_handle = scene_handle;
        G_current_task->players3d[i].window_handle = window_handle;
        *out_player_handle = i + 1u;
        return 0;
    }

    (void)k3d_player_destroy(player);
    return -1;
}

static int sacx_api_k3d_player_destroy(uint32_t player_handle)
{
    sacx_3d_player_slot *slot = sacx_3d_player_from_handle(G_current_task, player_handle);
    if (!slot)
        return -1;
    (void)k3d_player_destroy(slot->handle);
    slot->used = 0u;
    slot->handle.idx = -1;
    slot->scene_handle = 0u;
    slot->window_handle = 0u;
    return 0;
}

static int sacx_api_k3d_player_set_free_mode(uint32_t player_handle, uint32_t free_mode)
{
    sacx_3d_player_slot *slot = sacx_3d_player_from_handle(G_current_task, player_handle);
    if (!slot)
        return -1;
    return k3d_player_set_free_mode(slot->handle, free_mode);
}

static int sacx_api_k3d_player_set_camera(uint32_t player_handle, const sacx3d_camera *camera)
{
    sacx_3d_player_slot *slot = sacx_3d_player_from_handle(G_current_task, player_handle);
    k3d_camera native;
    if (!slot || !camera)
        return -1;
    sacx_to_k3d_camera(camera, &native);
    return k3d_player_set_camera(slot->handle, &native);
}

static int sacx_api_k3d_player_get_camera(uint32_t player_handle, sacx3d_camera *out_camera)
{
    sacx_3d_player_slot *slot = sacx_3d_player_from_handle(G_current_task, player_handle);
    k3d_camera native;
    if (!slot || !out_camera)
        return -1;
    if (k3d_player_get_camera(slot->handle, &native) != 0)
        return -1;
    k3d_to_sacx_camera(&native, out_camera);
    return 0;
}

static int sacx_api_k3d_player_clear_colliders(uint32_t player_handle)
{
    sacx_3d_player_slot *slot = sacx_3d_player_from_handle(G_current_task, player_handle);
    if (!slot)
        return -1;
    return k3d_player_clear_colliders(slot->handle);
}

static int sacx_api_k3d_player_add_collider(uint32_t player_handle, const sacx3d_box *box)
{
    sacx_3d_player_slot *slot = sacx_3d_player_from_handle(G_current_task, player_handle);
    k3d_box native;
    if (!slot || !box)
        return -1;
    sacx_to_k3d_box(box, &native);
    return k3d_player_add_collider(slot->handle, &native);
}

static int sacx_api_k3d_player_update(uint32_t player_handle, float dt, uint32_t render_scene)
{
    sacx_3d_player_slot *slot = sacx_3d_player_from_handle(G_current_task, player_handle);
    sacx_3d_scene_slot *scene = 0;
    k3d_player_input input;

    if (!slot)
        return -1;
    if (slot->window_handle)
    {
        sacx_window_slot *win = sacx_window_from_handle(G_current_task, slot->window_handle);
        if (!win || !kwindow_focused(win->handle))
            return 1;
    }

    memset(&input, 0, sizeof(input));
    input.dt = dt;
    input.mouse_dx = kmouse_dx();
    input.mouse_dy = kmouse_dy();
    input.mouse_buttons = kmouse_buttons();
    input.key_w = kinput_key_down(SACX_KEY_W) ? 1u : 0u;
    input.key_a = kinput_key_down(SACX_KEY_A) ? 1u : 0u;
    input.key_s = kinput_key_down(SACX_KEY_S) ? 1u : 0u;
    input.key_d = kinput_key_down(SACX_KEY_D) ? 1u : 0u;
    input.key_up = kinput_key_down(SACX_KEY_UP) ? 1u : 0u;
    input.key_down = kinput_key_down(SACX_KEY_DOWN) ? 1u : 0u;
    input.key_left = kinput_key_down(SACX_KEY_LEFT) ? 1u : 0u;
    input.key_right = kinput_key_down(SACX_KEY_RIGHT) ? 1u : 0u;
    input.key_q = kinput_key_down(SACX_KEY_Q) ? 1u : 0u;
    input.key_e = kinput_key_down(SACX_KEY_E) ? 1u : 0u;
    if (k3d_player_update(slot->handle, &input) != 0)
        return -1;

    if (render_scene)
    {
        scene = sacx_3d_scene_from_handle(G_current_task, slot->scene_handle);
        if (!scene)
            return -1;
        return k3d_scene_render(scene->handle);
    }

    return 0;
}

static int sacx_api_k3d_scene_apply_default_world(uint32_t scene_handle)
{
    sacx_3d_scene_slot *slot = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    if (!slot)
        return -1;
    return k3d_scene_apply_default_world(slot->handle);
}

static int sacx_api_k3d_scene_add_room(uint32_t scene_handle, uint32_t player_handle, const sacx3d_room_desc *desc)
{
    sacx_3d_scene_slot *scene = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    sacx_3d_player_slot *player_slot = 0;
    k3d_player_handle player;
    k3d_room_desc native_desc;
    const k3d_room_desc *native_desc_ptr = 0;

    if (!scene)
        return -1;
    player.idx = -1;
    if (player_handle)
    {
        player_slot = sacx_3d_player_from_handle(G_current_task, player_handle);
        if (!player_slot)
            return -1;
        player = player_slot->handle;
    }
    if (desc)
    {
        sacx_to_k3d_room_desc(desc, &native_desc);
        native_desc_ptr = &native_desc;
    }
    return k3d_scene_add_room(scene->handle, player, native_desc_ptr);
}

static int sacx_api_k3d_scene_add_obstacle_cube(uint32_t scene_handle, uint32_t player_handle,
                                                float w, float h, float d,
                                                float x, float y, float z,
                                                sacx_color color, uint32_t *out_instance)
{
    sacx_3d_scene_slot *scene = sacx_3d_scene_from_handle(G_current_task, scene_handle);
    sacx_3d_player_slot *player_slot = 0;
    k3d_player_handle player;
    k3d_instance_handle inst;
    k3d_instance_handle *inst_ptr = out_instance ? &inst : 0;

    if (!scene)
        return -1;
    player.idx = -1;
    if (player_handle)
    {
        player_slot = sacx_3d_player_from_handle(G_current_task, player_handle);
        if (!player_slot)
            return -1;
        player = player_slot->handle;
    }
    if (out_instance)
        *out_instance = 0u;
    if (k3d_scene_add_obstacle_cube(scene->handle, player, w, h, d, x, y, z, sacx_to_kcolor(color), inst_ptr) != 0)
        return -1;
    if (out_instance)
        *out_instance = (uint32_t)inst.idx + 1u;
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
    task->api.gfx_obj_get_rect = sacx_api_gfx_obj_get_rect;
    task->api.gfx_obj_set_rotation_deg = sacx_api_gfx_obj_set_rotation_deg;
    task->api.gfx_obj_rotation_deg = sacx_api_gfx_obj_rotation_deg;
    task->api.gfx_obj_set_rotation_pivot = sacx_api_gfx_obj_set_rotation_pivot;
    task->api.gfx_obj_clear_rotation_pivot = sacx_api_gfx_obj_clear_rotation_pivot;
    task->api.gfx_obj_set_circle = sacx_api_gfx_obj_set_circle;
    task->api.gfx_text_set = sacx_api_gfx_text_set;
    task->api.gfx_text_set_align = sacx_api_gfx_text_set_align;
    task->api.gfx_text_set_spacing = sacx_api_gfx_text_set_spacing;
    task->api.gfx_text_set_scale = sacx_api_gfx_text_set_scale;
    task->api.gfx_text_set_pos = sacx_api_gfx_text_set_pos;
    task->api.gfx_image_set_size = sacx_api_gfx_image_set_size;
    task->api.gfx_image_set_pos = sacx_api_gfx_image_set_pos;
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
    task->api.app_arg_raw_path = sacx_api_app_arg_raw_path;
    task->api.app_arg_friendly_path = sacx_api_app_arg_friendly_path;
    task->api.dialog_open_file = sacx_api_dialog_open_file;
    task->api.dialog_active = sacx_api_dialog_active;
    task->api.window_focused = sacx_api_window_focused;

    task->api.k3d_scene_create = sacx_api_k3d_scene_create;
    task->api.k3d_scene_destroy = sacx_api_k3d_scene_destroy;
    task->api.k3d_scene_render = sacx_api_k3d_scene_render;
    task->api.k3d_scene_resize = sacx_api_k3d_scene_resize;
    task->api.k3d_scene_root_obj = sacx_api_k3d_scene_root_obj;
    task->api.k3d_scene_set_camera = sacx_api_k3d_scene_set_camera;
    task->api.k3d_scene_get_camera = sacx_api_k3d_scene_get_camera;
    task->api.k3d_scene_set_ambient = sacx_api_k3d_scene_set_ambient;
    task->api.k3d_scene_set_directional_light = sacx_api_k3d_scene_set_directional_light;
    task->api.k3d_scene_clear_point_lights = sacx_api_k3d_scene_clear_point_lights;
    task->api.k3d_scene_add_point_light = sacx_api_k3d_scene_add_point_light;
    task->api.k3d_scene_set_point_light = sacx_api_k3d_scene_set_point_light;
    task->api.k3d_scene_set_fog = sacx_api_k3d_scene_set_fog;
    task->api.k3d_scene_new_cube = sacx_api_k3d_scene_new_cube;
    task->api.k3d_scene_load_obj = sacx_api_k3d_scene_load_obj;
    task->api.k3d_scene_add_image_surface = sacx_api_k3d_scene_add_image_surface;
    task->api.k3d_scene_add_text_surface = sacx_api_k3d_scene_add_text_surface;
    task->api.k3d_instance_set_pos = sacx_api_k3d_instance_set_pos;
    task->api.k3d_instance_set_rotation = sacx_api_k3d_instance_set_rotation;
    task->api.k3d_instance_set_scale = sacx_api_k3d_instance_set_scale;
    task->api.k3d_instance_set_visible = sacx_api_k3d_instance_set_visible;
    task->api.k3d_instance_set_casts_shadow = sacx_api_k3d_instance_set_casts_shadow;
    task->api.k3d_player_create = sacx_api_k3d_player_create;
    task->api.k3d_player_destroy = sacx_api_k3d_player_destroy;
    task->api.k3d_player_set_free_mode = sacx_api_k3d_player_set_free_mode;
    task->api.k3d_player_set_camera = sacx_api_k3d_player_set_camera;
    task->api.k3d_player_get_camera = sacx_api_k3d_player_get_camera;
    task->api.k3d_player_clear_colliders = sacx_api_k3d_player_clear_colliders;
    task->api.k3d_player_add_collider = sacx_api_k3d_player_add_collider;
    task->api.k3d_player_update = sacx_api_k3d_player_update;
    task->api.k3d_scene_apply_default_world = sacx_api_k3d_scene_apply_default_world;
    task->api.k3d_scene_add_room = sacx_api_k3d_scene_add_room;
    task->api.k3d_scene_add_obstacle_cube = sacx_api_k3d_scene_add_obstacle_cube;

    task->api.img_create = sacx_api_img_create;
    task->api.img_clone = sacx_api_img_clone;
    task->api.img_pixels = sacx_api_img_pixels;
    task->api.img_touch = sacx_api_img_touch;
    task->api.img_save = sacx_api_img_save;
    task->api.img_save_async = sacx_api_img_save_async;
    task->api.img_save_status = sacx_api_img_save_status;
    task->api.img_save_release = sacx_api_img_save_release;
    task->api.img_draw_text = sacx_api_img_draw_text;
    task->api.img_clipboard_set = sacx_api_img_clipboard_set;
    task->api.img_clipboard_get = sacx_api_img_clipboard_get;
    task->api.app_arg_image = sacx_api_app_arg_image;
    task->api.dialog_save_file = sacx_api_dialog_save_file;
    task->api.window_set_close_deferred = sacx_api_window_set_close_deferred;
    task->api.window_close_requested = sacx_api_window_close_requested;
    task->api.window_close_accept = sacx_api_window_close_accept;
    task->api.window_close_cancel = sacx_api_window_close_cancel;
    task->api.textbox_select = sacx_api_textbox_select;
    task->api.textbox_selection = sacx_api_textbox_selection;
    task->api.textbox_copy_selection = sacx_api_textbox_copy_selection;
    task->api.textbox_cut_selection = sacx_api_textbox_cut_selection;
    task->api.textbox_paste = sacx_api_textbox_paste;
    task->api.textbox_undo = sacx_api_textbox_undo;
    task->api.textbox_redo = sacx_api_textbox_redo;
    task->api.textbox_set_max_len = sacx_api_textbox_set_max_len;
    task->api.textbox_max_len = sacx_api_textbox_max_len;
    task->api.window_set_modal_child = sacx_api_window_set_modal_child;
    task->api.window_clear_modal_child = sacx_api_window_clear_modal_child;
    task->api.window_has_active_modal = sacx_api_window_has_active_modal;
    task->api.window_center_on_parent = sacx_api_window_center_on_parent;
    task->api.dialog_open_file_for_window = sacx_api_dialog_open_file_for_window;
    task->api.dialog_save_file_for_window = sacx_api_dialog_save_file_for_window;
    task->api.ui_view_create_rect = sacx_api_ui_view_create_rect;
    task->api.ui_window_view_create = sacx_api_ui_window_view_create;
    task->api.ui_view_destroy = sacx_api_ui_view_destroy;
    task->api.ui_view_root = sacx_api_ui_view_root;
    task->api.ui_view_add_obj = sacx_api_ui_view_add_obj;
    task->api.ui_view_set_state = sacx_api_ui_view_set_state;
    task->api.ui_view_state = sacx_api_ui_view_state;
    task->api.ui_view_set_visible = sacx_api_ui_view_set_visible;
    task->api.ui_view_set_layout = sacx_api_ui_view_set_layout;
    task->api.ui_view_apply_layout = sacx_api_ui_view_apply_layout;
    task->api.ui_view_set_bounds = sacx_api_ui_view_set_bounds;
    task->api.ui_dropdown_create = sacx_api_ui_dropdown_create;
    task->api.ui_dropdown_destroy = sacx_api_ui_dropdown_destroy;
    task->api.ui_dropdown_root = sacx_api_ui_dropdown_root;
    task->api.ui_dropdown_selected = sacx_api_ui_dropdown_selected;
    task->api.ui_dropdown_set_selected = sacx_api_ui_dropdown_set_selected;
    task->api.ui_dropdown_set_enabled = sacx_api_ui_dropdown_set_enabled;
    task->api.ui_radio_create = sacx_api_ui_radio_create;
    task->api.ui_radio_destroy = sacx_api_ui_radio_destroy;
    task->api.ui_radio_root = sacx_api_ui_radio_root;
    task->api.ui_radio_selected = sacx_api_ui_radio_selected;
    task->api.ui_radio_set_selected = sacx_api_ui_radio_set_selected;
    task->api.ui_radio_set_enabled = sacx_api_ui_radio_set_enabled;
    task->api.ui_toggle_create = sacx_api_ui_toggle_create;
    task->api.ui_toggle_destroy = sacx_api_ui_toggle_destroy;
    task->api.ui_toggle_root = sacx_api_ui_toggle_root;
    task->api.ui_toggle_checked = sacx_api_ui_toggle_checked;
    task->api.ui_toggle_set_checked = sacx_api_ui_toggle_set_checked;
    task->api.ui_toggle_set_enabled = sacx_api_ui_toggle_set_enabled;
    task->api.work_submit = sacx_api_work_submit;
    task->api.work_status = sacx_api_work_status;
    task->api.work_wait = sacx_api_work_wait;
    task->api.work_cancel = sacx_api_work_cancel;
    task->api.net_request_start = sacx_api_net_request_start;
    task->api.net_request_status = sacx_api_net_request_status;
    task->api.net_response_info = sacx_api_net_response_info;
    task->api.net_response_read = sacx_api_net_response_read;
    task->api.net_request_cancel = sacx_api_net_request_cancel;
    task->api.net_request_release = sacx_api_net_request_release;
    task->api.img_load_memory = sacx_api_img_load_memory;
    task->api.mem_alloc = sacx_api_mem_alloc;
    task->api.mem_free = sacx_api_mem_free;
    task->api.net_request_start_ex = sacx_api_net_request_start_ex;
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

extern "C" int sacx_runtime_launch_ex(const char *raw_path,
                                      const char *friendly_path,
                                      const char *arg_raw_path,
                                      const char *arg_friendly_path,
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
    {
        terminal_warn("sacx: no free task slot");
        return -1;
    }

    sacx_task_reset(task);
    task->task_id = G_next_task_id++;
    if (!G_next_task_id)
        G_next_task_id = 1u;
    task->state = SACX_TASK_READY;
    task->wake_tick = dihos_time_ticks();
    if (io)
        task->io = *io;
    sacx_copy_trunc(task->launch_arg_raw, sizeof(task->launch_arg_raw), arg_raw_path ? arg_raw_path : "");
    sacx_copy_trunc(task->launch_arg_friendly, sizeof(task->launch_arg_friendly),
                    arg_friendly_path ? arg_friendly_path : "");

    if (sacx_read_file_all(raw_path, &file_data, &file_size) != 0)
    {
        terminal_print_inline("sacx: app file read failed rc=");
        terminal_print_inline_hex32((uint32_t)kfile_last_result());
        terminal_print("");
        sacx_task_reset(task);
        return -1;
    }

    terminal_print_inline("sacx: app file bytes=");
    terminal_print_inline_hex32(file_size);
    terminal_print("");

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
        ksb_puts(&b, " [");
        ksb_puts(&b, sacx_arch_name(task->loaded_arch));
        ksb_puts(&b, task->loaded_from_fat ? " fat]" : " legacy]");
        sacx_task_log(task, msg);
    }

    return 0;
}

extern "C" int sacx_runtime_launch(const char *raw_path,
                                   const char *friendly_path,
                                   const sacx_runtime_io *io,
                                   uint32_t *out_task_id)
{
    return sacx_runtime_launch_ex(raw_path, friendly_path, 0, 0, io, out_task_id);
}

extern "C" int sacx_runtime_launch_image(const char *raw_path,
                                         const char *friendly_path,
                                         const kimg *image,
                                         const sacx_runtime_io *io,
                                         uint32_t *out_task_id)
{
    uint32_t task_id = 0u;
    uint32_t image_handle = 0u;
    sacx_task *task = 0;

    if (!image || !image->px || !image->w || !image->h)
        return -1;
    if (sacx_runtime_launch_ex(raw_path, friendly_path, 0, 0, io, &task_id) != 0)
        return -1;

    task = sacx_find_task_by_id(task_id);
    if (!task || sacx_task_register_image_copy(task, image, &image_handle) != 0)
    {
        if (task)
        {
            sacx_task_finish(task, -1, "startup image allocation failed", SACX_TASK_FAULTED);
            sacx_task_reset(task);
        }
        return -1;
    }

    task->launch_arg_image = image_handle;
    if (out_task_id)
        *out_task_id = task_id;
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

static uint32_t sacx_count_windows(const sacx_task *task)
{
    uint32_t count = 0u;
    if (!task)
        return 0u;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_WINDOWS; ++i)
        if (task->windows[i].used)
            ++count;
    return count;
}

static uint32_t sacx_count_gfx_objects(const sacx_task *task)
{
    uint32_t count = 0u;
    if (!task)
        return 0u;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_GFX_OBJECTS; ++i)
        if (task->gfx_objects[i].used)
            ++count;
    return count;
}

static uint32_t sacx_count_images(const sacx_task *task)
{
    uint32_t count = 0u;
    if (!task)
        return 0u;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_IMAGES; ++i)
        if (task->images[i].used)
            ++count;
    return count;
}

static uint32_t sacx_count_memory_bytes(const sacx_task *task)
{
    uint64_t bytes = 0u;
    if (!task)
        return 0u;
    for (uint32_t i = 0u; i < SACX_MAX_TASK_MEMORY_ALLOCS; ++i)
    {
        const sacx_memory_slot *slot = &task->memory_allocs[i];
        if (slot->used)
            bytes += slot->pages << 12;
    }
    return bytes > 0xffffffffull ? 0xffffffffu : (uint32_t)bytes;
}

extern "C" uint32_t sacx_runtime_task_snapshot(sacx_task_info *out_items, uint32_t max_items)
{
    uint32_t written = 0u;

    if (!out_items || max_items == 0u)
        return 0u;

    for (uint32_t i = 0u; i < SACX_MAX_TASKS && written < max_items; ++i)
    {
        sacx_task *task = &G_tasks[i];
        sacx_task_info *out = 0;

        if (task->state == SACX_TASK_UNUSED)
            continue;

        out = &out_items[written++];
        memset(out, 0, sizeof(*out));
        out->task_id = task->task_id;
        out->state = task->state;
        out->state_age = task->state_age;
        out->exit_status = task->exit_status;
        out->wake_tick = task->wake_tick;
        out->arena_size = task->arena_size;
        out->image_size = task->image_size;
        out->memory_size = sacx_count_memory_bytes(task);
        out->loaded_arch = task->loaded_arch;
        out->window_count = sacx_count_windows(task);
        out->gfx_count = sacx_count_gfx_objects(task);
        out->image_count = sacx_count_images(task);
        out->preemptions = task->preemptions;
        sacx_copy_trunc(out->friendly_path, sizeof(out->friendly_path), task->friendly_path);
        sacx_copy_trunc(out->message, sizeof(out->message), task->exit_message);
    }

    return written;
}

extern "C" int sacx_runtime_task_cancel(uint32_t task_id, int32_t status, const char *message)
{
    sacx_task *task = sacx_find_task_by_id(task_id);

    if (!task)
        return -1;

    if (task->state == SACX_TASK_EXITED || task->state == SACX_TASK_FAULTED)
        return 0;

    sacx_task_finish(task, status, message ? message : "closed", SACX_TASK_EXITED);
    return 0;
}

extern "C" int sacx_runtime_task_release(uint32_t task_id)
{
    sacx_task *task = sacx_find_task_by_id(task_id);
    if (!task)
        return -1;
    if (task->state != SACX_TASK_EXITED && task->state != SACX_TASK_FAULTED)
        return -1;
    if (sacx_task_worker_active(task))
        return -2;
    if (task->arena)
    {
        pmem_free_executable_pages(task->arena, task->arena_size / 4096u);
        task->arena = 0;
    }
    sacx_task_destroy_net_requests(task);
    sacx_task_reset(task);
    return 0;
}
