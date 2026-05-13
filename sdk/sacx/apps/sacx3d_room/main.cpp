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
static uint32_t g_last_view_w = 0;
static uint32_t g_last_view_h = 0;
static uint8_t g_free_mode = 0;
static uint8_t g_fog = 1;

static const int32_t VIEW_X = 18;
static const int32_t VIEW_Y = 58;
static const uint32_t VIEW_PAD_X = 36;
static const uint32_t VIEW_PAD_BOTTOM = 39;
static const uint32_t INTERNAL_MAX_W = 960;
static const uint32_t INTERNAL_MAX_H = 540;

static sacx_color rgb(uint8_t r, uint8_t g, uint8_t b)
{
    sacx_color c = {r, g, b};
    return c;
}

static void add_collider(float min_x, float min_y, float min_z,
                         float max_x, float max_y, float max_z)
{
    sacx3d_box box;
    if (!g_api || !g_player || !g_api->k3d_player_add_collider)
        return;
    box.min_x = min_x;
    box.min_y = min_y;
    box.min_z = min_z;
    box.max_x = max_x;
    box.max_y = max_y;
    box.max_z = max_z;
    (void)g_api->k3d_player_add_collider(g_player, &box);
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

static void compute_internal_size(uint32_t view_w, uint32_t view_h, uint32_t *out_w, uint32_t *out_h)
{
    uint32_t w = view_w ? view_w : 1u;
    uint32_t h = view_h ? view_h : 1u;

    if (w > INTERNAL_MAX_W)
    {
        h = (uint32_t)(((uint64_t)h * INTERNAL_MAX_W) / w);
        w = INTERNAL_MAX_W;
        if (!h)
            h = 1u;
    }
    if (h > INTERNAL_MAX_H)
    {
        w = (uint32_t)(((uint64_t)w * INTERNAL_MAX_H) / h);
        h = INTERNAL_MAX_H;
        if (!w)
            w = 1u;
    }

    *out_w = w;
    *out_h = h;
}

static void layout_viewport(void)
{
    int32_t root_x = 0;
    int32_t root_y = 0;
    uint32_t root_w = 0;
    uint32_t root_h = 0;
    uint32_t view_w = 1u;
    uint32_t view_h = 1u;
    uint32_t internal_w = 0;
    uint32_t internal_h = 0;
    int32_t status_y = 0;

    if (!g_api || !g_root || !g_viewport || !g_api->gfx_obj_get_rect)
        return;
    if (g_api->gfx_obj_get_rect(g_root, &root_x, &root_y, &root_w, &root_h) != 0)
        return;
    (void)root_x;
    (void)root_y;

    view_w = root_w > VIEW_PAD_X ? root_w - VIEW_PAD_X : 1u;
    view_h = root_h > (uint32_t)VIEW_Y + VIEW_PAD_BOTTOM ? root_h - (uint32_t)VIEW_Y - VIEW_PAD_BOTTOM : 1u;
    if (view_w < 64u)
        view_w = 64u;
    if (view_h < 48u)
        view_h = 48u;

    if (view_w != g_last_view_w || view_h != g_last_view_h)
    {
        if (g_api->gfx_obj_set_rect)
            (void)g_api->gfx_obj_set_rect(g_viewport, VIEW_X, VIEW_Y, view_w, view_h);
        if (g_scene_obj && g_api->gfx_image_set_pos)
            (void)g_api->gfx_image_set_pos(g_scene_obj, 0, 0);
        if (g_scene && g_api->k3d_scene_resize)
        {
            compute_internal_size(view_w, view_h, &internal_w, &internal_h);
            if (g_api->k3d_scene_resize(g_scene, view_w, view_h, internal_w, internal_h) == 0 &&
                g_api->k3d_scene_render)
            {
                (void)g_api->k3d_scene_render(g_scene);
            }
        }
        g_last_view_w = view_w;
        g_last_view_h = view_h;
    }

    if (g_status && g_api->gfx_text_set_pos)
    {
        status_y = root_h > 35u ? (int32_t)root_h - 35 : VIEW_Y + (int32_t)view_h + 6;
        (void)g_api->gfx_text_set_pos(g_status, (int32_t)(root_w / 2u), status_y);
    }
}

static void setup_world(void)
{
    sacx3d::Scene scene;
    uint32_t img = 0;
    scene.attach(g_api, g_scene, g_scene_obj);

    scene.set_ambient(rgb(160, 185, 220), 0.0f);
    scene.set_directional_light(sacx3d::vec3(-0.25f, -0.75f, 0.35f), rgb(255, 242, 220), 0.45f);
    if (g_api->k3d_scene_clear_point_lights)
        (void)g_api->k3d_scene_clear_point_lights(g_scene);
    scene.add_point_light(sacx3d::vec3(-4.8f, 2.4f, 2.0f), rgb(255, 110, 90), 6.5f, 6.4f);
    scene.add_point_light(sacx3d::vec3(4.4f, 2.7f, 7.0f), rgb(80, 165, 255), 7.0f, 8.0f);
    scene.add_point_light(sacx3d::vec3(0.0f, 3.0f, 10.5f), rgb(120, 255, 190), 8.5f, 4.5f);
    scene.set_fog(rgb(28, 34, 44), 16.0f, 38.0f, g_fog != 0u);

    scene.new_cube(13.2f, 0.16f, 16.8f, 0.0f, -0.08f, 4.4f, rgb(82, 88, 86)).set_casts_shadow(false);
    scene.new_cube(13.2f, 0.18f, 16.8f, 0.0f, 3.45f, 4.4f, rgb(52, 57, 70)).set_casts_shadow(false);
    scene.new_cube(13.2f, 3.6f, 0.22f, 0.0f, 1.72f, 12.72f, rgb(76, 86, 108)).set_casts_shadow(false);
    scene.new_cube(0.22f, 3.6f, 16.8f, -6.6f, 1.72f, 4.4f, rgb(70, 82, 103)).set_casts_shadow(false);
    scene.new_cube(0.22f, 3.6f, 16.8f, 6.6f, 1.72f, 4.4f, rgb(82, 76, 98)).set_casts_shadow(false);
    scene.new_cube(3.0f, 1.0f, 0.6f, 0.0f, 0.50f, 10.7f, rgb(105, 117, 128)).set_casts_shadow(false);

    add_collider(-6.8f, -1.0f, -4.0f, -6.25f, 4.3f, 13.2f);
    add_collider(6.25f, -1.0f, -4.0f, 6.8f, 4.3f, 13.2f);
    add_collider(-6.9f, -1.0f, 12.45f, 6.9f, 4.3f, 13.1f);
    add_collider(-1.65f, -1.0f, 10.35f, 1.65f, 1.4f, 11.05f);

    scene.add_text_surface("K3D SURFACE GALLERY", SACX3D_SURFACE_BACK, 0.0f, 2.35f, 12.60f, 4.6f, 0.72f,
                           rgb(250, 252, 255), 255u, rgb(18, 24, 34), 225u, 4u);
    scene.add_text_surface("TEXT IS A TEXTURE", SACX3D_SURFACE_BACK, -3.55f, 1.45f, 12.60f, 2.6f, 0.45f,
                           rgb(38, 34, 24), 255u, rgb(255, 218, 96), 235u, 2u);
    scene.add_text_surface("ALPHA BACKGROUNDS", SACX3D_SURFACE_BACK, 3.55f, 1.45f, 12.60f, 2.8f, 0.45f,
                           rgb(235, 245, 255), 255u, rgb(24, 70, 105), 170u, 2u);
    scene.add_text_surface("LEFT WALL", SACX3D_SURFACE_RIGHT, -6.48f, 1.55f, 4.2f, 2.1f, 0.46f,
                           rgb(255, 235, 220), 255u, rgb(100, 42, 50), 210u, 2u);
    scene.add_text_surface("RIGHT WALL", SACX3D_SURFACE_LEFT, 6.48f, 1.55f, 4.4f, 2.1f, 0.46f,
                           rgb(220, 245, 255), 255u, rgb(36, 62, 92), 210u, 2u);
    scene.add_text_surface("FLOOR DECAL", SACX3D_SURFACE_TOP, 0.0f, 0.03f, 3.25f, 2.6f, 0.55f,
                           rgb(18, 22, 24), 255u, rgb(180, 235, 180), 185u, 2u);

    scene.add_obstacle_cube(g_player, 1.25f, 1.25f, 1.25f, -4.35f, 0.62f, 1.6f, rgb(222, 166, 78));
    scene.add_text_surface("FRONT", SACX3D_SURFACE_FRONT, -4.35f, 0.88f, 2.23f, 0.86f, 0.30f,
                           rgb(35, 28, 18), 255u, rgb(255, 238, 142), 230u, 2u);
    scene.add_text_surface("TOP", SACX3D_SURFACE_TOP, -4.35f, 1.26f, 1.6f, 0.72f, 0.32f,
                           rgb(24, 30, 32), 255u, rgb(166, 255, 210), 220u, 2u);

    scene.add_obstacle_cube(g_player, 1.2f, 2.1f, 1.2f, 4.55f, 1.05f, 3.5f, rgb(82, 162, 205));
    scene.add_text_surface("SIDE", SACX3D_SURFACE_LEFT, 3.95f, 1.52f, 3.5f, 0.80f, 0.32f,
                           rgb(230, 246, 255), 255u, rgb(28, 62, 84), 230u, 2u);

    sacx3d::Instance rotor = scene.new_cube(1.35f, 0.30f, 1.35f, 0.0f, 1.15f, 6.2f, rgb(214, 90, 132));
    g_rotor = rotor.handle();
    scene.add_text_surface("MOVING", SACX3D_SURFACE_FRONT, 0.0f, 1.15f, 6.88f, 0.92f, 0.28f,
                           rgb(255, 245, 252), 255u, rgb(80, 24, 56), 215u, 2u);

    if (g_api && g_api->img_load && g_api->img_load("0:/OS/System/Images/bgpaper.jpg", &img) == 0)
    {
        scene.add_image_surface(img, SACX3D_SURFACE_BACK, 0.0f, 1.05f, 12.59f, 2.8f, 1.15f);
        if (g_api->img_destroy)
            (void)g_api->img_destroy(img);
    }

    sacx3d::Instance obj = scene.load_obj("0:/Documents/Games/sacx3d/rhombic-dodecagon.obj", 0.0f, 0.95f, 8.0f);
    if (obj.valid())
    {
        (void)obj.set_scale(0.75f, 0.75f, 0.75f);
        (void)obj.set_casts_shadow(false);
    }
}

static int on_update(const sacx_api *api)
{
    (void)api;
    if (!g_api || !g_scene)
        return 0;
    layout_viewport();
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

    (void)api->gfx_obj_add_rect(VIEW_X, VIEW_Y, 754, 448, 1, rgb(8, 10, 14), 1u, &g_viewport);
    (void)api->gfx_obj_set_parent(g_viewport, g_root);
    (void)api->gfx_obj_set_clip_to_parent(g_viewport, 1u);
    (void)api->gfx_obj_add_text("", 395, 510, 4, rgb(230, 235, 240), 255, 1u, 0, 0, SACX_TEXT_ALIGN_CENTER, 1u, &g_status);
    (void)api->gfx_obj_set_parent(g_status, g_root);
    set_status();

    sacx3d::Scene scene(api, g_viewport, 0, 0, 754, 448, 426, 254, 2);
    if (!scene.valid())
        return -1;
    g_scene = scene.handle();
    g_scene_obj = scene.root_obj();
    if (api->gfx_image_set_sample_mode)
        (void)api->gfx_image_set_sample_mode(g_scene_obj, SACX_GFX_IMAGE_SAMPLE_NEAREST);
    layout_viewport();

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
