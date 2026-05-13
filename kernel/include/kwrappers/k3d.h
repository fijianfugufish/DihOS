#pragma once
#include <stdint.h>
#include "kwrappers/kgfx.h"
#include "kwrappers/kui_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    float x;
    float y;
    float z;
} k3d_vec3;

typedef struct
{
    int idx;
} k3d_scene_handle;

typedef struct
{
    int idx;
} k3d_instance_handle;

typedef struct
{
    int idx;
} k3d_player_handle;

typedef struct
{
    kgfx_obj_handle parent;
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t w;
    uint32_t h;
    uint32_t internal_w;
    uint32_t internal_h;
} k3d_viewport_desc;

typedef struct
{
    k3d_vec3 pos;
    float yaw_deg;
    float pitch_deg;
    float roll_deg;
    float fov_deg;
    float near_z;
    float far_z;
} k3d_camera;

typedef struct
{
    uint8_t enabled;
    kcolor color;
    float start;
    float end;
} k3d_fog;

typedef struct
{
    k3d_vec3 pos;
    kcolor color;
    float radius;
    float intensity;
    uint8_t enabled;
} k3d_point_light;

typedef struct
{
    float min_x;
    float min_y;
    float min_z;
    float max_x;
    float max_y;
    float max_z;
} k3d_box;

typedef struct
{
    k3d_camera camera;
    float walk_speed;
    float free_speed;
    float mouse_sensitivity;
    float radius;
    float eye_height;
    uint8_t free_mode;
    uint8_t drag_to_look;
} k3d_player_desc;

typedef struct
{
    float center_x;
    float floor_y;
    float center_z;
    float w;
    float h;
    float d;
    float wall_thickness;
    kcolor floor_color;
    kcolor ceiling_color;
    kcolor left_wall_color;
    kcolor right_wall_color;
    kcolor front_wall_color;
    kcolor back_wall_color;
} k3d_room_desc;

typedef struct
{
    float dt;
    int32_t mouse_dx;
    int32_t mouse_dy;
    uint8_t mouse_buttons;
    uint8_t key_w;
    uint8_t key_a;
    uint8_t key_s;
    uint8_t key_d;
    uint8_t key_up;
    uint8_t key_down;
    uint8_t key_left;
    uint8_t key_right;
    uint8_t key_q;
    uint8_t key_e;
} k3d_player_input;

int k3d_scene_create(const k3d_viewport_desc *desc, k3d_scene_handle *out_scene, kgfx_obj_handle *out_obj);
int k3d_scene_destroy(k3d_scene_handle scene);
int k3d_scene_render(k3d_scene_handle scene);
int k3d_scene_resize(k3d_scene_handle scene, uint32_t viewport_w, uint32_t viewport_h,
                     uint32_t internal_w, uint32_t internal_h);
int k3d_scene_root_obj(k3d_scene_handle scene, kgfx_obj_handle *out_obj);

int k3d_scene_set_camera(k3d_scene_handle scene, const k3d_camera *camera);
int k3d_scene_get_camera(k3d_scene_handle scene, k3d_camera *out_camera);
int k3d_scene_set_ambient(k3d_scene_handle scene, kcolor color, float intensity);
int k3d_scene_set_directional_light(k3d_scene_handle scene, k3d_vec3 dir, kcolor color, float intensity);
int k3d_scene_clear_point_lights(k3d_scene_handle scene);
int k3d_scene_add_point_light(k3d_scene_handle scene, const k3d_point_light *light, uint32_t *out_light);
int k3d_scene_set_point_light(k3d_scene_handle scene, uint32_t light_idx, const k3d_point_light *light);
int k3d_scene_set_fog(k3d_scene_handle scene, const k3d_fog *fog);

int k3d_scene_new_cube(k3d_scene_handle scene,
                       float w, float h, float d,
                       float x, float y, float z,
                       kcolor color,
                       k3d_instance_handle *out_instance);
int k3d_scene_load_obj(k3d_scene_handle scene, const char *path,
                       float x, float y, float z,
                       k3d_instance_handle *out_instance);
int k3d_instance_set_pos(k3d_scene_handle scene, k3d_instance_handle inst, float x, float y, float z);
int k3d_instance_set_rotation(k3d_scene_handle scene, k3d_instance_handle inst, float pitch_deg, float yaw_deg, float roll_deg);
int k3d_instance_set_scale(k3d_scene_handle scene, k3d_instance_handle inst, float sx, float sy, float sz);
int k3d_instance_set_visible(k3d_scene_handle scene, k3d_instance_handle inst, uint32_t visible);

int k3d_player_create(k3d_scene_handle scene, const k3d_player_desc *desc, k3d_player_handle *out_player);
int k3d_player_destroy(k3d_player_handle player);
int k3d_player_set_free_mode(k3d_player_handle player, uint32_t free_mode);
int k3d_player_set_camera(k3d_player_handle player, const k3d_camera *camera);
int k3d_player_get_camera(k3d_player_handle player, k3d_camera *out_camera);
int k3d_player_clear_colliders(k3d_player_handle player);
int k3d_player_add_collider(k3d_player_handle player, const k3d_box *box);
int k3d_player_update(k3d_player_handle player, const k3d_player_input *input);

int k3d_scene_apply_default_world(k3d_scene_handle scene);
int k3d_scene_add_room(k3d_scene_handle scene, k3d_player_handle player, const k3d_room_desc *desc);
int k3d_scene_add_obstacle_cube(k3d_scene_handle scene, k3d_player_handle player,
                                float w, float h, float d,
                                float x, float y, float z,
                                kcolor color,
                                k3d_instance_handle *out_instance);

#ifdef __cplusplus
}
#endif
