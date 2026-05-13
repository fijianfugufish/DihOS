#pragma once
#include "sacx_api.h"

namespace sacx3d
{
    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    struct Box
    {
        float min_x, min_y, min_z;
        float max_x, max_y, max_z;
    };

    static inline sacx_color rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        sacx_color c = {r, g, b};
        return c;
    }

    static inline sacx3d_vec3 vec3(float x, float y, float z)
    {
        sacx3d_vec3 v = {x, y, z};
        return v;
    }

    static inline float absf(float v) { return v < 0.0f ? -v : v; }

    static inline float sin_deg(float deg)
    {
        while (deg > 180.0f)
            deg -= 360.0f;
        while (deg < -180.0f)
            deg += 360.0f;
        float x = deg * 0.017453292519943295f;
        float x2 = x * x;
        return x * (1.0f - x2 / 6.0f + (x2 * x2) / 120.0f - (x2 * x2 * x2) / 5040.0f);
    }

    static inline float cos_deg(float deg) { return sin_deg(deg + 90.0f); }

    class Instance
    {
    public:
        Instance() : api_(0), scene_(0), handle_(0) {}
        Instance(const sacx_api *api, uint32_t scene, uint32_t handle) : api_(api), scene_(scene), handle_(handle) {}

        uint32_t handle() const { return handle_; }
        bool valid() const { return api_ && scene_ && handle_; }

        int set_pos(float x, float y, float z) const
        {
            return valid() && api_->k3d_instance_set_pos ? api_->k3d_instance_set_pos(scene_, handle_, x, y, z) : -1;
        }

        int set_rotation(float pitch_deg, float yaw_deg, float roll_deg) const
        {
            return valid() && api_->k3d_instance_set_rotation ? api_->k3d_instance_set_rotation(scene_, handle_, pitch_deg, yaw_deg, roll_deg) : -1;
        }

        int set_scale(float sx, float sy, float sz) const
        {
            return valid() && api_->k3d_instance_set_scale ? api_->k3d_instance_set_scale(scene_, handle_, sx, sy, sz) : -1;
        }

        int set_visible(bool visible) const
        {
            return valid() && api_->k3d_instance_set_visible ? api_->k3d_instance_set_visible(scene_, handle_, visible ? 1u : 0u) : -1;
        }

        int set_casts_shadow(bool casts_shadow) const
        {
            return valid() && api_->k3d_instance_set_casts_shadow ? api_->k3d_instance_set_casts_shadow(scene_, handle_, casts_shadow ? 1u : 0u) : -1;
        }

    private:
        const sacx_api *api_;
        uint32_t scene_;
        uint32_t handle_;
    };

    class Scene
    {
    public:
        Scene() : api_(0), handle_(0), root_obj_(0) {}

        Scene(const sacx_api *api, uint32_t parent_obj, int32_t x, int32_t y,
              uint32_t w, uint32_t h, uint32_t internal_w = 0, uint32_t internal_h = 0, int32_t z = 1)
            : api_(0), handle_(0), root_obj_(0)
        {
            create(api, parent_obj, x, y, w, h, internal_w, internal_h, z);
        }

        int create(const sacx_api *api, uint32_t parent_obj, int32_t x, int32_t y,
                   uint32_t w, uint32_t h, uint32_t internal_w = 0, uint32_t internal_h = 0, int32_t z = 1)
        {
            if (!api || !api->k3d_scene_create)
                return -1;
            sacx3d_viewport_desc desc;
            desc.parent_obj_handle = parent_obj;
            desc.x = x;
            desc.y = y;
            desc.z = z;
            desc.w = w;
            desc.h = h;
            desc.internal_w = internal_w;
            desc.internal_h = internal_h;
            api_ = api;
            return api_->k3d_scene_create(&desc, &handle_, &root_obj_);
        }

        int destroy()
        {
            if (!api_ || !handle_ || !api_->k3d_scene_destroy)
                return -1;
            int rc = api_->k3d_scene_destroy(handle_);
            handle_ = 0;
            root_obj_ = 0;
            return rc;
        }

        bool valid() const { return api_ && handle_; }
        uint32_t handle() const { return handle_; }
        uint32_t root_obj() const { return root_obj_; }

        void attach(const sacx_api *api, uint32_t scene_handle, uint32_t root_obj)
        {
            api_ = api;
            handle_ = scene_handle;
            root_obj_ = root_obj;
        }

        int render() const
        {
            return valid() && api_->k3d_scene_render ? api_->k3d_scene_render(handle_) : -1;
        }

        int set_camera(const sacx3d_camera &camera) const
        {
            return valid() && api_->k3d_scene_set_camera ? api_->k3d_scene_set_camera(handle_, &camera) : -1;
        }

        sacx3d_camera camera() const
        {
            sacx3d_camera c = {{0.0f, 1.7f, -6.0f}, 0.0f, 0.0f, 0.0f, 70.0f, 0.08f, 120.0f};
            if (valid() && api_->k3d_scene_get_camera)
                (void)api_->k3d_scene_get_camera(handle_, &c);
            return c;
        }

        int set_ambient(sacx_color color, float intensity) const
        {
            return valid() && api_->k3d_scene_set_ambient ? api_->k3d_scene_set_ambient(handle_, color, intensity) : -1;
        }

        int set_directional_light(sacx3d_vec3 dir, sacx_color color, float intensity) const
        {
            return valid() && api_->k3d_scene_set_directional_light ? api_->k3d_scene_set_directional_light(handle_, dir, color, intensity) : -1;
        }

        int add_point_light(sacx3d_vec3 pos, sacx_color color, float radius, float intensity, uint32_t *out_light = 0) const
        {
            if (!valid() || !api_->k3d_scene_add_point_light)
                return -1;
            sacx3d_point_light light;
            light.pos = pos;
            light.color = color;
            light.radius = radius;
            light.intensity = intensity;
            light.enabled = 1u;
            uint32_t ignored = 0;
            return api_->k3d_scene_add_point_light(handle_, &light, out_light ? out_light : &ignored);
        }

        int set_point_light(uint32_t idx, sacx3d_vec3 pos, sacx_color color, float radius, float intensity, bool enabled = true) const
        {
            if (!valid() || !api_->k3d_scene_set_point_light)
                return -1;
            sacx3d_point_light light;
            light.pos = pos;
            light.color = color;
            light.radius = radius;
            light.intensity = intensity;
            light.enabled = enabled ? 1u : 0u;
            return api_->k3d_scene_set_point_light(handle_, idx, &light);
        }

        int set_fog(sacx_color color, float start, float end, bool enabled = true) const
        {
            if (!valid() || !api_->k3d_scene_set_fog)
                return -1;
            sacx3d_fog fog;
            fog.enabled = enabled ? 1u : 0u;
            fog.color = color;
            fog.start = start;
            fog.end = end;
            return api_->k3d_scene_set_fog(handle_, &fog);
        }

        int apply_default_world() const
        {
            return valid() && api_->k3d_scene_apply_default_world ? api_->k3d_scene_apply_default_world(handle_) : -1;
        }

        int add_room(uint32_t player_handle = 0, const sacx3d_room_desc *desc = 0) const
        {
            return valid() && api_->k3d_scene_add_room ? api_->k3d_scene_add_room(handle_, player_handle, desc) : -1;
        }

        Instance new_cube(float w, float h, float d, float x, float y, float z, sacx_color color = rgb(210, 210, 220)) const
        {
            uint32_t inst = 0;
            if (valid() && api_->k3d_scene_new_cube)
                (void)api_->k3d_scene_new_cube(handle_, w, h, d, x, y, z, color, &inst);
            return Instance(api_, handle_, inst);
        }

        Instance add_obstacle_cube(uint32_t player_handle, float w, float h, float d,
                                   float x, float y, float z, sacx_color color = rgb(210, 210, 220)) const
        {
            uint32_t inst = 0;
            if (valid() && api_->k3d_scene_add_obstacle_cube)
                (void)api_->k3d_scene_add_obstacle_cube(handle_, player_handle, w, h, d, x, y, z, color, &inst);
            return Instance(api_, handle_, inst);
        }

        Instance load_obj(const char *path, float x, float y, float z) const
        {
            uint32_t inst = 0;
            if (valid() && path && api_->k3d_scene_load_obj)
                (void)api_->k3d_scene_load_obj(handle_, path, x, y, z, &inst);
            return Instance(api_, handle_, inst);
        }

        Instance add_image_surface(uint32_t image_handle, uint32_t face,
                                   float x, float y, float z, float w, float h) const
        {
            uint32_t inst = 0;
            if (valid() && image_handle && api_->k3d_scene_add_image_surface)
                (void)api_->k3d_scene_add_image_surface(handle_, image_handle, face, x, y, z, w, h, &inst);
            return Instance(api_, handle_, inst);
        }

        Instance add_text_surface(const char *text, uint32_t face,
                                  float x, float y, float z, float w, float h,
                                  sacx_color text_color = rgb(245, 248, 255), uint8_t text_alpha = 255u,
                                  sacx_color bg_color = rgb(0, 0, 0), uint8_t bg_alpha = 0u,
                                  uint32_t scale = 2u) const
        {
            uint32_t inst = 0;
            if (valid() && text && api_->k3d_scene_add_text_surface)
            {
                (void)api_->k3d_scene_add_text_surface(handle_, text, face, x, y, z, w, h,
                                                       text_color, text_alpha, bg_color, bg_alpha,
                                                       scale, &inst);
            }
            return Instance(api_, handle_, inst);
        }

    private:
        const sacx_api *api_;
        uint32_t handle_;
        uint32_t root_obj_;
    };

    class PlayerController
    {
    public:
        PlayerController() : api_(0), handle_(0), scene_(0), free_mode_(0) {}

        PlayerController(const sacx_api *api, const Scene &scene, uint32_t window_handle, bool freecam = false)
            : api_(0), handle_(0), scene_(0), free_mode_(0)
        {
            create(api, scene, window_handle, freecam);
        }

        int create(const sacx_api *api, const Scene &scene, uint32_t window_handle, bool freecam = false,
                   const sacx3d_player_desc *custom_desc = 0)
        {
            if (!api || !scene.valid() || !api->k3d_player_create)
                return -1;
            sacx3d_player_desc desc;
            if (custom_desc)
                desc = *custom_desc;
            else
            {
                desc.camera = scene.camera();
                desc.walk_speed = 4.0f;
                desc.free_speed = 6.0f;
                desc.mouse_sensitivity = 0.12f;
                desc.radius = 0.35f;
                desc.eye_height = 1.7f;
                desc.free_mode = freecam ? 1u : 0u;
                desc.drag_to_look = 1u;
            }
            desc.free_mode = freecam ? 1u : desc.free_mode;
            api_ = api;
            scene_ = scene.handle();
            free_mode_ = desc.free_mode ? 1u : 0u;
            return api_->k3d_player_create(scene_, window_handle, &desc, &handle_);
        }

        bool valid() const { return api_ && handle_; }
        uint32_t handle() const { return handle_; }

        int destroy()
        {
            if (!valid() || !api_->k3d_player_destroy)
                return -1;
            int rc = api_->k3d_player_destroy(handle_);
            handle_ = 0;
            return rc;
        }

        int set_free_mode(bool freecam)
        {
            if (!valid() || !api_->k3d_player_set_free_mode)
                return -1;
            free_mode_ = freecam ? 1u : 0u;
            return api_->k3d_player_set_free_mode(handle_, free_mode_);
        }

        bool free_mode() const { return free_mode_ != 0u; }

        int add_collider(Box b)
        {
            if (!valid() || !api_->k3d_player_add_collider)
                return -1;
            sacx3d_box box;
            box.min_x = b.min_x;
            box.min_y = b.min_y;
            box.min_z = b.min_z;
            box.max_x = b.max_x;
            box.max_y = b.max_y;
            box.max_z = b.max_z;
            return api_->k3d_player_add_collider(handle_, &box);
        }

        int update(float dt, bool render_scene = true)
        {
            return valid() && api_->k3d_player_update ? api_->k3d_player_update(handle_, dt, render_scene ? 1u : 0u) : -1;
        }

        sacx3d_camera camera() const
        {
            sacx3d_camera c = {{0.0f, 1.7f, -5.0f}, 0.0f, 0.0f, 0.0f, 70.0f, 0.08f, 120.0f};
            if (valid() && api_->k3d_player_get_camera)
                (void)api_->k3d_player_get_camera(handle_, &c);
            return c;
        }

    private:
        const sacx_api *api_;
        uint32_t handle_;
        uint32_t scene_;
        uint8_t free_mode_;
    };

    class FreeCamera
    {
    public:
        sacx3d_camera camera;
        float speed;
        float mouse_sensitivity;

        FreeCamera()
            : camera{{0.0f, 2.0f, -6.0f}, 0.0f, 0.0f, 0.0f, 70.0f, 0.08f, 160.0f},
              speed(6.0f), mouse_sensitivity(0.12f)
        {
        }

        void update(const sacx_api *api, const Scene &scene, float dt)
        {
            if (!api)
                return;
            sacx_mouse_state mouse;
            if (api->input_mouse_consume && api->input_mouse_consume(&mouse) == 0)
            {
                if (mouse.buttons & 1u)
                {
                    camera.yaw_deg += (float)mouse.dx * mouse_sensitivity;
                    camera.pitch_deg -= (float)mouse.dy * mouse_sensitivity;
                }
            }
            float forward = api->input_key_down && api->input_key_down(SACX_KEY_W) ? 1.0f : 0.0f;
            forward -= api->input_key_down && api->input_key_down(SACX_KEY_S) ? 1.0f : 0.0f;
            float right = api->input_key_down && api->input_key_down(SACX_KEY_D) ? 1.0f : 0.0f;
            right -= api->input_key_down && api->input_key_down(SACX_KEY_A) ? 1.0f : 0.0f;
            float up = api->input_key_down && api->input_key_down(SACX_KEY_E) ? 1.0f : 0.0f;
            up -= api->input_key_down && api->input_key_down(SACX_KEY_Q) ? 1.0f : 0.0f;
            float sy = sin_deg(camera.yaw_deg);
            float cy = cos_deg(camera.yaw_deg);
            camera.pos.x += (sy * forward + cy * right) * speed * dt;
            camera.pos.z += (cy * forward - sy * right) * speed * dt;
            camera.pos.y += up * speed * dt;
            scene.set_camera(camera);
        }
    };
}
