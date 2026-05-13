#include "kwrappers/k3d.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kimg.h"
#include "kwrappers/string.h"
#include "memory/pmem.h"

#include <stddef.h>

#define K3D_MAX_SCENES 16u
#define K3D_MAX_MESHES 48u
#define K3D_MAX_INSTANCES 128u
#define K3D_MAX_MATERIALS 96u
#define K3D_MAX_POINT_LIGHTS 8u
#define K3D_MAX_PLAYERS 32u
#define K3D_MAX_PLAYER_COLLIDERS 64u
#define K3D_MAX_FILE_BYTES (4u * 1024u * 1024u)

typedef struct
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
} k3d_vertex;

typedef struct
{
    uint32_t a, b, c;
    uint16_t material;
} k3d_tri;

typedef struct
{
    uint8_t used;
    char name[64];
    kcolor diffuse;
    kimg texture;
    uint8_t has_texture;
} k3d_material;

typedef struct
{
    uint8_t used;
    k3d_vertex *verts;
    k3d_tri *tris;
    uint32_t vert_count;
    uint32_t tri_count;
    uint64_t vert_pages;
    uint64_t tri_pages;
} k3d_mesh;

typedef struct
{
    uint8_t used;
    uint8_t visible;
    uint16_t mesh_idx;
    float x, y, z;
    float pitch, yaw, roll;
    float sx, sy, sz;
} k3d_instance;

typedef struct
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
} k3d_pipe_vert;

typedef struct
{
    float sx, sy;
    float inv_z;
    float cam_z;
    float wx, wy, wz;
    float nx, ny, nz;
    float u, v;
} k3d_screen_vert;

typedef struct
{
    uint8_t used;
    uint32_t w, h;
    uint32_t viewport_w, viewport_h;
    uint32_t *color;
    float *depth;
    uint64_t color_pages;
    uint64_t depth_pages;
    kgfx_obj_handle image_obj;

    k3d_camera camera;
    kcolor ambient_color;
    float ambient_intensity;
    k3d_vec3 dir_light_dir;
    kcolor dir_light_color;
    float dir_light_intensity;
    k3d_point_light point_lights[K3D_MAX_POINT_LIGHTS];
    k3d_fog fog;

    k3d_material materials[K3D_MAX_MATERIALS];
    k3d_mesh meshes[K3D_MAX_MESHES];
    k3d_instance instances[K3D_MAX_INSTANCES];
} k3d_scene;

typedef struct
{
    uint8_t used;
    k3d_scene_handle scene;
    k3d_camera camera;
    float walk_speed;
    float free_speed;
    float mouse_sensitivity;
    float radius;
    float eye_height;
    uint8_t free_mode;
    uint8_t drag_to_look;
    k3d_box colliders[K3D_MAX_PLAYER_COLLIDERS];
    uint32_t collider_count;
} k3d_player;

typedef struct
{
    float x, y, z;
} k3d_obj_pos;

typedef struct
{
    float u, v;
} k3d_obj_uv;

typedef struct
{
    float x, y, z;
} k3d_obj_norm;

typedef struct
{
    int v;
    int vt;
    int vn;
} k3d_face_ref;

static k3d_scene G_scenes[K3D_MAX_SCENES];
static k3d_player G_players[K3D_MAX_PLAYERS];

static float k3d_absf(float v) { return v < 0.0f ? -v : v; }
static float k3d_clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static uint8_t k3d_clamp_u8(float v)
{
    if (v <= 0.0f)
        return 0u;
    if (v >= 255.0f)
        return 255u;
    return (uint8_t)(v + 0.5f);
}

static float k3d_sqrtf(float v)
{
    if (v <= 0.0f)
        return 0.0f;
    float x = v > 1.0f ? v : 1.0f;
    for (uint32_t i = 0; i < 8u; ++i)
        x = 0.5f * (x + v / x);
    return x;
}

static float k3d_sin_deg(float deg)
{
    while (deg > 180.0f)
        deg -= 360.0f;
    while (deg < -180.0f)
        deg += 360.0f;
    float x = deg * 0.017453292519943295f;
    float x2 = x * x;
    return x * (1.0f - x2 / 6.0f + (x2 * x2) / 120.0f - (x2 * x2 * x2) / 5040.0f);
}

static float k3d_cos_deg(float deg)
{
    return k3d_sin_deg(deg + 90.0f);
}

static float k3d_tan_deg(float deg)
{
    float c = k3d_cos_deg(deg);
    if (k3d_absf(c) < 0.001f)
        c = c < 0.0f ? -0.001f : 0.001f;
    return k3d_sin_deg(deg) / c;
}

static k3d_vec3 k3d_vec3_norm(k3d_vec3 v)
{
    float len = k3d_sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0001f)
        return (k3d_vec3){0.0f, 0.0f, 1.0f};
    return (k3d_vec3){v.x / len, v.y / len, v.z / len};
}

static float k3d_dot3(k3d_vec3 a, k3d_vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static k3d_vec3 k3d_cross3(k3d_vec3 a, k3d_vec3 b)
{
    return (k3d_vec3){a.y * b.z - a.z * b.y,
                      a.z * b.x - a.x * b.z,
                      a.x * b.y - a.y * b.x};
}

static void *k3d_alloc_bytes(uint64_t bytes, uint64_t *out_pages)
{
    uint64_t pages = (bytes + 4095ull) >> 12;
    void *p = 0;
    if (out_pages)
        *out_pages = 0;
    if (!pages)
        return 0;
    p = pmem_alloc_pages(pages);
    if (!p)
        return 0;
    memset(p, 0, (size_t)(pages << 12));
    if (out_pages)
        *out_pages = pages;
    return p;
}

static void k3d_free_image(kimg *img)
{
    if (!img || !img->px || !img->w || !img->h)
        return;
    uint64_t pages = (((uint64_t)img->w * (uint64_t)img->h * 4ull) + 4095ull) >> 12;
    pmem_free_pages(img->px, pages);
    img->px = 0;
    img->w = 0;
    img->h = 0;
}

static k3d_scene *k3d_scene_ref(k3d_scene_handle h)
{
    if (h.idx < 0 || (uint32_t)h.idx >= K3D_MAX_SCENES)
        return 0;
    if (!G_scenes[h.idx].used)
        return 0;
    return &G_scenes[h.idx];
}

static k3d_player *k3d_player_ref(k3d_player_handle h)
{
    if (h.idx < 0 || (uint32_t)h.idx >= K3D_MAX_PLAYERS)
        return 0;
    if (!G_players[h.idx].used)
        return 0;
    return &G_players[h.idx];
}

static int k3d_alloc_scene(k3d_scene_handle *out)
{
    if (!out)
        return -1;
    for (uint32_t i = 0; i < K3D_MAX_SCENES; ++i)
    {
        if (G_scenes[i].used)
            continue;
        memset(&G_scenes[i], 0, sizeof(G_scenes[i]));
        G_scenes[i].used = 1u;
        G_scenes[i].image_obj.idx = -1;
        out->idx = (int)i;
        return 0;
    }
    out->idx = -1;
    return -1;
}

static int k3d_add_material(k3d_scene *s, const char *name, kcolor diffuse, uint16_t *out_idx)
{
    if (!s || !out_idx)
        return -1;
    for (uint32_t i = 0; i < K3D_MAX_MATERIALS; ++i)
    {
        if (s->materials[i].used)
            continue;
        k3d_material *m = &s->materials[i];
        memset(m, 0, sizeof(*m));
        m->used = 1u;
        m->diffuse = diffuse;
        if (name)
            strncpy(m->name, name, sizeof(m->name) - 1u);
        *out_idx = (uint16_t)i;
        return 0;
    }
    return -1;
}

static int k3d_find_material(k3d_scene *s, const char *name, uint16_t *out_idx)
{
    if (!s || !name || !out_idx)
        return -1;
    for (uint32_t i = 0; i < K3D_MAX_MATERIALS; ++i)
    {
        if (s->materials[i].used && strcmp(s->materials[i].name, name) == 0)
        {
            *out_idx = (uint16_t)i;
            return 0;
        }
    }
    return -1;
}

static int k3d_add_mesh(k3d_scene *s, uint16_t *out_idx)
{
    if (!s || !out_idx)
        return -1;
    for (uint32_t i = 0; i < K3D_MAX_MESHES; ++i)
    {
        if (s->meshes[i].used)
            continue;
        memset(&s->meshes[i], 0, sizeof(s->meshes[i]));
        s->meshes[i].used = 1u;
        *out_idx = (uint16_t)i;
        return 0;
    }
    return -1;
}

static int k3d_add_instance(k3d_scene *s, uint16_t mesh_idx, float x, float y, float z, k3d_instance_handle *out)
{
    if (!s || !out)
        return -1;
    for (uint32_t i = 0; i < K3D_MAX_INSTANCES; ++i)
    {
        if (s->instances[i].used)
            continue;
        k3d_instance *in = &s->instances[i];
        memset(in, 0, sizeof(*in));
        in->used = 1u;
        in->visible = 1u;
        in->mesh_idx = mesh_idx;
        in->x = x;
        in->y = y;
        in->z = z;
        in->sx = in->sy = in->sz = 1.0f;
        out->idx = (int)i;
        return 0;
    }
    out->idx = -1;
    return -1;
}

static void k3d_destroy_mesh(k3d_mesh *m)
{
    if (!m)
        return;
    if (m->verts && m->vert_pages)
        pmem_free_pages(m->verts, m->vert_pages);
    if (m->tris && m->tri_pages)
        pmem_free_pages(m->tris, m->tri_pages);
    memset(m, 0, sizeof(*m));
}

static void k3d_destroy_scene_contents(k3d_scene *s)
{
    if (!s)
        return;
    if (s->image_obj.idx >= 0)
        (void)kgfx_obj_destroy(s->image_obj);
    if (s->color && s->color_pages)
        pmem_free_pages(s->color, s->color_pages);
    if (s->depth && s->depth_pages)
        pmem_free_pages(s->depth, s->depth_pages);
    for (uint32_t i = 0; i < K3D_MAX_MESHES; ++i)
        k3d_destroy_mesh(&s->meshes[i]);
    for (uint32_t i = 0; i < K3D_MAX_MATERIALS; ++i)
    {
        if (s->materials[i].used)
            k3d_free_image(&s->materials[i].texture);
        memset(&s->materials[i], 0, sizeof(s->materials[i]));
    }
    memset(s, 0, sizeof(*s));
}

static int k3d_allocate_buffers(k3d_scene *s, uint32_t w, uint32_t h)
{
    uint64_t color_pages = 0;
    uint64_t depth_pages = 0;
    uint32_t *color = 0;
    float *depth = 0;

    if (!s || !w || !h || w > 2048u || h > 2048u)
        return -1;

    color = (uint32_t *)k3d_alloc_bytes((uint64_t)w * (uint64_t)h * 4ull, &color_pages);
    depth = (float *)k3d_alloc_bytes((uint64_t)w * (uint64_t)h * sizeof(float), &depth_pages);
    if (!color || !depth)
    {
        if (color)
            pmem_free_pages(color, color_pages);
        if (depth)
            pmem_free_pages(depth, depth_pages);
        return -1;
    }

    if (s->color && s->color_pages)
        pmem_free_pages(s->color, s->color_pages);
    if (s->depth && s->depth_pages)
        pmem_free_pages(s->depth, s->depth_pages);

    s->color = color;
    s->depth = depth;
    s->color_pages = color_pages;
    s->depth_pages = depth_pages;
    s->w = w;
    s->h = h;
    return 0;
}

int k3d_scene_create(const k3d_viewport_desc *desc, k3d_scene_handle *out_scene, kgfx_obj_handle *out_obj)
{
    k3d_scene_handle h = {.idx = -1};
    k3d_scene *s = 0;
    uint32_t iw = 0, ih = 0;

    if (!desc || !out_scene || !out_obj || !desc->w || !desc->h)
        return -1;
    if (!kgfx_obj_ref(desc->parent))
        return -1;

    iw = desc->internal_w ? desc->internal_w : desc->w;
    ih = desc->internal_h ? desc->internal_h : desc->h;
    if (iw > 640u)
    {
        ih = (uint32_t)(((uint64_t)ih * 640ull) / (uint64_t)iw);
        iw = 640u;
    }
    if (ih == 0u)
        ih = 1u;

    if (k3d_alloc_scene(&h) != 0)
        return -1;
    s = &G_scenes[h.idx];
    if (k3d_allocate_buffers(s, iw, ih) != 0)
    {
        k3d_destroy_scene_contents(s);
        return -1;
    }

    s->image_obj = kgfx_obj_add_image(s->color, iw, ih, desc->x, desc->y, iw);
    if (s->image_obj.idx < 0)
    {
        k3d_destroy_scene_contents(s);
        return -1;
    }
    kgfx_image_set_size(s->image_obj, desc->w, desc->h);
    kgfx_obj_set_parent(s->image_obj, desc->parent);
    kgfx_obj_set_clip_to_parent(s->image_obj, 1u);
    kgfx_obj_ref(s->image_obj)->z = desc->z;

    s->viewport_w = desc->w;
    s->viewport_h = desc->h;
    s->camera = (k3d_camera){{0.0f, 1.7f, -6.0f}, 0.0f, 0.0f, 0.0f, 70.0f, 0.08f, 120.0f};
    s->ambient_color = (kcolor){255, 255, 255};
    s->ambient_intensity = 0.25f;
    s->dir_light_dir = k3d_vec3_norm((k3d_vec3){-0.35f, -0.8f, 0.45f});
    s->dir_light_color = (kcolor){255, 245, 225};
    s->dir_light_intensity = 0.75f;
    s->fog.enabled = 0u;
    s->fog.color = (kcolor){40, 52, 70};
    s->fog.start = 20.0f;
    s->fog.end = 80.0f;

    *out_scene = h;
    *out_obj = s->image_obj;
    return 0;
}

int k3d_scene_destroy(k3d_scene_handle scene)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s)
        return -1;
    for (uint32_t i = 0; i < K3D_MAX_PLAYERS; ++i)
    {
        if (G_players[i].used && G_players[i].scene.idx == scene.idx)
            memset(&G_players[i], 0, sizeof(G_players[i]));
    }
    k3d_destroy_scene_contents(s);
    return 0;
}

int k3d_scene_root_obj(k3d_scene_handle scene, kgfx_obj_handle *out_obj)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s || !out_obj || s->image_obj.idx < 0)
        return -1;
    *out_obj = s->image_obj;
    return 0;
}

int k3d_scene_resize(k3d_scene_handle scene, uint32_t viewport_w, uint32_t viewport_h,
                     uint32_t internal_w, uint32_t internal_h)
{
    k3d_scene *s = k3d_scene_ref(scene);
    kgfx_obj *o = 0;
    if (!s || !viewport_w || !viewport_h)
        return -1;
    if (!internal_w)
        internal_w = viewport_w;
    if (!internal_h)
        internal_h = viewport_h;
    if (k3d_allocate_buffers(s, internal_w, internal_h) != 0)
        return -1;
    o = kgfx_obj_ref(s->image_obj);
    if (!o || o->kind != KGFX_OBJ_IMAGE)
        return -1;
    o->u.image.argb = s->color;
    o->u.image.src_w = internal_w;
    o->u.image.src_h = internal_h;
    o->u.image.stride_px = internal_w;
    kgfx_image_set_size(s->image_obj, viewport_w, viewport_h);
    kgfx_image_touch(s->image_obj);
    s->viewport_w = viewport_w;
    s->viewport_h = viewport_h;
    return 0;
}

int k3d_scene_set_camera(k3d_scene_handle scene, const k3d_camera *camera)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s || !camera)
        return -1;
    s->camera = *camera;
    if (s->camera.fov_deg < 20.0f)
        s->camera.fov_deg = 20.0f;
    if (s->camera.fov_deg > 120.0f)
        s->camera.fov_deg = 120.0f;
    if (s->camera.near_z <= 0.01f)
        s->camera.near_z = 0.01f;
    if (s->camera.far_z < s->camera.near_z + 1.0f)
        s->camera.far_z = s->camera.near_z + 1.0f;
    return 0;
}

int k3d_scene_get_camera(k3d_scene_handle scene, k3d_camera *out_camera)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s || !out_camera)
        return -1;
    *out_camera = s->camera;
    return 0;
}

int k3d_scene_set_ambient(k3d_scene_handle scene, kcolor color, float intensity)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s)
        return -1;
    s->ambient_color = color;
    s->ambient_intensity = k3d_clampf(intensity, 0.0f, 4.0f);
    return 0;
}

int k3d_scene_set_directional_light(k3d_scene_handle scene, k3d_vec3 dir, kcolor color, float intensity)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s)
        return -1;
    s->dir_light_dir = k3d_vec3_norm(dir);
    s->dir_light_color = color;
    s->dir_light_intensity = k3d_clampf(intensity, 0.0f, 8.0f);
    return 0;
}

int k3d_scene_clear_point_lights(k3d_scene_handle scene)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s)
        return -1;
    memset(s->point_lights, 0, sizeof(s->point_lights));
    return 0;
}

int k3d_scene_add_point_light(k3d_scene_handle scene, const k3d_point_light *light, uint32_t *out_light)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s || !light || !out_light)
        return -1;
    for (uint32_t i = 0; i < K3D_MAX_POINT_LIGHTS; ++i)
    {
        if (s->point_lights[i].enabled)
            continue;
        s->point_lights[i] = *light;
        s->point_lights[i].enabled = 1u;
        if (s->point_lights[i].radius <= 0.01f)
            s->point_lights[i].radius = 0.01f;
        *out_light = i;
        return 0;
    }
    return -1;
}

int k3d_scene_set_point_light(k3d_scene_handle scene, uint32_t light_idx, const k3d_point_light *light)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s || !light || light_idx >= K3D_MAX_POINT_LIGHTS)
        return -1;
    s->point_lights[light_idx] = *light;
    s->point_lights[light_idx].enabled = light->enabled ? 1u : 0u;
    if (s->point_lights[light_idx].radius <= 0.01f)
        s->point_lights[light_idx].radius = 0.01f;
    return 0;
}

int k3d_scene_set_fog(k3d_scene_handle scene, const k3d_fog *fog)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s || !fog)
        return -1;
    s->fog = *fog;
    if (s->fog.end <= s->fog.start + 0.01f)
        s->fog.end = s->fog.start + 0.01f;
    return 0;
}

int k3d_scene_apply_default_world(k3d_scene_handle scene)
{
    k3d_point_light light;

    if (!k3d_scene_ref(scene))
        return -1;

    (void)k3d_scene_set_ambient(scene, (kcolor){190, 205, 230}, 0.20f);
    (void)k3d_scene_set_directional_light(scene, (k3d_vec3){-0.35f, -0.9f, 0.3f}, (kcolor){255, 240, 210}, 0.55f);
    (void)k3d_scene_clear_point_lights(scene);

    light = (k3d_point_light){(k3d_vec3){-2.8f, 1.2f, 1.5f}, (kcolor){255, 92, 70}, 5.0f, 1.4f, 1u};
    (void)k3d_scene_add_point_light(scene, &light, &(uint32_t){0});
    light = (k3d_point_light){(k3d_vec3){3.2f, 1.4f, 4.8f}, (kcolor){80, 160, 255}, 6.0f, 1.2f, 1u};
    (void)k3d_scene_add_point_light(scene, &light, &(uint32_t){0});
    light = (k3d_point_light){(k3d_vec3){0.0f, 2.2f, 9.0f}, (kcolor){120, 255, 175}, 7.5f, 1.0f, 1u};
    (void)k3d_scene_add_point_light(scene, &light, &(uint32_t){0});

    return k3d_scene_set_fog(scene, &(k3d_fog){1u, (kcolor){36, 45, 60}, 12.0f, 34.0f});
}

static k3d_room_desc k3d_default_room_desc(void)
{
    k3d_room_desc d;
    d.center_x = 0.0f;
    d.floor_y = 0.0f;
    d.center_z = 4.0f;
    d.w = 12.0f;
    d.h = 3.2f;
    d.d = 18.0f;
    d.wall_thickness = 0.25f;
    d.floor_color = (kcolor){125, 128, 120};
    d.ceiling_color = (kcolor){75, 82, 98};
    d.left_wall_color = (kcolor){100, 108, 128};
    d.right_wall_color = (kcolor){100, 108, 128};
    d.front_wall_color = (kcolor){112, 96, 118};
    d.back_wall_color = (kcolor){80, 94, 120};
    return d;
}

static void k3d_room_add_collider(k3d_player_handle player,
                                  float min_x, float min_y, float min_z,
                                  float max_x, float max_y, float max_z)
{
    k3d_box box;
    if (!k3d_player_ref(player))
        return;
    box.min_x = min_x;
    box.min_y = min_y;
    box.min_z = min_z;
    box.max_x = max_x;
    box.max_y = max_y;
    box.max_z = max_z;
    (void)k3d_player_add_collider(player, &box);
}

int k3d_scene_add_room(k3d_scene_handle scene, k3d_player_handle player, const k3d_room_desc *desc)
{
    k3d_room_desc d = desc ? *desc : k3d_default_room_desc();
    k3d_instance_handle ignored;
    float half_w;
    float half_d;
    float wall_t;
    float wall_y;
    float left_x;
    float right_x;
    float front_z;
    float back_z;

    if (!k3d_scene_ref(scene))
        return -1;
    if (d.w <= 0.0f || d.h <= 0.0f || d.d <= 0.0f)
        return -1;
    if (d.wall_thickness <= 0.0f)
        d.wall_thickness = 0.25f;

    half_w = d.w * 0.5f;
    half_d = d.d * 0.5f;
    wall_t = d.wall_thickness;
    wall_y = d.floor_y + d.h * 0.5f - wall_t * 0.2f;
    left_x = d.center_x - half_w;
    right_x = d.center_x + half_w;
    front_z = d.center_z + half_d;
    back_z = d.center_z - half_d;

    if (k3d_scene_new_cube(scene, d.w, wall_t * 0.88f, d.d, d.center_x, d.floor_y - wall_t * 0.44f, d.center_z, d.floor_color, &ignored) != 0)
        return -1;
    if (k3d_scene_new_cube(scene, d.w, wall_t * 0.88f, d.d, d.center_x, d.floor_y + d.h, d.center_z, d.ceiling_color, &ignored) != 0)
        return -1;
    if (k3d_scene_new_cube(scene, wall_t, d.h, d.d, left_x, wall_y, d.center_z, d.left_wall_color, &ignored) != 0)
        return -1;
    if (k3d_scene_new_cube(scene, wall_t, d.h, d.d, right_x, wall_y, d.center_z, d.right_wall_color, &ignored) != 0)
        return -1;
    if (k3d_scene_new_cube(scene, d.w, d.h, wall_t, d.center_x, wall_y, front_z, d.front_wall_color, &ignored) != 0)
        return -1;
    if (k3d_scene_new_cube(scene, d.w, d.h, wall_t, d.center_x, wall_y, back_z, d.back_wall_color, &ignored) != 0)
        return -1;

    k3d_room_add_collider(player, left_x - wall_t * 1.2f, d.floor_y - 1.0f, back_z - wall_t,
                          left_x + wall_t * 1.4f, d.floor_y + d.h + 0.8f, front_z + wall_t);
    k3d_room_add_collider(player, right_x - wall_t * 1.4f, d.floor_y - 1.0f, back_z - wall_t,
                          right_x + wall_t * 1.2f, d.floor_y + d.h + 0.8f, front_z + wall_t);
    k3d_room_add_collider(player, left_x - wall_t, d.floor_y - 1.0f, front_z - wall_t * 1.4f,
                          right_x + wall_t, d.floor_y + d.h + 0.8f, front_z + wall_t * 1.2f);
    k3d_room_add_collider(player, left_x - wall_t, d.floor_y - 1.0f, back_z - wall_t * 1.2f,
                          right_x + wall_t, d.floor_y + d.h + 0.8f, back_z + wall_t * 1.4f);
    return 0;
}

int k3d_scene_add_obstacle_cube(k3d_scene_handle scene, k3d_player_handle player,
                                float w, float h, float d,
                                float x, float y, float z,
                                kcolor color,
                                k3d_instance_handle *out_instance)
{
    k3d_instance_handle local;
    k3d_instance_handle *dst = out_instance ? out_instance : &local;

    if (k3d_scene_new_cube(scene, w, h, d, x, y, z, color, dst) != 0)
        return -1;
    k3d_room_add_collider(player, x - w * 0.5f - 0.05f, y - h * 0.5f - 1.0f, z - d * 0.5f - 0.05f,
                          x + w * 0.5f + 0.05f, y + h * 0.5f + 0.8f, z + d * 0.5f + 0.05f);
    return 0;
}

static int k3d_make_checker(k3d_material *m)
{
    uint64_t pages = 0;
    uint32_t w = 64u, h = 64u;
    if (!m)
        return -1;
    m->texture.px = (uint32_t *)k3d_alloc_bytes((uint64_t)w * h * 4ull, &pages);
    if (!m->texture.px)
        return -1;
    m->texture.w = w;
    m->texture.h = h;
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            uint8_t shade = (((x >> 4) ^ (y >> 4)) & 1u) ? 230u : 170u;
            uint8_t r = (uint8_t)(((uint32_t)m->diffuse.r * shade) / 255u);
            uint8_t g = (uint8_t)(((uint32_t)m->diffuse.g * shade) / 255u);
            uint8_t b = (uint8_t)(((uint32_t)m->diffuse.b * shade) / 255u);
            m->texture.px[y * w + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    m->has_texture = 1u;
    return 0;
}

int k3d_scene_new_cube(k3d_scene_handle scene,
                       float w, float h, float d,
                       float x, float y, float z,
                       kcolor color,
                       k3d_instance_handle *out_instance)
{
    k3d_scene *s = k3d_scene_ref(scene);
    uint16_t mat_idx = 0, mesh_idx = 0;
    k3d_mesh *m = 0;
    k3d_vertex *v = 0;
    k3d_tri *t = 0;
    float hx = w * 0.5f, hy = h * 0.5f, hz = d * 0.5f;

    if (!s || !out_instance || w <= 0.0f || h <= 0.0f || d <= 0.0f)
        return -1;
    if (k3d_add_material(s, "cube", color, &mat_idx) != 0)
        return -1;
    (void)k3d_make_checker(&s->materials[mat_idx]);
    if (k3d_add_mesh(s, &mesh_idx) != 0)
        return -1;

    m = &s->meshes[mesh_idx];
    m->vert_count = 24u;
    m->tri_count = 12u;
    m->verts = (k3d_vertex *)k3d_alloc_bytes(sizeof(k3d_vertex) * m->vert_count, &m->vert_pages);
    m->tris = (k3d_tri *)k3d_alloc_bytes(sizeof(k3d_tri) * m->tri_count, &m->tri_pages);
    if (!m->verts || !m->tris)
    {
        k3d_destroy_mesh(m);
        return -1;
    }
    v = m->verts;
    t = m->tris;

#define K3D_FACE(base, ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz, nx, ny, nz)       \
    do                                                                                   \
    {                                                                                    \
        v[(base) + 0] = (k3d_vertex){(ax), (ay), (az), (nx), (ny), (nz), 0.0f, 1.0f};    \
        v[(base) + 1] = (k3d_vertex){(bx), (by), (bz), (nx), (ny), (nz), 1.0f, 1.0f};    \
        v[(base) + 2] = (k3d_vertex){(cx), (cy), (cz), (nx), (ny), (nz), 1.0f, 0.0f};    \
        v[(base) + 3] = (k3d_vertex){(dx), (dy), (dz), (nx), (ny), (nz), 0.0f, 0.0f};    \
    } while (0)

    K3D_FACE(0, -hx, -hy, hz, hx, -hy, hz, hx, hy, hz, -hx, hy, hz, 0.0f, 0.0f, 1.0f);
    K3D_FACE(4, hx, -hy, -hz, -hx, -hy, -hz, -hx, hy, -hz, hx, hy, -hz, 0.0f, 0.0f, -1.0f);
    K3D_FACE(8, hx, -hy, hz, hx, -hy, -hz, hx, hy, -hz, hx, hy, hz, 1.0f, 0.0f, 0.0f);
    K3D_FACE(12, -hx, -hy, -hz, -hx, -hy, hz, -hx, hy, hz, -hx, hy, -hz, -1.0f, 0.0f, 0.0f);
    K3D_FACE(16, -hx, hy, hz, hx, hy, hz, hx, hy, -hz, -hx, hy, -hz, 0.0f, 1.0f, 0.0f);
    K3D_FACE(20, -hx, -hy, -hz, hx, -hy, -hz, hx, -hy, hz, -hx, -hy, hz, 0.0f, -1.0f, 0.0f);
#undef K3D_FACE

    for (uint32_t f = 0; f < 6u; ++f)
    {
        uint32_t b = f * 4u;
        t[f * 2u + 0u] = (k3d_tri){b, b + 1u, b + 2u, mat_idx};
        t[f * 2u + 1u] = (k3d_tri){b, b + 2u, b + 3u, mat_idx};
    }
    return k3d_add_instance(s, mesh_idx, x, y, z, out_instance);
}

static void k3d_transform_vertex(const k3d_instance *in, const k3d_vertex *src, k3d_pipe_vert *dst)
{
    float x = src->x * in->sx;
    float y = src->y * in->sy;
    float z = src->z * in->sz;
    float nx = src->nx, ny = src->ny, nz = src->nz;
    float sy = k3d_sin_deg(in->yaw), cy = k3d_cos_deg(in->yaw);
    float sp = k3d_sin_deg(in->pitch), cp = k3d_cos_deg(in->pitch);
    float sr = k3d_sin_deg(in->roll), cr = k3d_cos_deg(in->roll);
    float x1 = x * cy + z * sy;
    float z1 = -x * sy + z * cy;
    float nx1 = nx * cy + nz * sy;
    float nz1 = -nx * sy + nz * cy;
    float y2 = y * cp - z1 * sp;
    float z2 = y * sp + z1 * cp;
    float ny2 = ny * cp - nz1 * sp;
    float nz2 = ny * sp + nz1 * cp;
    float x3 = x1 * cr - y2 * sr;
    float y3 = x1 * sr + y2 * cr;
    float nx3 = nx1 * cr - ny2 * sr;
    float ny3 = nx1 * sr + ny2 * cr;

    dst->x = x3 + in->x;
    dst->y = y3 + in->y;
    dst->z = z2 + in->z;
    k3d_vec3 n = k3d_vec3_norm((k3d_vec3){nx3, ny3, nz2});
    dst->nx = n.x;
    dst->ny = n.y;
    dst->nz = n.z;
    dst->u = src->u;
    dst->v = src->v;
}

static k3d_pipe_vert k3d_lerp_pipe(k3d_pipe_vert a, k3d_pipe_vert b, float t)
{
    k3d_pipe_vert o;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.z = a.z + (b.z - a.z) * t;
    o.nx = a.nx + (b.nx - a.nx) * t;
    o.ny = a.ny + (b.ny - a.ny) * t;
    o.nz = a.nz + (b.nz - a.nz) * t;
    o.u = a.u + (b.u - a.u) * t;
    o.v = a.v + (b.v - a.v) * t;
    return o;
}

static int k3d_camera_transform(const k3d_scene *s, const k3d_pipe_vert *src, k3d_pipe_vert *dst)
{
    float x = src->x - s->camera.pos.x;
    float y = src->y - s->camera.pos.y;
    float z = src->z - s->camera.pos.z;
    float sy = k3d_sin_deg(-s->camera.yaw_deg), cy = k3d_cos_deg(-s->camera.yaw_deg);
    float sp = k3d_sin_deg(-s->camera.pitch_deg), cp = k3d_cos_deg(-s->camera.pitch_deg);
    float x1 = x * cy + z * sy;
    float z1 = -x * sy + z * cy;
    float y2 = y * cp - z1 * sp;
    float z2 = y * sp + z1 * cp;
    *dst = *src;
    dst->x = x1;
    dst->y = y2;
    dst->z = z2;
    return z2 > 0.0f;
}

static uint32_t k3d_sample_material(const k3d_scene *s, uint16_t mat_idx, float u, float v)
{
    const k3d_material *m = 0;
    if (!s || mat_idx >= K3D_MAX_MATERIALS || !s->materials[mat_idx].used)
        return 0xFFFFFFFFu;
    m = &s->materials[mat_idx];
    if (m->has_texture && m->texture.px && m->texture.w && m->texture.h)
    {
        int32_t ix = (int32_t)(u * (float)m->texture.w);
        int32_t iy = (int32_t)((1.0f - v) * (float)m->texture.h);
        while (ix < 0)
            ix += (int32_t)m->texture.w;
        while (iy < 0)
            iy += (int32_t)m->texture.h;
        ix %= (int32_t)m->texture.w;
        iy %= (int32_t)m->texture.h;
        return m->texture.px[(uint32_t)iy * m->texture.w + (uint32_t)ix];
    }
    return 0xFF000000u | ((uint32_t)m->diffuse.r << 16) | ((uint32_t)m->diffuse.g << 8) | m->diffuse.b;
}

static uint32_t k3d_shade(const k3d_scene *s, uint16_t mat_idx,
                          float wx, float wy, float wz,
                          float nx, float ny, float nz,
                          float u, float v, float cam_z)
{
    uint32_t tex = k3d_sample_material(s, mat_idx, u, v);
    float br = s->ambient_intensity * ((float)s->ambient_color.r / 255.0f);
    float bg = s->ambient_intensity * ((float)s->ambient_color.g / 255.0f);
    float bb = s->ambient_intensity * ((float)s->ambient_color.b / 255.0f);
    k3d_vec3 n = k3d_vec3_norm((k3d_vec3){nx, ny, nz});
    k3d_vec3 to_light = {-s->dir_light_dir.x, -s->dir_light_dir.y, -s->dir_light_dir.z};
    float ndl = k3d_clampf(k3d_dot3(n, to_light), 0.0f, 1.0f) * s->dir_light_intensity;
    br += ndl * ((float)s->dir_light_color.r / 255.0f);
    bg += ndl * ((float)s->dir_light_color.g / 255.0f);
    bb += ndl * ((float)s->dir_light_color.b / 255.0f);

    for (uint32_t i = 0; i < K3D_MAX_POINT_LIGHTS; ++i)
    {
        const k3d_point_light *pl = &s->point_lights[i];
        if (!pl->enabled)
            continue;
        float lx = pl->pos.x - wx;
        float ly = pl->pos.y - wy;
        float lz = pl->pos.z - wz;
        float dist2 = lx * lx + ly * ly + lz * lz;
        float r2 = pl->radius * pl->radius;
        if (dist2 >= r2 || r2 <= 0.0001f)
            continue;
        float dist = k3d_sqrtf(dist2);
        float inv = dist > 0.0001f ? 1.0f / dist : 1.0f;
        float atten = 1.0f - (dist2 / r2);
        float pd = k3d_clampf(n.x * lx * inv + n.y * ly * inv + n.z * lz * inv, 0.0f, 1.0f) *
                   atten * atten * pl->intensity;
        br += pd * ((float)pl->color.r / 255.0f);
        bg += pd * ((float)pl->color.g / 255.0f);
        bb += pd * ((float)pl->color.b / 255.0f);
    }

    float ao = 0.76f + 0.20f * k3d_absf(n.y) + 0.04f * k3d_clampf(1.0f - cam_z / 80.0f, 0.0f, 1.0f);
    float r = (float)((tex >> 16) & 255u) * br * ao;
    float g = (float)((tex >> 8) & 255u) * bg * ao;
    float b = (float)(tex & 255u) * bb * ao;

    if (s->fog.enabled)
    {
        float t = k3d_clampf((cam_z - s->fog.start) / (s->fog.end - s->fog.start), 0.0f, 1.0f);
        r = r + ((float)s->fog.color.r - r) * t;
        g = g + ((float)s->fog.color.g - g) * t;
        b = b + ((float)s->fog.color.b - b) * t;
    }

    return 0xFF000000u | ((uint32_t)k3d_clamp_u8(r) << 16) |
           ((uint32_t)k3d_clamp_u8(g) << 8) | k3d_clamp_u8(b);
}

static float k3d_edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void k3d_raster_tri(k3d_scene *s, uint16_t mat_idx,
                           const k3d_screen_vert *a,
                           const k3d_screen_vert *b,
                           const k3d_screen_vert *c)
{
    float area = k3d_edge(a->sx, a->sy, b->sx, b->sy, c->sx, c->sy);
    int32_t min_x, max_x, min_y, max_y;
    if (k3d_absf(area) < 0.0001f)
        return;
    float inv_area = 1.0f / area;
    min_x = (int32_t)k3d_clampf((a->sx < b->sx ? (a->sx < c->sx ? a->sx : c->sx) : (b->sx < c->sx ? b->sx : c->sx)) - 1.0f, 0.0f, (float)(s->w - 1u));
    max_x = (int32_t)k3d_clampf((a->sx > b->sx ? (a->sx > c->sx ? a->sx : c->sx) : (b->sx > c->sx ? b->sx : c->sx)) + 1.0f, 0.0f, (float)(s->w - 1u));
    min_y = (int32_t)k3d_clampf((a->sy < b->sy ? (a->sy < c->sy ? a->sy : c->sy) : (b->sy < c->sy ? b->sy : c->sy)) - 1.0f, 0.0f, (float)(s->h - 1u));
    max_y = (int32_t)k3d_clampf((a->sy > b->sy ? (a->sy > c->sy ? a->sy : c->sy) : (b->sy > c->sy ? b->sy : c->sy)) + 1.0f, 0.0f, (float)(s->h - 1u));

    for (int32_t y = min_y; y <= max_y; ++y)
    {
        for (int32_t x = min_x; x <= max_x; ++x)
        {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = k3d_edge(b->sx, b->sy, c->sx, c->sy, px, py) * inv_area;
            float w1 = k3d_edge(c->sx, c->sy, a->sx, a->sy, px, py) * inv_area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            float iz = w0 * a->inv_z + w1 * b->inv_z + w2 * c->inv_z;
            if (iz <= 0.0f)
                continue;
            float z = 1.0f / iz;
            uint32_t idx = (uint32_t)y * s->w + (uint32_t)x;
            if (z >= s->depth[idx])
                continue;
            s->depth[idx] = z;
            float wx = (w0 * a->wx * a->inv_z + w1 * b->wx * b->inv_z + w2 * c->wx * c->inv_z) * z;
            float wy = (w0 * a->wy * a->inv_z + w1 * b->wy * b->inv_z + w2 * c->wy * c->inv_z) * z;
            float wz = (w0 * a->wz * a->inv_z + w1 * b->wz * b->inv_z + w2 * c->wz * c->inv_z) * z;
            float nx = (w0 * a->nx * a->inv_z + w1 * b->nx * b->inv_z + w2 * c->nx * c->inv_z) * z;
            float ny = (w0 * a->ny * a->inv_z + w1 * b->ny * b->inv_z + w2 * c->ny * c->inv_z) * z;
            float nz = (w0 * a->nz * a->inv_z + w1 * b->nz * b->inv_z + w2 * c->nz * c->inv_z) * z;
            float u = (w0 * a->u * a->inv_z + w1 * b->u * b->inv_z + w2 * c->u * c->inv_z) * z;
            float v = (w0 * a->v * a->inv_z + w1 * b->v * b->inv_z + w2 * c->v * c->inv_z) * z;
            s->color[idx] = k3d_shade(s, mat_idx, wx, wy, wz, nx, ny, nz, u, v, z);
        }
    }
}

static int k3d_project(const k3d_scene *s, const k3d_pipe_vert *cam, const k3d_pipe_vert *world, k3d_screen_vert *out)
{
    float f = 1.0f / k3d_tan_deg(s->camera.fov_deg * 0.5f);
    float aspect = (float)s->w / (float)s->h;
    if (cam->z <= s->camera.near_z)
        return 0;
    out->inv_z = 1.0f / cam->z;
    out->cam_z = cam->z;
    out->sx = ((cam->x * out->inv_z) * (f / aspect) * 0.5f + 0.5f) * (float)s->w;
    out->sy = (0.5f - (cam->y * out->inv_z) * f * 0.5f) * (float)s->h;
    out->wx = world->x;
    out->wy = world->y;
    out->wz = world->z;
    out->nx = world->nx;
    out->ny = world->ny;
    out->nz = world->nz;
    out->u = world->u;
    out->v = world->v;
    return 1;
}

static float k3d_clip_eval(const k3d_pipe_vert *cam, uint32_t plane, float near_z, float tan_x, float tan_y)
{
    switch (plane)
    {
    case 0:
        return cam->z - near_z;
    case 1:
        return cam->x + cam->z * tan_x;
    case 2:
        return cam->z * tan_x - cam->x;
    case 3:
        return cam->y + cam->z * tan_y;
    default:
        return cam->z * tan_y - cam->y;
    }
}

static uint32_t k3d_clip_plane(const k3d_pipe_vert *in_world, const k3d_pipe_vert *in_cam, uint32_t in_count,
                               k3d_pipe_vert *out_world, k3d_pipe_vert *out_cam,
                               uint32_t plane, float near_z, float tan_x, float tan_y)
{
    uint32_t out_count = 0;
    if (!in_count)
        return 0;

    for (uint32_t i = 0; i < in_count; ++i)
    {
        uint32_t j = (i + 1u) % in_count;
        float fa = k3d_clip_eval(&in_cam[i], plane, near_z, tan_x, tan_y);
        float fb = k3d_clip_eval(&in_cam[j], plane, near_z, tan_x, tan_y);
        uint8_t ina = fa >= 0.0f ? 1u : 0u;
        uint8_t inb = fb >= 0.0f ? 1u : 0u;

        if (ina && out_count < 16u)
        {
            out_world[out_count] = in_world[i];
            out_cam[out_count] = in_cam[i];
            ++out_count;
        }

        if (ina != inb && out_count < 16u)
        {
            float denom = fa - fb;
            float t = k3d_absf(denom) > 0.000001f ? (fa / denom) : 0.0f;
            out_world[out_count] = k3d_lerp_pipe(in_world[i], in_world[j], t);
            out_cam[out_count] = k3d_lerp_pipe(in_cam[i], in_cam[j], t);
            if (plane == 0)
                out_cam[out_count].z = near_z;
            ++out_count;
        }
    }

    return out_count;
}

static void k3d_draw_clipped_tri(k3d_scene *s, uint16_t mat_idx,
                                 const k3d_pipe_vert *wa,
                                 const k3d_pipe_vert *wb,
                                 const k3d_pipe_vert *wc)
{
    k3d_pipe_vert world_a[16];
    k3d_pipe_vert cam_a[16];
    k3d_pipe_vert world_b[16];
    k3d_pipe_vert cam_b[16];
    uint32_t count = 3u;
    float near_z = s->camera.near_z;
    float tan_y = k3d_tan_deg(s->camera.fov_deg * 0.5f);
    float tan_x = tan_y * ((float)s->w / (float)s->h);

    world_a[0] = *wa;
    world_a[1] = *wb;
    world_a[2] = *wc;
    for (uint32_t i = 0; i < count; ++i)
        (void)k3d_camera_transform(s, &world_a[i], &cam_a[i]);

    for (uint32_t plane = 0; plane < 5u; ++plane)
    {
        count = k3d_clip_plane(world_a, cam_a, count, world_b, cam_b, plane, near_z, tan_x, tan_y);
        if (count < 3u)
            return;
        for (uint32_t i = 0; i < count; ++i)
        {
            world_a[i] = world_b[i];
            cam_a[i] = cam_b[i];
        }
    }

    if (count < 3u)
        return;
    for (uint32_t i = 1u; i + 1u < count; ++i)
    {
        k3d_screen_vert sv0, sv1, sv2;
        if (!k3d_project(s, &cam_a[0], &world_a[0], &sv0) ||
            !k3d_project(s, &cam_a[i], &world_a[i], &sv1) ||
            !k3d_project(s, &cam_a[i + 1u], &world_a[i + 1u], &sv2))
            continue;
        k3d_raster_tri(s, mat_idx, &sv0, &sv1, &sv2);
    }
}

static void k3d_raster_shadow_tri(k3d_scene *s,
                                  const k3d_screen_vert *a,
                                  const k3d_screen_vert *b,
                                  const k3d_screen_vert *c)
{
    float area = k3d_edge(a->sx, a->sy, b->sx, b->sy, c->sx, c->sy);
    int32_t min_x, max_x, min_y, max_y;
    if (k3d_absf(area) < 0.0001f)
        return;
    float inv_area = 1.0f / area;
    min_x = (int32_t)k3d_clampf((a->sx < b->sx ? (a->sx < c->sx ? a->sx : c->sx) : (b->sx < c->sx ? b->sx : c->sx)) - 1.0f, 0.0f, (float)(s->w - 1u));
    max_x = (int32_t)k3d_clampf((a->sx > b->sx ? (a->sx > c->sx ? a->sx : c->sx) : (b->sx > c->sx ? b->sx : c->sx)) + 1.0f, 0.0f, (float)(s->w - 1u));
    min_y = (int32_t)k3d_clampf((a->sy < b->sy ? (a->sy < c->sy ? a->sy : c->sy) : (b->sy < c->sy ? b->sy : c->sy)) - 1.0f, 0.0f, (float)(s->h - 1u));
    max_y = (int32_t)k3d_clampf((a->sy > b->sy ? (a->sy > c->sy ? a->sy : c->sy) : (b->sy > c->sy ? b->sy : c->sy)) + 1.0f, 0.0f, (float)(s->h - 1u));

    for (int32_t y = min_y; y <= max_y; ++y)
    {
        for (int32_t x = min_x; x <= max_x; ++x)
        {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = k3d_edge(b->sx, b->sy, c->sx, c->sy, px, py) * inv_area;
            float w1 = k3d_edge(c->sx, c->sy, a->sx, a->sy, px, py) * inv_area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            float iz = w0 * a->inv_z + w1 * b->inv_z + w2 * c->inv_z;
            if (iz <= 0.0f)
                continue;
            float z = 1.0f / iz;
            uint32_t idx = (uint32_t)y * s->w + (uint32_t)x;
            if (s->depth[idx] > 999999.0f || k3d_absf(z - s->depth[idx]) > 0.20f)
                continue;
            uint32_t p = s->color[idx];
            uint8_t r = (uint8_t)((((p >> 16) & 255u) * 52u) / 100u);
            uint8_t g = (uint8_t)((((p >> 8) & 255u) * 52u) / 100u);
            uint8_t bch = (uint8_t)(((p & 255u) * 52u) / 100u);
            s->color[idx] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | bch;
        }
    }
}

static void k3d_draw_shadow_clipped_tri(k3d_scene *s,
                                        const k3d_pipe_vert *wa,
                                        const k3d_pipe_vert *wb,
                                        const k3d_pipe_vert *wc)
{
    k3d_pipe_vert world_a[16];
    k3d_pipe_vert cam_a[16];
    k3d_pipe_vert world_b[16];
    k3d_pipe_vert cam_b[16];
    uint32_t count = 3u;
    float near_z = s->camera.near_z;
    float tan_y = k3d_tan_deg(s->camera.fov_deg * 0.5f);
    float tan_x = tan_y * ((float)s->w / (float)s->h);

    world_a[0] = *wa;
    world_a[1] = *wb;
    world_a[2] = *wc;
    for (uint32_t i = 0; i < count; ++i)
        (void)k3d_camera_transform(s, &world_a[i], &cam_a[i]);

    for (uint32_t plane = 0; plane < 5u; ++plane)
    {
        count = k3d_clip_plane(world_a, cam_a, count, world_b, cam_b, plane, near_z, tan_x, tan_y);
        if (count < 3u)
            return;
        for (uint32_t i = 0; i < count; ++i)
        {
            world_a[i] = world_b[i];
            cam_a[i] = cam_b[i];
        }
    }

    for (uint32_t i = 1u; i + 1u < count; ++i)
    {
        k3d_screen_vert sv0, sv1, sv2;
        if (!k3d_project(s, &cam_a[0], &world_a[0], &sv0) ||
            !k3d_project(s, &cam_a[i], &world_a[i], &sv1) ||
            !k3d_project(s, &cam_a[i + 1u], &world_a[i + 1u], &sv2))
            continue;
        k3d_raster_shadow_tri(s, &sv0, &sv1, &sv2);
    }
}

static int k3d_project_shadow_vertex(k3d_vec3 light_dir, const k3d_pipe_vert *src, k3d_pipe_vert *out)
{
    if (k3d_absf(light_dir.y) < 0.04f || src->y <= 0.03f)
        return 0;
    float t = (0.02f - src->y) / light_dir.y;
    if (t <= 0.0f)
        return 0;
    *out = *src;
    out->x = src->x + light_dir.x * t;
    out->y = 0.02f;
    out->z = src->z + light_dir.z * t;
    out->nx = 0.0f;
    out->ny = 1.0f;
    out->nz = 0.0f;
    return 1;
}

static void k3d_render_shadows(k3d_scene *s)
{
    k3d_vec3 light_dir = k3d_vec3_norm(s->dir_light_dir);
    if (s->dir_light_intensity <= 0.0f || k3d_absf(light_dir.y) < 0.04f)
        return;

    for (uint32_t ii = 0; ii < K3D_MAX_INSTANCES; ++ii)
    {
        k3d_instance *in = &s->instances[ii];
        if (!in->used || !in->visible || in->mesh_idx >= K3D_MAX_MESHES)
            continue;
        k3d_mesh *m = &s->meshes[in->mesh_idx];
        if (!m->used || !m->verts || !m->tris)
            continue;
        for (uint32_t ti = 0; ti < m->tri_count; ++ti)
        {
            const k3d_tri *tr = &m->tris[ti];
            if (tr->a >= m->vert_count || tr->b >= m->vert_count || tr->c >= m->vert_count)
                continue;
            k3d_pipe_vert a, b, c;
            k3d_pipe_vert sa, sb, sc;
            k3d_transform_vertex(in, &m->verts[tr->a], &a);
            k3d_transform_vertex(in, &m->verts[tr->b], &b);
            k3d_transform_vertex(in, &m->verts[tr->c], &c);
            if (!k3d_project_shadow_vertex(light_dir, &a, &sa) ||
                !k3d_project_shadow_vertex(light_dir, &b, &sb) ||
                !k3d_project_shadow_vertex(light_dir, &c, &sc))
                continue;
            k3d_draw_shadow_clipped_tri(s, &sa, &sb, &sc);
        }
    }
}

int k3d_scene_render(k3d_scene_handle scene)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s || !s->color || !s->depth)
        return -1;
    uint32_t bg = 0xFF000000u | ((uint32_t)s->fog.color.r << 16) | ((uint32_t)s->fog.color.g << 8) | s->fog.color.b;
    for (uint32_t i = 0; i < s->w * s->h; ++i)
    {
        s->color[i] = bg;
        s->depth[i] = 1000000000.0f;
    }

    for (uint32_t ii = 0; ii < K3D_MAX_INSTANCES; ++ii)
    {
        k3d_instance *in = &s->instances[ii];
        if (!in->used || !in->visible || in->mesh_idx >= K3D_MAX_MESHES)
            continue;
        k3d_mesh *m = &s->meshes[in->mesh_idx];
        if (!m->used || !m->verts || !m->tris)
            continue;
        for (uint32_t ti = 0; ti < m->tri_count; ++ti)
        {
            const k3d_tri *tr = &m->tris[ti];
            if (tr->a >= m->vert_count || tr->b >= m->vert_count || tr->c >= m->vert_count)
                continue;
            k3d_pipe_vert a, b, c;
            k3d_transform_vertex(in, &m->verts[tr->a], &a);
            k3d_transform_vertex(in, &m->verts[tr->b], &b);
            k3d_transform_vertex(in, &m->verts[tr->c], &c);
            k3d_draw_clipped_tri(s, tr->material, &a, &b, &c);
        }
    }
    k3d_render_shadows(s);
    kgfx_image_touch(s->image_obj);
    return 0;
}

static const char *k3d_skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r')
        ++p;
    return p;
}

static int k3d_line_starts(const char *line, const char *prefix)
{
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

static float k3d_parse_float(const char **pp)
{
    const char *p = k3d_skip_ws(*pp);
    float sign = 1.0f;
    float v = 0.0f;
    float scale = 0.1f;
    if (*p == '-')
    {
        sign = -1.0f;
        ++p;
    }
    else if (*p == '+')
        ++p;
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10.0f + (float)(*p - '0');
        ++p;
    }
    if (*p == '.')
    {
        ++p;
        while (*p >= '0' && *p <= '9')
        {
            v += (float)(*p - '0') * scale;
            scale *= 0.1f;
            ++p;
        }
    }
    *pp = p;
    return v * sign;
}

static int k3d_parse_int(const char **pp)
{
    const char *p = *pp;
    int sign = 1;
    int v = 0;
    if (*p == '-')
    {
        sign = -1;
        ++p;
    }
    else if (*p == '+')
        ++p;
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10 + (*p - '0');
        ++p;
    }
    *pp = p;
    return v * sign;
}

static void k3d_copy_token(char *dst, uint32_t cap, const char **pp)
{
    const char *p = k3d_skip_ws(*pp);
    uint32_t n = 0;
    if (!dst || !cap)
        return;
    while (*p && *p != '\n' && *p != '\r' && *p != ' ' && *p != '\t' && n + 1u < cap)
        dst[n++] = *p++;
    dst[n] = 0;
    *pp = p;
}

static int k3d_read_file(const char *path, char **out_buf, uint32_t *out_size)
{
    KFile f;
    uint64_t size64 = 0;
    uint32_t size = 0;
    uint32_t got = 0;
    uint64_t pages = 0;
    char *buf = 0;
    if (!path || !out_buf || !out_size)
        return -1;
    if (kfile_open(&f, path, KFILE_READ) != 0)
        return -1;
    size64 = kfile_size(&f);
    if (size64 == 0 || size64 > K3D_MAX_FILE_BYTES)
    {
        kfile_close(&f);
        return -1;
    }
    size = (uint32_t)size64;
    buf = (char *)k3d_alloc_bytes((uint64_t)size + 1ull, &pages);
    if (!buf)
    {
        kfile_close(&f);
        return -1;
    }
    if (kfile_read(&f, buf, size, &got) != 0 || got != size)
    {
        pmem_free_pages(buf, pages);
        kfile_close(&f);
        return -1;
    }
    kfile_close(&f);
    buf[size] = 0;
    *out_buf = buf;
    *out_size = size;
    return 0;
}

static void k3d_dirname(const char *path, char *out, uint32_t cap)
{
    uint32_t last = 0;
    uint32_t i = 0;
    if (!out || !cap)
        return;
    out[0] = 0;
    if (!path)
        return;
    while (path[i])
    {
        if (path[i] == '/' || path[i] == '\\')
            last = i + 1u;
        ++i;
    }
    if (last >= cap)
        last = cap - 1u;
    for (i = 0; i < last; ++i)
        out[i] = path[i];
    out[last] = 0;
}

static void k3d_join_path(const char *base_file, const char *rel, char *out, uint32_t cap)
{
    char dir[256];
    uint32_t n = 0;
    if (!out || !cap)
        return;
    out[0] = 0;
    if (!rel)
        return;
    if (rel[0] == '/' || rel[0] == '\\' || (rel[0] && rel[1] == ':'))
    {
        strncpy(out, rel, cap - 1u);
        out[cap - 1u] = 0;
        return;
    }
    k3d_dirname(base_file, dir, sizeof(dir));
    while (dir[n] && n + 1u < cap)
    {
        out[n] = dir[n];
        ++n;
    }
    for (uint32_t i = 0; rel[i] && n + 1u < cap; ++i)
        out[n++] = rel[i];
    out[n] = 0;
}

static void k3d_load_mtl(k3d_scene *s, const char *obj_path, const char *mtl_name)
{
    char path[256];
    char *buf = 0;
    uint32_t size = 0;
    uint64_t pages = 0;
    uint16_t current = 0xFFFFu;
    k3d_join_path(obj_path, mtl_name, path, sizeof(path));
    if (k3d_read_file(path, &buf, &size) != 0)
        return;
    pages = ((uint64_t)size + 1ull + 4095ull) >> 12;
    for (char *line = buf; *line;)
    {
        char *next = line;
        while (*next && *next != '\n')
            ++next;
        if (*next)
            *next++ = 0;
        line = (char *)k3d_skip_ws(line);
        if (k3d_line_starts(line, "newmtl "))
        {
            const char *p = line + 7;
            char name[64];
            k3d_copy_token(name, sizeof(name), &p);
            if (k3d_find_material(s, name, &current) != 0)
                (void)k3d_add_material(s, name, (kcolor){220, 220, 220}, &current);
        }
        else if (k3d_line_starts(line, "Kd ") && current != 0xFFFFu)
        {
            const char *p = line + 3;
            float r = k3d_parse_float(&p);
            float g = k3d_parse_float(&p);
            float b = k3d_parse_float(&p);
            s->materials[current].diffuse = (kcolor){k3d_clamp_u8(r * 255.0f), k3d_clamp_u8(g * 255.0f), k3d_clamp_u8(b * 255.0f)};
        }
        else if (k3d_line_starts(line, "map_Kd ") && current != 0xFFFFu)
        {
            const char *p = line + 7;
            char tex_name[128];
            char tex_path[256];
            kimg img;
            k3d_copy_token(tex_name, sizeof(tex_name), &p);
            k3d_join_path(path, tex_name, tex_path, sizeof(tex_path));
            kimg_zero(&img);
            if (kimg_load(&img, tex_path) == 0)
            {
                k3d_free_image(&s->materials[current].texture);
                s->materials[current].texture = img;
                s->materials[current].has_texture = 1u;
            }
        }
        line = next;
    }
    pmem_free_pages(buf, pages);
}

static int k3d_face_ref_index(int idx, uint32_t count)
{
    if (idx > 0)
        return idx - 1;
    if (idx < 0)
        return (int)count + idx;
    return -1;
}

static k3d_face_ref k3d_parse_face_ref(const char **pp)
{
    k3d_face_ref r = {0, 0, 0};
    const char *p = k3d_skip_ws(*pp);
    r.v = k3d_parse_int(&p);
    if (*p == '/')
    {
        ++p;
        if (*p != '/')
            r.vt = k3d_parse_int(&p);
        if (*p == '/')
        {
            ++p;
            r.vn = k3d_parse_int(&p);
        }
    }
    *pp = p;
    return r;
}

int k3d_scene_load_obj(k3d_scene_handle scene, const char *path,
                       float x, float y, float z,
                       k3d_instance_handle *out_instance)
{
    k3d_scene *s = k3d_scene_ref(scene);
    char *buf = 0;
    uint32_t size = 0;
    uint64_t file_pages = 0;
    uint32_t pos_count = 0, uv_count = 0, norm_count = 0, tri_count = 0, vert_count = 0;
    uint64_t pos_pages = 0, uv_pages = 0, norm_pages = 0;
    k3d_obj_pos *pos = 0;
    k3d_obj_uv *uv = 0;
    k3d_obj_norm *norm = 0;
    uint16_t mesh_idx = 0, default_mat = 0;

    if (!s || !path || !out_instance)
        return -1;
    if (k3d_read_file(path, &buf, &size) != 0)
        return -1;
    file_pages = ((uint64_t)size + 1ull + 4095ull) >> 12;

    for (char *line = buf; *line;)
    {
        char *next = line;
        while (*next && *next != '\n')
            ++next;
        if (*next)
            *next++ = 0;
        line = (char *)k3d_skip_ws(line);
        if (k3d_line_starts(line, "v "))
            ++pos_count;
        else if (k3d_line_starts(line, "vt "))
            ++uv_count;
        else if (k3d_line_starts(line, "vn "))
            ++norm_count;
        else if (k3d_line_starts(line, "f "))
        {
            const char *p = line + 2;
            uint32_t refs = 0;
            while (*p)
            {
                p = k3d_skip_ws(p);
                if (!*p)
                    break;
                ++refs;
                while (*p && *p != ' ' && *p != '\t' && *p != '\r')
                    ++p;
            }
            if (refs >= 3u)
            {
                tri_count += refs - 2u;
                vert_count += (refs - 2u) * 3u;
            }
        }
        line = next;
    }

    if (!pos_count || !tri_count || !vert_count)
    {
        pmem_free_pages(buf, file_pages);
        return -1;
    }

    pos = (k3d_obj_pos *)k3d_alloc_bytes(sizeof(k3d_obj_pos) * pos_count, &pos_pages);
    uv = (k3d_obj_uv *)k3d_alloc_bytes(sizeof(k3d_obj_uv) * (uv_count ? uv_count : 1u), &uv_pages);
    norm = (k3d_obj_norm *)k3d_alloc_bytes(sizeof(k3d_obj_norm) * (norm_count ? norm_count : 1u), &norm_pages);
    if (!pos || !uv || !norm)
    {
        if (pos)
            pmem_free_pages(pos, pos_pages);
        if (uv)
            pmem_free_pages(uv, uv_pages);
        if (norm)
            pmem_free_pages(norm, norm_pages);
        pmem_free_pages(buf, file_pages);
        return -1;
    }

    if (k3d_add_material(s, "obj_default", (kcolor){220, 220, 220}, &default_mat) != 0 ||
        k3d_add_mesh(s, &mesh_idx) != 0)
    {
        pmem_free_pages(pos, pos_pages);
        pmem_free_pages(uv, uv_pages);
        pmem_free_pages(norm, norm_pages);
        pmem_free_pages(buf, file_pages);
        return -1;
    }

    for (uint32_t i = 0; i < size; ++i)
        if (buf[i] == 0)
            buf[i] = '\n';
    buf[size] = 0;

    k3d_mesh *mesh = &s->meshes[mesh_idx];
    mesh->verts = (k3d_vertex *)k3d_alloc_bytes(sizeof(k3d_vertex) * vert_count, &mesh->vert_pages);
    mesh->tris = (k3d_tri *)k3d_alloc_bytes(sizeof(k3d_tri) * tri_count, &mesh->tri_pages);
    if (!mesh->verts || !mesh->tris)
    {
        k3d_destroy_mesh(mesh);
        pmem_free_pages(pos, pos_pages);
        pmem_free_pages(uv, uv_pages);
        pmem_free_pages(norm, norm_pages);
        pmem_free_pages(buf, file_pages);
        return -1;
    }

    uint32_t pi = 0, ui = 0, ni = 0, vi = 0, ti = 0;
    uint16_t current_mat = default_mat;
    for (char *line = buf; *line;)
    {
        char *next = line;
        while (*next && *next != '\n')
            ++next;
        if (*next)
            *next++ = 0;
        line = (char *)k3d_skip_ws(line);
        if (k3d_line_starts(line, "v "))
        {
            const char *p = line + 2;
            if (pi < pos_count)
            {
                pos[pi].x = k3d_parse_float(&p);
                pos[pi].y = k3d_parse_float(&p);
                pos[pi].z = k3d_parse_float(&p);
                ++pi;
            }
        }
        else if (k3d_line_starts(line, "vt "))
        {
            const char *p = line + 3;
            if (ui < uv_count)
            {
                uv[ui].u = k3d_parse_float(&p);
                uv[ui].v = k3d_parse_float(&p);
                ++ui;
            }
        }
        else if (k3d_line_starts(line, "vn "))
        {
            const char *p = line + 3;
            if (ni < norm_count)
            {
                norm[ni].x = k3d_parse_float(&p);
                norm[ni].y = k3d_parse_float(&p);
                norm[ni].z = k3d_parse_float(&p);
                ++ni;
            }
        }
        else if (k3d_line_starts(line, "mtllib "))
        {
            const char *p = line + 7;
            char name[128];
            k3d_copy_token(name, sizeof(name), &p);
            k3d_load_mtl(s, path, name);
        }
        else if (k3d_line_starts(line, "usemtl "))
        {
            const char *p = line + 7;
            char name[64];
            k3d_copy_token(name, sizeof(name), &p);
            if (k3d_find_material(s, name, &current_mat) != 0)
                (void)k3d_add_material(s, name, (kcolor){220, 220, 220}, &current_mat);
        }
        else if (k3d_line_starts(line, "f "))
        {
            const char *p = line + 2;
            k3d_face_ref refs[16];
            uint32_t rc = 0;
            while (*p && rc < 16u)
            {
                p = k3d_skip_ws(p);
                if (!*p)
                    break;
                refs[rc++] = k3d_parse_face_ref(&p);
                while (*p && *p != ' ' && *p != '\t' && *p != '\r')
                    ++p;
            }
            for (uint32_t r = 1u; r + 1u < rc && ti < tri_count && vi + 2u < vert_count; ++r)
            {
                k3d_face_ref fr[3] = {refs[0], refs[r], refs[r + 1u]};
                uint32_t base = vi;
                for (uint32_t q = 0; q < 3u; ++q)
                {
                    int pidx = k3d_face_ref_index(fr[q].v, pi);
                    int tidx = k3d_face_ref_index(fr[q].vt, ui);
                    int nidx = k3d_face_ref_index(fr[q].vn, ni);
                    k3d_vertex vv = {0};
                    if (pidx >= 0 && (uint32_t)pidx < pi)
                    {
                        vv.x = pos[pidx].x;
                        vv.y = pos[pidx].y;
                        vv.z = pos[pidx].z;
                    }
                    if (tidx >= 0 && (uint32_t)tidx < ui)
                    {
                        vv.u = uv[tidx].u;
                        vv.v = uv[tidx].v;
                    }
                    if (nidx >= 0 && (uint32_t)nidx < ni)
                    {
                        vv.nx = norm[nidx].x;
                        vv.ny = norm[nidx].y;
                        vv.nz = norm[nidx].z;
                    }
                    mesh->verts[vi++] = vv;
                }
                if (mesh->verts[base].nx == 0.0f && mesh->verts[base].ny == 0.0f && mesh->verts[base].nz == 0.0f)
                {
                    k3d_vec3 e0 = {mesh->verts[base + 1u].x - mesh->verts[base].x,
                                   mesh->verts[base + 1u].y - mesh->verts[base].y,
                                   mesh->verts[base + 1u].z - mesh->verts[base].z};
                    k3d_vec3 e1 = {mesh->verts[base + 2u].x - mesh->verts[base].x,
                                   mesh->verts[base + 2u].y - mesh->verts[base].y,
                                   mesh->verts[base + 2u].z - mesh->verts[base].z};
                    k3d_vec3 n = k3d_vec3_norm(k3d_cross3(e0, e1));
                    mesh->verts[base].nx = mesh->verts[base + 1u].nx = mesh->verts[base + 2u].nx = n.x;
                    mesh->verts[base].ny = mesh->verts[base + 1u].ny = mesh->verts[base + 2u].ny = n.y;
                    mesh->verts[base].nz = mesh->verts[base + 1u].nz = mesh->verts[base + 2u].nz = n.z;
                }
                mesh->tris[ti++] = (k3d_tri){base, base + 1u, base + 2u, current_mat};
            }
        }
        line = next;
    }

    mesh->vert_count = vi;
    mesh->tri_count = ti;
    pmem_free_pages(pos, pos_pages);
    pmem_free_pages(uv, uv_pages);
    pmem_free_pages(norm, norm_pages);
    pmem_free_pages(buf, file_pages);
    if (!mesh->vert_count || !mesh->tri_count)
    {
        k3d_destroy_mesh(mesh);
        return -1;
    }
    return k3d_add_instance(s, mesh_idx, x, y, z, out_instance);
}

static k3d_instance *k3d_instance_ref(k3d_scene *s, k3d_instance_handle inst)
{
    if (!s || inst.idx < 0 || (uint32_t)inst.idx >= K3D_MAX_INSTANCES)
        return 0;
    if (!s->instances[inst.idx].used)
        return 0;
    return &s->instances[inst.idx];
}

int k3d_instance_set_pos(k3d_scene_handle scene, k3d_instance_handle inst, float x, float y, float z)
{
    k3d_instance *in = k3d_instance_ref(k3d_scene_ref(scene), inst);
    if (!in)
        return -1;
    in->x = x;
    in->y = y;
    in->z = z;
    return 0;
}

int k3d_instance_set_rotation(k3d_scene_handle scene, k3d_instance_handle inst, float pitch_deg, float yaw_deg, float roll_deg)
{
    k3d_instance *in = k3d_instance_ref(k3d_scene_ref(scene), inst);
    if (!in)
        return -1;
    in->pitch = pitch_deg;
    in->yaw = yaw_deg;
    in->roll = roll_deg;
    return 0;
}

int k3d_instance_set_scale(k3d_scene_handle scene, k3d_instance_handle inst, float sx, float sy, float sz)
{
    k3d_instance *in = k3d_instance_ref(k3d_scene_ref(scene), inst);
    if (!in || sx == 0.0f || sy == 0.0f || sz == 0.0f)
        return -1;
    in->sx = sx;
    in->sy = sy;
    in->sz = sz;
    return 0;
}

int k3d_instance_set_visible(k3d_scene_handle scene, k3d_instance_handle inst, uint32_t visible)
{
    k3d_instance *in = k3d_instance_ref(k3d_scene_ref(scene), inst);
    if (!in)
        return -1;
    in->visible = visible ? 1u : 0u;
    return 0;
}

int k3d_player_create(k3d_scene_handle scene, const k3d_player_desc *desc, k3d_player_handle *out_player)
{
    k3d_scene *s = k3d_scene_ref(scene);
    if (!s || !out_player)
        return -1;

    for (uint32_t i = 0; i < K3D_MAX_PLAYERS; ++i)
    {
        if (G_players[i].used)
            continue;

        k3d_player *p = &G_players[i];
        memset(p, 0, sizeof(*p));
        p->used = 1u;
        p->scene = scene;
        p->camera = s->camera;
        p->walk_speed = 4.0f;
        p->free_speed = 6.0f;
        p->mouse_sensitivity = 0.12f;
        p->radius = 0.35f;
        p->eye_height = 1.7f;
        p->drag_to_look = 1u;

        if (desc)
        {
            p->camera = desc->camera;
            if (desc->walk_speed > 0.0f)
                p->walk_speed = desc->walk_speed;
            if (desc->free_speed > 0.0f)
                p->free_speed = desc->free_speed;
            if (desc->mouse_sensitivity > 0.0f)
                p->mouse_sensitivity = desc->mouse_sensitivity;
            if (desc->radius > 0.0f)
                p->radius = desc->radius;
            if (desc->eye_height > 0.0f)
                p->eye_height = desc->eye_height;
            p->free_mode = desc->free_mode ? 1u : 0u;
            p->drag_to_look = desc->drag_to_look ? 1u : 0u;
        }

        if (!p->camera.fov_deg)
            p->camera.fov_deg = 70.0f;
        if (!p->camera.near_z)
            p->camera.near_z = 0.08f;
        if (!p->camera.far_z)
            p->camera.far_z = 120.0f;
        (void)k3d_scene_set_camera(scene, &p->camera);

        out_player->idx = (int)i;
        return 0;
    }

    out_player->idx = -1;
    return -1;
}

int k3d_player_destroy(k3d_player_handle player)
{
    k3d_player *p = k3d_player_ref(player);
    if (!p)
        return -1;
    memset(p, 0, sizeof(*p));
    return 0;
}

int k3d_player_set_free_mode(k3d_player_handle player, uint32_t free_mode)
{
    k3d_player *p = k3d_player_ref(player);
    if (!p)
        return -1;
    p->free_mode = free_mode ? 1u : 0u;
    return 0;
}

int k3d_player_set_camera(k3d_player_handle player, const k3d_camera *camera)
{
    k3d_player *p = k3d_player_ref(player);
    if (!p || !camera)
        return -1;
    p->camera = *camera;
    return k3d_scene_set_camera(p->scene, &p->camera);
}

int k3d_player_get_camera(k3d_player_handle player, k3d_camera *out_camera)
{
    k3d_player *p = k3d_player_ref(player);
    if (!p || !out_camera)
        return -1;
    *out_camera = p->camera;
    return 0;
}

int k3d_player_clear_colliders(k3d_player_handle player)
{
    k3d_player *p = k3d_player_ref(player);
    if (!p)
        return -1;
    p->collider_count = 0u;
    return 0;
}

int k3d_player_add_collider(k3d_player_handle player, const k3d_box *box)
{
    k3d_player *p = k3d_player_ref(player);
    if (!p || !box || p->collider_count >= K3D_MAX_PLAYER_COLLIDERS)
        return -1;
    p->colliders[p->collider_count++] = *box;
    return 0;
}

static int k3d_player_collides(const k3d_player *p, float x, float y, float z)
{
    if (!p)
        return 0;
    for (uint32_t i = 0; i < p->collider_count; ++i)
    {
        const k3d_box *b = &p->colliders[i];
        if (x + p->radius >= b->min_x && x - p->radius <= b->max_x &&
            y >= b->min_y && y <= b->max_y &&
            z + p->radius >= b->min_z && z - p->radius <= b->max_z)
            return 1;
    }
    return 0;
}

static void k3d_player_move_axis(k3d_player *p, float dx, float dz)
{
    float nx = p->camera.pos.x + dx;
    float nz = p->camera.pos.z + dz;
    if (!k3d_player_collides(p, nx, p->camera.pos.y, nz))
    {
        p->camera.pos.x = nx;
        p->camera.pos.z = nz;
    }
}

int k3d_player_update(k3d_player_handle player, const k3d_player_input *input)
{
    k3d_player *p = k3d_player_ref(player);
    float dt = 0.0f;
    float forward = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    float sy = 0.0f;
    float cy = 0.0f;
    float speed = 0.0f;
    float dx = 0.0f;
    float dz = 0.0f;

    if (!p || !input)
        return -1;

    dt = input->dt;
    if (dt <= 0.0f)
        dt = 1.0f / 30.0f;
    if (dt > 0.1f)
        dt = 0.1f;

    if (!p->drag_to_look || (input->mouse_buttons & 1u))
    {
        p->camera.yaw_deg += (float)input->mouse_dx * p->mouse_sensitivity;
        p->camera.pitch_deg += (float)input->mouse_dy * p->mouse_sensitivity;
        if (p->camera.pitch_deg > 80.0f)
            p->camera.pitch_deg = 80.0f;
        if (p->camera.pitch_deg < -80.0f)
            p->camera.pitch_deg = -80.0f;
    }

    forward = (input->key_w || input->key_up) ? 1.0f : 0.0f;
    forward -= (input->key_s || input->key_down) ? 1.0f : 0.0f;
    right = (input->key_d || input->key_right) ? 1.0f : 0.0f;
    right -= (input->key_a || input->key_left) ? 1.0f : 0.0f;
    sy = k3d_sin_deg(p->camera.yaw_deg);
    cy = k3d_cos_deg(p->camera.yaw_deg);
    speed = p->free_mode ? p->free_speed : p->walk_speed;
    dx = (sy * forward + cy * right) * speed * dt;
    dz = (cy * forward - sy * right) * speed * dt;

    if (p->free_mode)
    {
        up = input->key_e ? 1.0f : 0.0f;
        up -= input->key_q ? 1.0f : 0.0f;
        p->camera.pos.x += dx;
        p->camera.pos.z += dz;
        p->camera.pos.y += up * speed * dt;
    }
    else
    {
        k3d_player_move_axis(p, dx, 0.0f);
        k3d_player_move_axis(p, 0.0f, dz);
        p->camera.pos.y = p->eye_height;
    }

    return k3d_scene_set_camera(p->scene, &p->camera);
}
