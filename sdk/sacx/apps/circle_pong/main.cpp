#include "sacx_api.h"

static const sacx_api *g_api = 0;

static uint32_t g_window = 0;
static uint32_t g_root = 0;
static uint32_t g_title = 0;
static uint32_t g_status = 0;
static uint32_t g_hint = 0;
static uint32_t g_ball = 0;
static uint32_t g_center_dot = 0;
static uint32_t g_paddle = 0;
static uint32_t g_ring[96];
static uint32_t g_ring_count = 0;

static int32_t g_root_x = 0;
static int32_t g_root_y = 0;
static uint32_t g_root_w = 1;
static uint32_t g_root_h = 1;
static int32_t g_cx = 1;
static int32_t g_cy = 1;
static int32_t g_radius = 120;
static int32_t g_ball_x = 0;
static int32_t g_ball_y = 0;
static int32_t g_ball_vx = 0;
static int32_t g_ball_vy = 0;
static int32_t g_paddle_cx = 0;
static int32_t g_paddle_cy = 0;
static int32_t g_paddle_tx = 0;
static int32_t g_paddle_ty = 0;
static int32_t g_paddle_half = 76;
static int32_t g_paddle_angle_deg = 0;
static uint32_t g_score = 0;
static uint32_t g_best = 0;
static uint32_t g_frame = 0;
static uint32_t g_survival_frames = 0;
static uint32_t g_rng = 0x5A17C0DEu;
static uint8_t g_game_over = 0;
static uint8_t g_paused = 0;

#define FP_SHIFT 8
#define FP_ONE (1 << FP_SHIFT)
#define BALL_R 7
#define RING_DOT_R 2
#define PADDLE_THICK 12
#define MIN_PADDLE_HALF 22
#define MAX_PADDLE_HALF 76
#define BALL_SPEED_START 390
#define BALL_SPEED_STEP 12
#define BALL_SPEED_MAX 680

static const int16_t kSin64[64] = {
    0, 100, 199, 296, 391, 483, 569, 651,
    724, 788, 844, 890, 927, 953, 970, 978,
    974, 961, 939, 907, 866, 816, 757, 690,
    615, 532, 444, 350, 252, 151, 50, -50,
    -151, -252, -350, -444, -532, -615, -690, -757,
    -816, -866, -907, -939, -961, -974, -978, -970,
    -953, -927, -890, -844, -788, -724, -651, -569,
    -483, -391, -296, -199, -100, 0
};

static const int16_t kCos64[64] = {
    978, 970, 953, 927, 890, 844, 788, 724,
    651, 569, 483, 391, 296, 199, 100, 0,
    -100, -199, -296, -391, -483, -569, -651, -724,
    -788, -844, -890, -927, -953, -970, -978, -974,
    -961, -939, -907, -866, -816, -757, -690, -615,
    -532, -444, -350, -252, -151, -50, 50, 151,
    252, 350, 444, 532, 615, 690, 757, 816,
    866, 907, 939, 961, 974, 978, 974, 961
};

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
    if (!dst || cap == 0)
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
    if (!dst || cap == 0)
        return;
    while (len + 1u < cap && dst[len])
        ++len;
    copy_text(dst + len, cap - len, src);
}

static void append_uint(char *dst, uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0)
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

static int32_t abs_i32(int32_t v)
{
    return v < 0 ? -v : v;
}

static int32_t isqrt_i64(int64_t v)
{
    int64_t bit = 1ll << 62;
    int64_t res = 0;
    if (v <= 0)
        return 0;
    while (bit > v)
        bit >>= 2;
    while (bit)
    {
        if (v >= res + bit)
        {
            v -= res + bit;
            res = (res >> 1) + bit;
        }
        else
        {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t)res;
}

static int32_t normalize_deg(int32_t deg)
{
    deg %= 360;
    if (deg < 0)
        deg += 360;
    return deg;
}

static int32_t atan2_deg_approx(int32_t y, int32_t x)
{
    int32_t ay = abs_i32(y);
    int32_t angle = 0;

    if (x == 0 && y == 0)
        return 0;

    if (x >= 0)
    {
        int32_t den = x + ay;
        int32_t r = den ? (int32_t)(((int64_t)(x - ay) * 1024) / den) : 0;
        angle = 45 - (45 * r) / 1024;
    }
    else
    {
        int32_t den = ay - x;
        int32_t r = den ? (int32_t)(((int64_t)(x + ay) * 1024) / den) : 0;
        angle = 135 - (45 * r) / 1024;
    }

    return y < 0 ? -angle : angle;
}

static uint32_t rng_next(void)
{
    g_rng = g_rng * 1664525u + 1013904223u + (uint32_t)g_frame;
    return g_rng;
}

static int32_t current_speed(void)
{
    int32_t speed = BALL_SPEED_START + (int32_t)g_score * BALL_SPEED_STEP +
                    (int32_t)(g_survival_frames / 60u) * 18;
    if (speed > BALL_SPEED_MAX)
        speed = BALL_SPEED_MAX;
    return speed;
}

static void normalize_ball_speed(void)
{
    int32_t len = isqrt_i64((int64_t)g_ball_vx * g_ball_vx + (int64_t)g_ball_vy * g_ball_vy);
    int32_t speed = current_speed();

    if (len <= 0)
        return;

    g_ball_vx = (int32_t)(((int64_t)g_ball_vx * speed) / len);
    g_ball_vy = (int32_t)(((int64_t)g_ball_vy * speed) / len);
}

static void set_text(uint32_t obj, const char *text)
{
    if (g_api && obj)
        (void)g_api->gfx_text_set(obj, text);
}

static void update_status(void)
{
    char line[96];
    line[0] = 0;
    if (g_paused)
        append_text(line, sizeof(line), "PAUSED  ");
    if (g_game_over)
        append_text(line, sizeof(line), "GAME OVER  ");
    append_text(line, sizeof(line), "score ");
    append_uint(line, sizeof(line), g_score);
    append_text(line, sizeof(line), "   best ");
    append_uint(line, sizeof(line), g_best);
    append_text(line, sizeof(line), "   paddle ");
    append_uint(line, sizeof(line), (uint32_t)(g_paddle_half * 2));
    set_text(g_status, line);
}

static void reset_ball(void)
{
    uint32_t idx = rng_next() & 63u;
    int32_t speed = current_speed();
    g_ball_x = g_cx * FP_ONE;
    g_ball_y = g_cy * FP_ONE;
    g_ball_vx = (int32_t)(((int64_t)kCos64[idx] * speed) / 978);
    g_ball_vy = (int32_t)(((int64_t)kSin64[idx] * speed) / 978);
    if (abs_i32(g_ball_vx) < 42 && abs_i32(g_ball_vy) < 42)
        g_ball_vx = speed;
}

static void reset_game(void)
{
    g_score = 0;
    g_survival_frames = 0;
    g_paddle_half = MAX_PADDLE_HALF;
    g_game_over = 0;
    g_paused = 0;
    reset_ball();
    update_status();
}

static void layout(void)
{
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t play_w = 0;
    uint32_t play_h = 0;
    int32_t r = 0;

    if (!g_api || !g_root)
        return;
    if (g_api->gfx_obj_get_rect(g_root, &x, &y, &w, &h) != 0)
        return;

    if (w < 260u)
        w = 260u;
    if (h < 240u)
        h = 240u;

    g_root_x = x;
    g_root_y = y;
    g_root_w = w;
    g_root_h = h;
    g_cx = (int32_t)(w / 2u);
    g_cy = (int32_t)(h / 2u) + 18;
    play_w = w > 60u ? w - 60u : w;
    play_h = h > 112u ? h - 112u : h;
    r = (int32_t)((play_w < play_h ? play_w : play_h) / 2u);
    if (r < 70)
        r = 70;
    g_radius = r;

    if (g_title)
        (void)g_api->gfx_text_set_pos(g_title, (int32_t)(w / 2u), 18);
    if (g_hint)
        (void)g_api->gfx_text_set_pos(g_hint, (int32_t)(w / 2u), (int32_t)h - 33);
    if (g_status)
        (void)g_api->gfx_text_set_pos(g_status, (int32_t)(w / 2u), 46);
    if (g_center_dot)
        (void)g_api->gfx_obj_set_circle(g_center_dot, g_cx, g_cy, 3u);
}

static void update_paddle_from_mouse(void)
{
    sacx_mouse_state mouse;
    int32_t mx = 0;
    int32_t my = 0;
    int32_t len = 0;
    int32_t ux = FP_ONE;
    int32_t uy = 0;

    if (!g_api || g_api->mouse_get_state(&mouse) != 0)
        return;

    mx = mouse.x - g_root_x - g_cx;
    my = mouse.y - g_root_y - g_cy;
    len = isqrt_i64((int64_t)mx * mx + (int64_t)my * my);
    if (len > 0)
    {
        ux = (mx * FP_ONE) / len;
        uy = (my * FP_ONE) / len;
    }

    g_paddle_cx = g_cx + (g_radius * ux) / FP_ONE;
    g_paddle_cy = g_cy + (g_radius * uy) / FP_ONE;
    g_paddle_tx = -uy;
    g_paddle_ty = ux;
    g_paddle_angle_deg = normalize_deg(atan2_deg_approx(my, mx) + 90);
}

static void draw_ring(void)
{
    for (uint32_t i = 0; i < g_ring_count; ++i)
    {
        int32_t x = g_cx + (g_radius * (int32_t)kCos64[i & 63u]) / 978;
        int32_t y = g_cy + (g_radius * (int32_t)kSin64[i & 63u]) / 978;
        uint8_t glow = (i & 1u) ? 78u : 118u;
        (void)g_api->gfx_obj_set_circle(g_ring[i], x, y, RING_DOT_R);
        (void)g_api->gfx_obj_set_fill_rgb(g_ring[i], 26, glow, 118);
    }
}

static void draw_paddle(void)
{
    uint32_t w = (uint32_t)(g_paddle_half * 2);
    uint32_t h = PADDLE_THICK;

    if (!g_api || !g_paddle)
        return;

    (void)g_api->gfx_obj_set_rect(g_paddle, g_paddle_cx - g_paddle_half, g_paddle_cy - (int32_t)(h / 2u), w, h);
    (void)g_api->gfx_obj_set_rotation_pivot(g_paddle, g_paddle_half, (int32_t)(h / 2u));
    (void)g_api->gfx_obj_set_rotation_deg(g_paddle, g_paddle_angle_deg);
}

static int hit_paddle(int32_t bx, int32_t by)
{
    int32_t px = bx - g_paddle_cx;
    int32_t py = by - g_paddle_cy;
    int32_t along = (int32_t)(((int64_t)px * g_paddle_tx + (int64_t)py * g_paddle_ty) / FP_ONE);
    int32_t closest_x = 0;
    int32_t closest_y = 0;
    int32_t dx = 0;
    int32_t dy = 0;

    if (along < -g_paddle_half)
        along = -g_paddle_half;
    if (along > g_paddle_half)
        along = g_paddle_half;

    closest_x = g_paddle_cx + (g_paddle_tx * along) / FP_ONE;
    closest_y = g_paddle_cy + (g_paddle_ty * along) / FP_ONE;
    dx = bx - closest_x;
    dy = by - closest_y;
    return ((int64_t)dx * dx + (int64_t)dy * dy) <= (int64_t)(BALL_R + (PADDLE_THICK / 2) + 4) * (BALL_R + (PADDLE_THICK / 2) + 4);
}

static void bounce_inward(int32_t bx, int32_t by)
{
    int32_t dx = bx - g_cx;
    int32_t dy = by - g_cy;
    int32_t len = isqrt_i64((int64_t)dx * dx + (int64_t)dy * dy);
    int32_t speed = current_speed();
    int32_t inward_x = 0;
    int32_t inward_y = 0;
    int32_t tangent_x = 0;
    int32_t tangent_y = 0;
    int32_t tangent_mix = (int32_t)((rng_next() >> 16) % 181u) - 90;
    int32_t out_x = 0;
    int32_t out_y = 0;
    int32_t out_len = 0;

    if (len <= 0)
        len = 1;

    inward_x = (int32_t)(-((int64_t)dx * FP_ONE) / len);
    inward_y = (int32_t)(-((int64_t)dy * FP_ONE) / len);
    tangent_x = -inward_y;
    tangent_y = inward_x;

    out_x = inward_x + (tangent_x * tangent_mix) / 100;
    out_y = inward_y + (tangent_y * tangent_mix) / 100;
    out_len = isqrt_i64((int64_t)out_x * out_x + (int64_t)out_y * out_y);
    if (out_len <= 0)
        out_len = FP_ONE;

    g_ball_vx = (int32_t)(((int64_t)out_x * speed) / out_len);
    g_ball_vy = (int32_t)(((int64_t)out_y * speed) / out_len);
}

static void update_ball(void)
{
    int32_t bx = 0;
    int32_t by = 0;
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t dist = 0;

    if (g_game_over)
        return;

    ++g_survival_frames;
    normalize_ball_speed();

    g_ball_x += g_ball_vx;
    g_ball_y += g_ball_vy;
    bx = g_ball_x / FP_ONE;
    by = g_ball_y / FP_ONE;
    dx = bx - g_cx;
    dy = by - g_cy;
    dist = isqrt_i64((int64_t)dx * dx + (int64_t)dy * dy);

    if (dist >= g_radius - BALL_R - (PADDLE_THICK / 2))
    {
        if (hit_paddle(bx, by))
        {
            ++g_score;
            if (g_score > g_best)
                g_best = g_score;
            if (g_paddle_half > MIN_PADDLE_HALF)
                g_paddle_half -= 4;
            bounce_inward(bx, by);
            update_status();
        }
        else if (dist > g_radius + BALL_R + 12)
        {
            g_game_over = 1u;
            update_status();
        }
    }

}

static void draw_ball(void)
{
    if (!g_api || !g_ball)
        return;
    if (g_game_over)
        (void)g_api->gfx_obj_set_fill_rgb(g_ball, 255, 86, 74);
    else
        (void)g_api->gfx_obj_set_fill_rgb(g_ball, 248, 236, 142);
    (void)g_api->gfx_obj_set_circle(g_ball, g_ball_x / FP_ONE, g_ball_y / FP_ONE, BALL_R);
}

static int create_ui(void)
{
    sacx_window_style style = sacx_window_style_default();
    style.body_fill = rgb(7, 11, 16);
    style.body_outline = rgb(77, 159, 168);
    style.titlebar_fill = rgb(15, 36, 44);
    style.title_color = rgb(226, 245, 240);
    style.close_button_style.fill = rgb(142, 52, 42);
    style.close_button_style.hover_fill = rgb(196, 72, 58);
    style.fullscreen_button_style.fill = rgb(44, 96, 106);
    style.fullscreen_button_style.hover_fill = rgb(58, 130, 142);
    style.titlebar_height = 36u;
    style.title_scale = 2u;
    style.close_glyph_scale = 2u;
    style.fullscreen_glyph_scale = 2u;

    if (g_api->window_create_ex(140, 84, 620, 620, 25, "Circle Pong", &style, &g_window) != 0)
        return -1;
    if (g_api->window_root(g_window, &g_root) != 0)
        return -1;

    if (g_api->gfx_obj_add_text("CIRCLE PONG", 310, 18, 4, rgb(203, 255, 237), 255, 2, 1, 0, SACX_TEXT_ALIGN_CENTER, 1, &g_title) != 0)
        return -1;
    if (g_api->gfx_obj_add_text("score 0", 310, 46, 4, rgb(150, 206, 210), 255, 1, 0, 0, SACX_TEXT_ALIGN_CENTER, 1, &g_status) != 0)
        return -1;
    if (g_api->gfx_obj_add_text("Mouse aims paddle  |  Space/click restarts  |  X closes", 310, 584, 4, rgb(119, 160, 166), 255, 1, 0, 0, SACX_TEXT_ALIGN_CENTER, 1, &g_hint) != 0)
        return -1;

    (void)g_api->gfx_obj_set_parent(g_title, g_root);
    (void)g_api->gfx_obj_set_parent(g_status, g_root);
    (void)g_api->gfx_obj_set_parent(g_hint, g_root);

    for (uint32_t i = 0; i < 64u; ++i)
    {
        if (g_api->gfx_obj_add_circle(0, 0, RING_DOT_R, 1, rgb(32, 108, 124), 1, &g_ring[g_ring_count]) == 0)
        {
            (void)g_api->gfx_obj_set_parent(g_ring[g_ring_count], g_root);
            ++g_ring_count;
        }
    }

    if (g_api->gfx_obj_add_rect(0, 0, (uint32_t)(MAX_PADDLE_HALF * 2), PADDLE_THICK, 3, rgb(83, 246, 164), 1, &g_paddle) != 0)
        return -1;
    (void)g_api->gfx_obj_set_parent(g_paddle, g_root);
    (void)g_api->gfx_obj_set_outline_rgb(g_paddle, 203, 255, 222);
    (void)g_api->gfx_obj_set_outline_width(g_paddle, 1);

    if (g_api->gfx_obj_add_circle(0, 0, BALL_R, 5, rgb(248, 236, 142), 1, &g_ball) != 0)
        return -1;
    if (g_api->gfx_obj_add_circle(0, 0, 3, 2, rgb(78, 121, 126), 1, &g_center_dot) != 0)
        return -1;
    (void)g_api->gfx_obj_set_parent(g_ball, g_root);
    (void)g_api->gfx_obj_set_parent(g_center_dot, g_root);

    (void)g_api->window_set_visible(g_window, 1u);
    layout();
    g_rng ^= (uint32_t)g_api->time_ticks();
    reset_game();
    return 0;
}

static int circle_pong_update(const sacx_api *api)
{
    sacx_mouse_state mouse;
    uint8_t focused = 1u;
    g_api = api;
    ++g_frame;

    if (!api || !g_window)
        return -1;
    if (!api->window_visible(g_window))
        return api->app_exit(0, "circle pong closed");

    layout();
    if (api->window_focused)
        focused = api->window_focused(g_window) ? 1u : 0u;

    if (focused)
        update_paddle_from_mouse();
    draw_ring();
    draw_paddle();

    if (!focused)
    {
        if (!g_paused)
        {
            g_paused = 1u;
            update_status();
        }
        draw_ball();
        return api->app_yield();
    }
    if (g_paused)
    {
        g_paused = 0u;
        update_status();
    }

    if (g_game_over)
    {
        if (api->input_key_pressed(SACX_KEY_SPACE) ||
            (api->mouse_get_state(&mouse) == 0 && (mouse.buttons & 1u)))
            reset_game();
    }
    else
    {
        update_ball();
    }

    draw_ball();
    return api->app_yield();
}

extern "C" int sacx_main(const sacx_api *api)
{
    if (!api || api->abi_version != SACX_API_ABI_VERSION)
        return -1;
    g_api = api;
    (void)SACX_APP_NO_CONSOLE(api);
    if (create_ui() != 0)
        return api->app_exit(-1, "circle pong UI failed");
    if (api->app_set_update(circle_pong_update) != 0)
        return api->app_exit(-1, "circle pong update failed");
    return 0;
}
