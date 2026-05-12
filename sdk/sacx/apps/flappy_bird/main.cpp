#include "sacx_api.h"

static const sacx_api *g_api = 0;

static const char *BIRD_PATH = "0:/Documents/Games/flappy bird/bird.png";
static const char *PIPE_PATH = "0:/Documents/Games/flappy bird/pipe.png";

static uint32_t g_window = 0;
static uint32_t g_root = 0;
static uint32_t g_sky = 0;
static uint32_t g_ground = 0;
static uint32_t g_ground_line = 0;
static uint32_t g_score_text = 0;
static uint32_t g_message_text = 0;
static uint32_t g_asset_text = 0;

static uint32_t g_bird_img = 0;
static uint32_t g_pipe_img = 0;
static uint32_t g_bird_obj = 0;
static uint32_t g_bird_body = 0;
static uint32_t g_bird_wing = 0;
static uint32_t g_bird_eye = 0;

static uint32_t g_pipe_top_img[3];
static uint32_t g_pipe_bottom_img[3];
static uint32_t g_pipe_top_rect[3];
static uint32_t g_pipe_bottom_rect[3];
static uint32_t g_pipe_top_lip[3];
static uint32_t g_pipe_bottom_lip[3];

static uint32_t g_cloud[9];
static uint32_t g_cloud_count = 0;

static uint32_t g_root_w = 1;
static uint32_t g_root_h = 1;
static int32_t g_play_top = 42;
static int32_t g_ground_y = 420;
static int32_t g_bird_x = 120;
static int32_t g_bird_y_fp = 0;
static int32_t g_bird_vy = 0;
static int32_t g_pipe_x[3];
static int32_t g_gap_y[3];
static uint8_t g_pipe_scored[3];
static uint32_t g_score = 0;
static uint32_t g_best = 0;
static uint32_t g_frame = 0;
static uint32_t g_rng = 0xC0FFEE42u;
static uint8_t g_prev_mouse_buttons = 0;
static uint8_t g_started = 0;
static uint8_t g_game_over = 0;
static uint8_t g_paused = 0;
static uint8_t g_assets_loaded = 0;

#define FP_SHIFT 8
#define FP_ONE (1 << FP_SHIFT)
#define PIPE_COUNT 3
#define BIRD_W 44
#define BIRD_H 34
#define BIRD_HIT_PAD 7
#define PIPE_W 78
#define PIPE_LIP_H 18
#define GAP_H 158
#define GROUND_H 58
#define PIPE_SPEED 2
#define GRAVITY 34
#define FLAP_VELOCITY -920

static sacx_color rgb(uint8_t r, uint8_t g, uint8_t b)
{
    sacx_color c;
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
}

static void copy_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t i = 0;

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

static void append_text(char *dst, uint32_t cap, const char *src)
{
    uint32_t len = 0;

    if (!dst || cap == 0u)
        return;
    while (len + 1u < cap && dst[len])
        ++len;
    copy_text(dst + len, cap - len, src);
}

static void append_uint(char *dst, uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;

    if (value == 0u)
    {
        append_text(dst, cap, "0");
        return;
    }

    while (value && n < sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n)
    {
        char one[2];
        one[0] = tmp[--n];
        one[1] = 0;
        append_text(dst, cap, one);
    }
}

static uint32_t rng_next(void)
{
    g_rng = g_rng * 1664525u + 1013904223u + g_frame;
    return g_rng;
}

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void set_text(uint32_t obj, const char *text)
{
    if (g_api && obj)
        (void)g_api->gfx_text_set(obj, text);
}

static void update_score_text(void)
{
    char line[80];

    line[0] = 0;
    append_text(line, sizeof(line), "score ");
    append_uint(line, sizeof(line), g_score);
    append_text(line, sizeof(line), "    best ");
    append_uint(line, sizeof(line), g_best);
    set_text(g_score_text, line);
}

static void update_message(void)
{
    if (g_paused)
        set_text(g_message_text, "PAUSED");
    else if (g_game_over)
        set_text(g_message_text, "GAME OVER");
    else if (!g_started)
        set_text(g_message_text, "TAP TO START");
    else
        set_text(g_message_text, "");
}

static void update_asset_text(void)
{
    if (!g_asset_text)
        return;
    set_text(g_asset_text, g_assets_loaded ? "" : "missing bird.png or pipe.png");
}

static int32_t random_gap_y(void)
{
    int32_t min_y = g_play_top + 74;
    int32_t max_y = g_ground_y - GAP_H - 74;
    uint32_t span = 1u;

    if (max_y < min_y)
        max_y = min_y;

    span = (uint32_t)(max_y - min_y + 1);
    return min_y + (int32_t)(rng_next() % span);
}

static void init_pipe(uint32_t idx, int32_t x)
{
    if (idx >= PIPE_COUNT)
        return;
    g_pipe_x[idx] = x;
    g_gap_y[idx] = random_gap_y();
    g_pipe_scored[idx] = 0u;
}

static void reset_game(void)
{
    uint32_t span = (g_root_w > 320u) ? ((g_root_w - 120u) / PIPE_COUNT) : 170u;

    if (span < 170u)
        span = 170u;

    g_score = 0;
    g_started = 0;
    g_game_over = 0;
    g_paused = 0;
    g_bird_y_fp = ((g_ground_y + g_play_top) / 2 - BIRD_H / 2) * FP_ONE;
    g_bird_vy = 0;

    for (uint32_t i = 0; i < PIPE_COUNT; ++i)
        init_pipe(i, (int32_t)g_root_w + 130 + (int32_t)(i * span));

    update_score_text();
    update_message();
}

static void flap(void)
{
    if (g_game_over)
    {
        reset_game();
        g_started = 1u;
    }
    else if (!g_started)
    {
        g_started = 1u;
    }

    g_bird_vy = FLAP_VELOCITY;
    update_message();
}

static void set_obj_visible(uint32_t obj, uint32_t visible)
{
    if (g_api && obj)
        (void)g_api->gfx_obj_set_visible(obj, visible);
}

static void sync_pipe_pair(uint32_t idx)
{
    int32_t x = g_pipe_x[idx];
    int32_t top_h = g_gap_y[idx] - g_play_top;
    int32_t bottom_y = g_gap_y[idx] + GAP_H;
    int32_t bottom_h = g_ground_y - bottom_y;
    uint32_t visible = (x + PIPE_W > 0 && x < (int32_t)g_root_w) ? 1u : 0u;

    if (top_h < 0)
        top_h = 0;
    if (bottom_h < 0)
        bottom_h = 0;

    if (g_pipe_img)
    {
        if (g_pipe_top_img[idx])
        {
            set_obj_visible(g_pipe_top_img[idx], visible && top_h > 0);
            (void)g_api->gfx_image_set_pos(g_pipe_top_img[idx], x, g_play_top);
            (void)g_api->gfx_image_set_size(g_pipe_top_img[idx], PIPE_W, (uint32_t)top_h);
        }
        if (g_pipe_bottom_img[idx])
        {
            set_obj_visible(g_pipe_bottom_img[idx], visible && bottom_h > 0);
            (void)g_api->gfx_image_set_pos(g_pipe_bottom_img[idx], x, bottom_y);
            (void)g_api->gfx_image_set_size(g_pipe_bottom_img[idx], PIPE_W, (uint32_t)bottom_h);
        }
    }

    if (g_pipe_top_rect[idx])
    {
        set_obj_visible(g_pipe_top_rect[idx], visible && top_h > 0 && !g_pipe_img);
        (void)g_api->gfx_obj_set_rect(g_pipe_top_rect[idx], x, g_play_top, PIPE_W, (uint32_t)top_h);
    }
    if (g_pipe_bottom_rect[idx])
    {
        set_obj_visible(g_pipe_bottom_rect[idx], visible && bottom_h > 0 && !g_pipe_img);
        (void)g_api->gfx_obj_set_rect(g_pipe_bottom_rect[idx], x, bottom_y, PIPE_W, (uint32_t)bottom_h);
    }
    if (g_pipe_top_lip[idx])
    {
        int32_t lip_y = g_gap_y[idx] - PIPE_LIP_H;
        set_obj_visible(g_pipe_top_lip[idx], visible && !g_pipe_img);
        (void)g_api->gfx_obj_set_rect(g_pipe_top_lip[idx], x - 6, lip_y, PIPE_W + 12u, PIPE_LIP_H);
    }
    if (g_pipe_bottom_lip[idx])
    {
        set_obj_visible(g_pipe_bottom_lip[idx], visible && !g_pipe_img);
        (void)g_api->gfx_obj_set_rect(g_pipe_bottom_lip[idx], x - 6, bottom_y, PIPE_W + 12u, PIPE_LIP_H);
    }
}

static void layout_clouds(void)
{
    static const int32_t kCloudX[3] = {92, 330, 560};
    static const int32_t kCloudY[3] = {84, 126, 74};

    for (uint32_t i = 0; i < 3u && i * 3u + 2u < g_cloud_count; ++i)
    {
        int32_t x = (kCloudX[i] + (int32_t)((g_frame / 3u + i * 71u) % (g_root_w + 160u))) % (int32_t)(g_root_w + 160u) - 100;
        int32_t y = kCloudY[i];

        (void)g_api->gfx_obj_set_circle(g_cloud[i * 3u + 0u], x, y + 16, 24u);
        (void)g_api->gfx_obj_set_circle(g_cloud[i * 3u + 1u], x + 30, y + 8, 30u);
        (void)g_api->gfx_obj_set_circle(g_cloud[i * 3u + 2u], x + 65, y + 18, 22u);
    }
}

static void layout(void)
{
    int32_t rx = 0;
    int32_t ry = 0;
    uint32_t rw = 0;
    uint32_t rh = 0;

    if (!g_api || !g_root)
        return;
    if (g_api->gfx_obj_get_rect(g_root, &rx, &ry, &rw, &rh) != 0)
        return;
    (void)rx;
    (void)ry;

    if (rw < 300u)
        rw = 300u;
    if (rh < 260u)
        rh = 260u;

    g_root_w = rw;
    g_root_h = rh;
    g_play_top = 42;
    g_ground_y = (int32_t)rh - GROUND_H;
    if (g_ground_y < g_play_top + GAP_H + 80)
        g_ground_y = g_play_top + GAP_H + 80;
    g_bird_x = (rw < 420u) ? 92 : 128;

    if (g_sky)
        (void)g_api->gfx_obj_set_rect(g_sky, 0, 0, rw, (uint32_t)g_ground_y);
    if (g_ground)
        (void)g_api->gfx_obj_set_rect(g_ground, 0, g_ground_y, rw, GROUND_H);
    if (g_ground_line)
        (void)g_api->gfx_obj_set_rect(g_ground_line, 0, g_ground_y, rw, 4u);
    if (g_score_text)
        (void)g_api->gfx_text_set_pos(g_score_text, (int32_t)(rw / 2u), 14);
    if (g_message_text)
        (void)g_api->gfx_text_set_pos(g_message_text, (int32_t)(rw / 2u), (int32_t)(rh / 2u) - 18);
    if (g_asset_text)
        (void)g_api->gfx_text_set_pos(g_asset_text, (int32_t)(rw / 2u), (int32_t)rh - 28);

    layout_clouds();

    for (uint32_t i = 0; i < PIPE_COUNT; ++i)
    {
        g_gap_y[i] = clamp_i32(g_gap_y[i], g_play_top + 58, g_ground_y - GAP_H - 26);
        sync_pipe_pair(i);
    }
}

static void draw_bird(void)
{
    int32_t y = g_bird_y_fp / FP_ONE;
    int32_t angle = g_bird_vy / 42;

    angle = clamp_i32(angle, -18, 26);

    if (g_bird_obj)
    {
        set_obj_visible(g_bird_obj, 1u);
        (void)g_api->gfx_image_set_pos(g_bird_obj, g_bird_x, y);
        (void)g_api->gfx_image_set_size(g_bird_obj, BIRD_W, BIRD_H);
        (void)g_api->gfx_obj_set_rotation_pivot(g_bird_obj, BIRD_W / 2, BIRD_H / 2);
        (void)g_api->gfx_obj_set_rotation_deg(g_bird_obj, angle);
    }
    else
    {
        int32_t cx = g_bird_x + BIRD_W / 2;
        int32_t cy = y + BIRD_H / 2;
        set_obj_visible(g_bird_body, 1u);
        set_obj_visible(g_bird_wing, 1u);
        set_obj_visible(g_bird_eye, 1u);
        (void)g_api->gfx_obj_set_circle(g_bird_body, cx, cy, 18u);
        (void)g_api->gfx_obj_set_rect(g_bird_wing, cx - 14, cy + 2, 24u, 10u);
        (void)g_api->gfx_obj_set_circle(g_bird_eye, cx + 8, cy - 6, 3u);
        (void)g_api->gfx_obj_set_rotation_pivot(g_bird_wing, 12, 5);
        (void)g_api->gfx_obj_set_rotation_deg(g_bird_wing, g_started ? -12 + (int32_t)(g_frame % 8u) * 4 : -8);
    }
}

static int intersects_pipe(uint32_t idx)
{
    int32_t bird_left = g_bird_x + BIRD_HIT_PAD;
    int32_t bird_right = g_bird_x + BIRD_W - BIRD_HIT_PAD;
    int32_t bird_top = (g_bird_y_fp / FP_ONE) + BIRD_HIT_PAD;
    int32_t bird_bottom = (g_bird_y_fp / FP_ONE) + BIRD_H - BIRD_HIT_PAD;
    int32_t pipe_left = g_pipe_x[idx];
    int32_t pipe_right = g_pipe_x[idx] + PIPE_W;

    if (bird_right <= pipe_left || bird_left >= pipe_right)
        return 0;
    if (bird_top < g_gap_y[idx] || bird_bottom > g_gap_y[idx] + GAP_H)
        return 1;
    return 0;
}

static void end_game(void)
{
    g_game_over = 1u;
    g_started = 0u;
    if (g_score > g_best)
        g_best = g_score;
    update_score_text();
    update_message();
}

static void update_game(void)
{
    int32_t bird_top = 0;
    int32_t bird_bottom = 0;
    int32_t rightmost = 0;

    if (!g_started || g_game_over || g_paused)
        return;

    g_bird_vy += GRAVITY;
    if (g_bird_vy > 980)
        g_bird_vy = 980;
    g_bird_y_fp += g_bird_vy;

    rightmost = g_pipe_x[0];
    for (uint32_t i = 1; i < PIPE_COUNT; ++i)
    {
        if (g_pipe_x[i] > rightmost)
            rightmost = g_pipe_x[i];
    }

    for (uint32_t i = 0; i < PIPE_COUNT; ++i)
    {
        g_pipe_x[i] -= PIPE_SPEED;
        if (g_pipe_x[i] + PIPE_W < -10)
        {
            init_pipe(i, rightmost + 210);
            rightmost = g_pipe_x[i];
        }

        if (!g_pipe_scored[i] && g_pipe_x[i] + PIPE_W < g_bird_x)
        {
            g_pipe_scored[i] = 1u;
            ++g_score;
            if (g_score > g_best)
                g_best = g_score;
            update_score_text();
        }

        if (intersects_pipe(i))
            end_game();
    }

    bird_top = g_bird_y_fp / FP_ONE;
    bird_bottom = bird_top + BIRD_H;
    if (bird_top < g_play_top - 8 || bird_bottom > g_ground_y)
        end_game();
}

static void sync_scene(void)
{
    layout();
    draw_bird();
    for (uint32_t i = 0; i < PIPE_COUNT; ++i)
        sync_pipe_pair(i);
}

static int load_assets(void)
{
    uint32_t bird_w = 0;
    uint32_t bird_h = 0;
    uint32_t pipe_w = 0;
    uint32_t pipe_h = 0;

    if (g_api->img_load_png(BIRD_PATH, &g_bird_img) == 0 && g_bird_img)
        (void)g_api->img_size(g_bird_img, &bird_w, &bird_h);
    if (g_api->img_load_png(PIPE_PATH, &g_pipe_img) == 0 && g_pipe_img)
        (void)g_api->img_size(g_pipe_img, &pipe_w, &pipe_h);

    g_assets_loaded = (g_bird_img && g_pipe_img && bird_w && bird_h && pipe_w && pipe_h) ? 1u : 0u;
    return g_assets_loaded ? 0 : -1;
}

static int create_pipe_objects(void)
{
    for (uint32_t i = 0; i < PIPE_COUNT; ++i)
    {
        if (g_pipe_img)
        {
            if (g_api->gfx_obj_add_image_from_img(g_pipe_img, 0, 0, &g_pipe_top_img[i]) != 0)
                return -1;
            if (g_api->gfx_obj_add_image_from_img(g_pipe_img, 0, 0, &g_pipe_bottom_img[i]) != 0)
                return -1;
            (void)g_api->gfx_obj_set_parent(g_pipe_top_img[i], g_root);
            (void)g_api->gfx_obj_set_parent(g_pipe_bottom_img[i], g_root);
            (void)g_api->gfx_image_set_sample_mode(g_pipe_top_img[i], SACX_GFX_IMAGE_SAMPLE_BILINEAR);
            (void)g_api->gfx_image_set_sample_mode(g_pipe_bottom_img[i], SACX_GFX_IMAGE_SAMPLE_BILINEAR);
            (void)g_api->gfx_obj_set_z(g_pipe_top_img[i], 4);
            (void)g_api->gfx_obj_set_z(g_pipe_bottom_img[i], 4);
        }

        if (g_api->gfx_obj_add_rect(0, 0, PIPE_W, 80u, 4, rgb(46, 166, 76), 1, &g_pipe_top_rect[i]) != 0)
            return -1;
        if (g_api->gfx_obj_add_rect(0, 0, PIPE_W, 80u, 4, rgb(43, 156, 72), 1, &g_pipe_bottom_rect[i]) != 0)
            return -1;
        if (g_api->gfx_obj_add_rect(0, 0, PIPE_W + 12u, PIPE_LIP_H, 5, rgb(72, 202, 96), 1, &g_pipe_top_lip[i]) != 0)
            return -1;
        if (g_api->gfx_obj_add_rect(0, 0, PIPE_W + 12u, PIPE_LIP_H, 5, rgb(72, 202, 96), 1, &g_pipe_bottom_lip[i]) != 0)
            return -1;

        (void)g_api->gfx_obj_set_parent(g_pipe_top_rect[i], g_root);
        (void)g_api->gfx_obj_set_parent(g_pipe_bottom_rect[i], g_root);
        (void)g_api->gfx_obj_set_parent(g_pipe_top_lip[i], g_root);
        (void)g_api->gfx_obj_set_parent(g_pipe_bottom_lip[i], g_root);
        (void)g_api->gfx_obj_set_outline_rgb(g_pipe_top_rect[i], 24, 94, 46);
        (void)g_api->gfx_obj_set_outline_rgb(g_pipe_bottom_rect[i], 24, 94, 46);
        (void)g_api->gfx_obj_set_outline_width(g_pipe_top_rect[i], 2);
        (void)g_api->gfx_obj_set_outline_width(g_pipe_bottom_rect[i], 2);
        (void)g_api->gfx_obj_set_outline_rgb(g_pipe_top_lip[i], 24, 94, 46);
        (void)g_api->gfx_obj_set_outline_rgb(g_pipe_bottom_lip[i], 24, 94, 46);
        (void)g_api->gfx_obj_set_outline_width(g_pipe_top_lip[i], 2);
        (void)g_api->gfx_obj_set_outline_width(g_pipe_bottom_lip[i], 2);
    }

    return 0;
}

static int create_clouds(void)
{
    for (uint32_t i = 0; i < 9u; ++i)
    {
        if (g_api->gfx_obj_add_circle(0, 0, 20u, 1, rgb(218, 245, 252), 1, &g_cloud[g_cloud_count]) == 0)
        {
            (void)g_api->gfx_obj_set_parent(g_cloud[g_cloud_count], g_root);
            (void)g_api->gfx_obj_set_alpha(g_cloud[g_cloud_count], 188u);
            ++g_cloud_count;
        }
    }
    return 0;
}

static int create_ui(void)
{
    sacx_window_style style = sacx_window_style_default();

    style.body_fill = rgb(87, 194, 224);
    style.body_outline = rgb(46, 123, 156);
    style.titlebar_fill = rgb(40, 129, 166);
    style.title_color = rgb(244, 252, 255);
    style.close_button_style.fill = rgb(193, 75, 65);
    style.close_button_style.hover_fill = rgb(225, 92, 78);
    style.fullscreen_button_style.fill = rgb(244, 197, 74);
    style.fullscreen_button_style.hover_fill = rgb(255, 214, 96);
    style.titlebar_height = 36u;
    style.title_scale = 2u;
    style.close_glyph_scale = 2u;
    style.fullscreen_glyph_scale = 2u;

    if (g_api->window_create_ex(152, 72, 660, 540, 26, "Flappy Bird", &style, &g_window) != 0)
        return -1;
    if (g_api->window_root(g_window, &g_root) != 0)
        return -1;

    if (g_api->gfx_obj_add_rect(0, 0, 660u, 480u, 0, rgb(93, 202, 232), 1, &g_sky) != 0)
        return -1;
    if (g_api->gfx_obj_add_rect(0, 480, 660u, GROUND_H, 8, rgb(219, 178, 92), 1, &g_ground) != 0)
        return -1;
    if (g_api->gfx_obj_add_rect(0, 480, 660u, 4u, 9, rgb(118, 195, 94), 1, &g_ground_line) != 0)
        return -1;

    (void)g_api->gfx_obj_set_parent(g_sky, g_root);
    (void)g_api->gfx_obj_set_parent(g_ground, g_root);
    (void)g_api->gfx_obj_set_parent(g_ground_line, g_root);

    create_clouds();
    (void)load_assets();

    if (create_pipe_objects() != 0)
        return -1;

    if (g_bird_img)
    {
        if (g_api->gfx_obj_add_image_from_img(g_bird_img, 0, 0, &g_bird_obj) != 0)
            return -1;
        (void)g_api->gfx_obj_set_parent(g_bird_obj, g_root);
        (void)g_api->gfx_image_set_sample_mode(g_bird_obj, SACX_GFX_IMAGE_SAMPLE_BILINEAR);
        (void)g_api->gfx_obj_set_z(g_bird_obj, 7);
    }
    else
    {
        if (g_api->gfx_obj_add_circle(0, 0, 18u, 7, rgb(244, 206, 55), 1, &g_bird_body) != 0)
            return -1;
        if (g_api->gfx_obj_add_rect(0, 0, 24u, 10u, 8, rgb(255, 239, 119), 1, &g_bird_wing) != 0)
            return -1;
        if (g_api->gfx_obj_add_circle(0, 0, 3u, 9, rgb(16, 24, 30), 1, &g_bird_eye) != 0)
            return -1;
        (void)g_api->gfx_obj_set_parent(g_bird_body, g_root);
        (void)g_api->gfx_obj_set_parent(g_bird_wing, g_root);
        (void)g_api->gfx_obj_set_parent(g_bird_eye, g_root);
        (void)g_api->gfx_obj_set_outline_rgb(g_bird_body, 172, 126, 22);
        (void)g_api->gfx_obj_set_outline_width(g_bird_body, 2);
    }

    if (g_api->gfx_obj_add_text("score 0    best 0", 330, 14, 12, rgb(255, 255, 255), 255, 2, 1, 0, SACX_TEXT_ALIGN_CENTER, 1, &g_score_text) != 0)
        return -1;
    if (g_api->gfx_obj_add_text("TAP TO START", 330, 245, 12, rgb(255, 255, 255), 255, 3, 1, 0, SACX_TEXT_ALIGN_CENTER, 1, &g_message_text) != 0)
        return -1;
    if (g_api->gfx_obj_add_text("", 330, 510, 12, rgb(94, 64, 42), 255, 1, 0, 0, SACX_TEXT_ALIGN_CENTER, 1, &g_asset_text) != 0)
        return -1;

    (void)g_api->gfx_obj_set_parent(g_score_text, g_root);
    (void)g_api->gfx_obj_set_parent(g_message_text, g_root);
    (void)g_api->gfx_obj_set_parent(g_asset_text, g_root);
    (void)g_api->gfx_obj_set_outline_rgb(g_message_text, 25, 86, 116);
    (void)g_api->gfx_obj_set_outline_width(g_message_text, 2);

    layout();
    reset_game();
    update_asset_text();

    (void)g_api->window_set_visible(g_window, 1u);
    return 0;
}

static int flappy_bird_update(const sacx_api *api)
{
    sacx_mouse_state mouse;
    uint8_t focused = 1u;
    uint8_t flap_pressed = 0u;

    g_api = api;
    ++g_frame;

    if (!api || !g_window)
        return -1;
    if (!api->window_visible(g_window))
        return api->app_exit(0, "flappy bird closed");

    if (api->window_focused)
        focused = api->window_focused(g_window) ? 1u : 0u;

    if (!focused)
    {
        g_prev_mouse_buttons = 0u;
        if (!g_paused)
        {
            g_paused = 1u;
            update_message();
        }
        sync_scene();
        return api->app_yield();
    }
    if (g_paused)
    {
        g_paused = 0u;
        update_message();
    }

    if (api->input_key_pressed(SACX_KEY_SPACE) || api->input_key_pressed(SACX_KEY_UP) ||
        api->input_key_pressed(SACX_KEY_W))
        flap_pressed = 1u;
    if (api->mouse_get_state(&mouse) == 0)
    {
        if ((mouse.buttons & 1u) && !(g_prev_mouse_buttons & 1u))
            flap_pressed = 1u;
        g_prev_mouse_buttons = mouse.buttons;
    }

    if (api->input_key_pressed(SACX_KEY_R))
        reset_game();
    if (api->input_key_pressed(SACX_KEY_ESCAPE) || api->input_key_pressed(SACX_KEY_X))
        return api->app_exit(0, "flappy bird closed");

    if (flap_pressed)
        flap();

    update_game();
    sync_scene();
    return api->app_yield();
}

extern "C" int sacx_main(const sacx_api *api)
{
    if (!api || api->abi_version != SACX_API_ABI_VERSION)
        return -1;

    g_api = api;
    (void)SACX_APP_NO_CONSOLE(api);
    if (create_ui() != 0)
        return api->app_exit(-1, "flappy bird UI failed");
    if (api->app_set_update(flappy_bird_update) != 0)
        return api->app_exit(-1, "flappy bird update failed");
    return 0;
}
