#include "sacx_api.h"
#include "sacx3d.hpp"

static const sacx_api *g_api = 0;
static uint32_t g_window = 0;
static uint32_t g_root = 0;
static uint32_t g_viewport = 0;
static uint32_t g_scene = 0;
static uint32_t g_scene_obj = 0;
static uint32_t g_player = 0;
static uint32_t g_status = 0;
static uint32_t g_rotor = 0;
static uint32_t g_frame = 0;
static uint8_t g_free_mode = 0;
static uint8_t g_fog = 1;

static sacx_color rgb(uint8_t r, uint8_t g, uint8_t b)
{
    sacx_color c = {r, g, b};
    return c;
}

static void set_status(void)
{
    if (!g_api || !g_status)
        return;
    if (g_free_mode)
        (void)g_api->gfx_text_set(g_status, "SACX3D room  |  free camera  |  drag to look  |  F toggle  G fog  WASD QE");
    else
        (void)g_api->gfx_text_set(g_status, "SACX3D room  |  player collision  |  drag to look  |  F toggle  G fog  WASD");
}

static void setup_world(void)
{
    sacx3d::Scene scene;
    scene.attach(g_api, g_scene, g_scene_obj);

    scene.apply_default_world();
    scene.add_room(g_player);
    scene.add_obstacle_cube(g_player, 1.2f, 1.2f, 1.2f, -2.2f, 0.6f, 1.2f, rgb(220, 175, 90));
    scene.add_obstacle_cube(g_player, 1.1f, 2.0f, 1.1f, 2.8f, 1.0f, 3.5f, rgb(95, 170, 205));
    sacx3d::Instance rotor = scene.new_cube(1.4f, 0.28f, 1.4f, 0.0f, 1.0f, 6.0f, rgb(220, 100, 130));
    g_rotor = rotor.handle();

    sacx3d::Instance obj = scene.load_obj("0:/Documents/Games/sacx3d/demo.obj", 0.0f, 0.0f, 8.5f);
    if (obj.valid())
        (void)obj.set_scale(1.0f, 1.0f, 1.0f);
}

static int on_update(const sacx_api *api)
{
    (void)api;
    if (!g_api || !g_scene)
        return 0;
    if (g_api->window_focused && !g_api->window_focused(g_window))
        return 0;
    if (g_api->input_key_pressed && g_api->input_key_pressed(SACX_KEY_ESCAPE))
        return g_api->app_exit ? g_api->app_exit(0, "closed") : 0;

    if (g_api->input_key_pressed && g_api->input_key_pressed(SACX_KEY_F))
    {
        g_free_mode = g_free_mode ? 0u : 1u;
        if (g_api->k3d_player_set_free_mode)
            (void)g_api->k3d_player_set_free_mode(g_player, g_free_mode);
        set_status();
    }
    if (g_api->input_key_pressed && g_api->input_key_pressed(SACX_KEY_G))
    {
        sacx3d::Scene scene;
        scene.attach(g_api, g_scene, g_scene_obj);
        g_fog = g_fog ? 0u : 1u;
        scene.set_fog(rgb(36, 45, 60), 12.0f, 34.0f, g_fog != 0u);
    }

    if (g_rotor)
        (void)g_api->k3d_instance_set_rotation(g_scene, g_rotor, 0.0f, (float)((g_frame * 3u) % 360u), 0.0f);
    if (g_api->k3d_player_update)
        (void)g_api->k3d_player_update(g_player, 1.0f / 30.0f, 1u);
    ++g_frame;
    return 0;
}

extern "C" int sacx_main(const sacx_api *api)
{
    if (!api)
        return -1;
    g_api = api;
    SACX_APP_NO_CONSOLE(api);

    sacx_window_style style = sacx_window_style_default();
    style.body_fill = rgb(22, 24, 30);
    style.titlebar_fill = rgb(42, 48, 58);

    if (api->window_create_ex(74, 58, 790, 545, 35, "SACX3D Room", &style, &g_window) != 0)
        return -1;
    if (api->window_root(g_window, &g_root) != 0)
        return -1;

    (void)api->gfx_obj_add_rect(18, 58, 754, 448, 1, rgb(8, 10, 14), 1u, &g_viewport);
    (void)api->gfx_obj_set_parent(g_viewport, g_root);
    (void)api->gfx_obj_set_clip_to_parent(g_viewport, 1u);
    (void)api->gfx_obj_add_text("", 22, 510, 4, rgb(230, 235, 240), 255, 1u, 0, 0, SACX_TEXT_ALIGN_LEFT, 1u, &g_status);
    (void)api->gfx_obj_set_parent(g_status, g_root);
    set_status();

    sacx3d::Scene scene(api, g_viewport, 0, 0, 754, 448, 426, 254, 2);
    if (!scene.valid())
        return -1;
    g_scene = scene.handle();
    g_scene_obj = scene.root_obj();

    sacx3d_player_desc player;
    player.camera.pos = sacx3d::vec3(0.0f, 1.7f, -5.0f);
    player.camera.yaw_deg = 0.0f;
    player.camera.pitch_deg = 0.0f;
    player.camera.roll_deg = 0.0f;
    player.camera.fov_deg = 72.0f;
    player.camera.near_z = 0.08f;
    player.camera.far_z = 130.0f;
    player.walk_speed = 4.0f;
    player.free_speed = 6.0f;
    player.mouse_sensitivity = 0.24f;
    player.radius = 0.34f;
    player.eye_height = 1.7f;
    player.free_mode = g_free_mode;
    player.drag_to_look = 1u;
    sacx3d::PlayerController controller;
    if (controller.create(api, scene, g_window, g_free_mode != 0u, &player) != 0)
        return -1;
    g_player = controller.handle();

    setup_world();
    controller.update(1.0f / 30.0f, true);
    api->app_set_update(on_update);
    return 0;
}
