#include "kwrappers/k3d.h"
#include "kwrappers/kfile.h"
#include "kwrappers/kimg.h"
#include "kwrappers/ktext.h"
#include "kwrappers/string.h"
#include "memory/pmem.h"

#include <stddef.h>

#define K3D_MULTIPLIER 8u

#define K3D_MAX_SCENES 16u * K3D_MULTIPLIER
#define K3D_MAX_MESHES 48u * K3D_MULTIPLIER
#define K3D_MAX_INSTANCES 128u * K3D_MULTIPLIER
#define K3D_MAX_MATERIALS 96u * K3D_MULTIPLIER
#define K3D_MAX_POINT_LIGHTS 8u * K3D_MULTIPLIER
#define K3D_MAX_PLAYERS 32u * K3D_MULTIPLIER
#define K3D_MAX_PLAYER_COLLIDERS 64u * K3D_MULTIPLIER
#define K3D_MAX_FILE_BYTES (4u * 1024u * 1024u) * K3D_MULTIPLIER
#define K3D_LIGHT_SAMPLE_STEP 16
#define K3D_MAX_SHADOW_TRIS 2048u
#define K3D_SHADOW_GRID_W 32u
#define K3D_SHADOW_GRID_H 32u
#define K3D_SHADOW_CELL_MAX 64u
#define K3D_SCREEN_TILE 32u
#define K3D_TILE_MAX_LIGHTS 8u
#define K3D_MAX_LIGHT_TILES_X ((2048u + K3D_SCREEN_TILE - 1u) / K3D_SCREEN_TILE)
#define K3D_MAX_LIGHT_TILES_Y ((2048u + K3D_SCREEN_TILE - 1u) / K3D_SCREEN_TILE)
#define K3D_MAX_LIGHT_TILES (K3D_MAX_LIGHT_TILES_X * K3D_MAX_LIGHT_TILES_Y)
#define K3D_MAX_FRAME_VERTS 65536u
#define K3D_SHADOW_GROUND_Y 0.02f
#define K3D_SHADOW_DEPTH_TOLERANCE 1.75f
#define K3D_SHADOW_HULL_MAX_POINTS 64u
#define K3D_MAX_FAST_SHADOW_HULLS 2048u
#define K3D_FAST_SHADOW_DIRECTIONAL 255u
#define K3D_FAST_SHADOW_RECEIVER_Y_TOLERANCE 1.50f
#define K3D_POINT_SHADOW_MAX_CAST_EXTRA 2.5f
#define K3D_POINT_SHADOW_MAX_CAST_RADIUS_MUL 1.35f
#define K3D_LIGHT_SHADOW_BIAS 0.035f
#define K3D_SURFACE_EPSILON 0.006f

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
    uint8_t texture_opaque;
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
    uint8_t casts_shadow;
    uint16_t mesh_idx;
    float x, y, z;
    float pitch, yaw, roll;
    float sx, sy, sz;
} k3d_instance;

typedef struct
{
    float x, y, z;
    float nx, ny, nz;
    float lr, lg, lb;
    float u, v;
} k3d_pipe_vert;

typedef struct
{
    float sx, sy;
    float inv_z;
    float cam_z;
    float wx, wy, wz;
    float nx, ny, nz;
    float lr, lg, lb;
    float u, v;
} k3d_screen_vert;

typedef struct
{
    k3d_vec3 a, b, c;
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
} k3d_shadow_tri_cache;

typedef struct
{
    uint16_t tri_indices[K3D_SHADOW_CELL_MAX];
    uint16_t count;
} k3d_shadow_cell;

typedef struct
{
    uint8_t count;
    uint8_t light_indices[K3D_TILE_MAX_LIGHTS];
} k3d_light_tile;

typedef struct
{
    float x, z;
} k3d_shadow_hull_point;

typedef struct
{
    uint8_t light_idx;
    uint8_t shade_pct;
    uint8_t count;
    float depth_tolerance;
    float min_x, max_x;
    float min_z, max_z;
    k3d_shadow_hull_point points[K3D_SHADOW_HULL_MAX_POINTS];
} k3d_fast_shadow_hull;

typedef struct
{
    float pos_x, pos_y, pos_z;
    float scale_x, scale_y, scale_z;
    float yaw_sin, yaw_cos;
    float pitch_sin, pitch_cos;
    float roll_sin, roll_cos;
} k3d_instance_xform;

typedef struct
{
    float cam_yaw_sin, cam_yaw_cos;
    float cam_pitch_sin, cam_pitch_cos;
    float near_z;
    float tan_x, tan_y;
    float proj_x, proj_y;
    float screen_w, screen_h;
    k3d_vec3 dir_light_dir;
    k3d_vec3 to_dir_light;
    float ambient_r, ambient_g, ambient_b;
    float dir_r, dir_g, dir_b;
    float dir_intensity;
    uint32_t point_light_scan_count;
    const k3d_shadow_tri_cache *shadow_tris;
    uint32_t shadow_tri_count;
    const k3d_shadow_cell *shadow_grid;
    float shadow_grid_min_x, shadow_grid_min_z;
    float shadow_grid_max_x, shadow_grid_max_z;
    float shadow_grid_cell_x, shadow_grid_cell_z;
    float shadow_grid_inv_cell_x, shadow_grid_inv_cell_z;
    uint8_t shadow_grid_ready;
    const k3d_light_tile *light_tiles;
    uint32_t light_tile_w, light_tile_h;
    const k3d_fast_shadow_hull *fast_shadow_hulls;
    uint32_t fast_shadow_hull_count;
    uint8_t point_light_shadows_enabled;
    uint8_t directional_ray_shadows_enabled;
    uint8_t projected_shadows_enabled;
    uint8_t tiled_lights_enabled;
    uint8_t fog_enabled;
    float fog_start;
    float fog_inv_span;
    float fog_r, fog_g, fog_b;
} k3d_render_ctx;

typedef struct
{
    const uint32_t *texels;
    uint32_t tex_w, tex_h;
    uint32_t tex_mask_w, tex_mask_h;
    uint8_t tex_pow2;
    uint8_t opaque;
    uint32_t diffuse;
    uint8_t fog_enabled;
    float fog_start;
    float fog_inv_span;
    float fog_r, fog_g, fog_b;
} k3d_tri_shade;

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
    uint8_t point_light_shadows_enabled;
    uint8_t directional_ray_shadows_enabled;
    uint8_t projected_shadows_enabled;
    uint8_t tiled_lights_enabled;
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
    k3d_pipe_vert v;
} k3d_shadow_point;

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
static k3d_shadow_tri_cache G_shadow_tri_cache[K3D_MAX_SHADOW_TRIS];
static k3d_shadow_cell G_shadow_grid[K3D_SHADOW_GRID_W * K3D_SHADOW_GRID_H];
static uint16_t G_shadow_tri_seen[K3D_MAX_SHADOW_TRIS];
static uint16_t G_shadow_ray_stamp = 1u;
static k3d_light_tile G_light_tiles[K3D_MAX_LIGHT_TILES];
static k3d_fast_shadow_hull G_fast_shadow_hulls[K3D_MAX_FAST_SHADOW_HULLS];
static k3d_pipe_vert G_frame_world_verts[K3D_MAX_FRAME_VERTS];
static k3d_pipe_vert G_frame_cam_verts[K3D_MAX_FRAME_VERTS];

static float k3d_absf(float v) { return v < 0.0f ? -v : v; }

static float k3d_min3f(float a, float b, float c)
{
    float m = a < b ? a : b;
    return m < c ? m : c;
}

static float k3d_max3f(float a, float b, float c)
{
    float m = a > b ? a : b;
    return m > c ? m : c;
}

static float k3d_clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int32_t k3d_floor_i32(float v)
{
    int32_t i = (int32_t)v;
    return (v < (float)i) ? (i - 1) : i;
}

static int32_t k3d_clamp_i32(int32_t v, int32_t lo, int32_t hi)
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

static uint8_t k3d_is_pow2_u32(uint32_t v)
{
    return v && ((v & (v - 1u)) == 0u);
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

static uint8_t k3d_texture_is_opaque(const kimg *img)
{
    uint32_t count;

    if (!img || !img->px || !img->w || !img->h)
        return 1u;
    count = img->w * img->h;
    for (uint32_t i = 0u; i < count; ++i)
    {
        if ((uint8_t)(img->px[i] >> 24) != 255u)
            return 0u;
    }
    return 1u;
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
        m->texture_opaque = 1u;
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
        in->casts_shadow = 1u;
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

static void k3d_set_instance_casts_shadow(k3d_scene *s, k3d_instance_handle h, uint8_t casts_shadow)
{
    if (!s || h.idx < 0 || (uint32_t)h.idx >= K3D_MAX_INSTANCES)
        return;
    if (s->instances[h.idx].used)
        s->instances[h.idx].casts_shadow = casts_shadow ? 1u : 0u;
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
    s->point_light_shadows_enabled = 0u;
    s->directional_ray_shadows_enabled = 0u;
    s->projected_shadows_enabled = 1u;
    s->tiled_lights_enabled = 1u;
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
    o->u.image.x = 0;
    o->u.image.y = 0;
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

int k3d_scene_set_render_settings(k3d_scene_handle scene, const k3d_render_settings *settings)
{
    k3d_scene *s = k3d_scene_ref(scene);

    if (!s || !settings)
        return -1;
    s->point_light_shadows_enabled = settings->point_light_shadows_enabled ? 1u : 0u;
    s->directional_ray_shadows_enabled = settings->directional_ray_shadows_enabled ? 1u : 0u;
    s->projected_shadows_enabled = settings->projected_shadows_enabled ? 1u : 0u;
    s->tiled_lights_enabled = settings->tiled_lights_enabled ? 1u : 0u;
    return 0;
}

int k3d_scene_get_render_settings(k3d_scene_handle scene, k3d_render_settings *out_settings)
{
    k3d_scene *s = k3d_scene_ref(scene);

    if (!s || !out_settings)
        return -1;
    out_settings->point_light_shadows_enabled = s->point_light_shadows_enabled;
    out_settings->directional_ray_shadows_enabled = s->directional_ray_shadows_enabled;
    out_settings->projected_shadows_enabled = s->projected_shadows_enabled;
    out_settings->tiled_lights_enabled = s->tiled_lights_enabled;
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

    light = (k3d_point_light){(k3d_vec3){-2.8f, 1.2f, 1.5f}, (kcolor){255, 92, 70}, 5.0f, 7.4f, 1u};
    (void)k3d_scene_add_point_light(scene, &light, &(uint32_t){0});
    light = (k3d_point_light){(k3d_vec3){3.2f, 1.4f, 4.8f}, (kcolor){80, 160, 255}, 6.0f, 12.0f, 1u};
    (void)k3d_scene_add_point_light(scene, &light, &(uint32_t){0});
    light = (k3d_point_light){(k3d_vec3){0.0f, 2.2f, 9.0f}, (kcolor){120, 255, 175}, 7.5f, 5.0f, 1u};
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
    k3d_scene *s = k3d_scene_ref(scene);
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

    if (!s)
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
    k3d_set_instance_casts_shadow(s, ignored, 0u);
    if (k3d_scene_new_cube(scene, d.w, wall_t * 0.88f, d.d, d.center_x, d.floor_y + d.h, d.center_z, d.ceiling_color, &ignored) != 0)
        return -1;
    k3d_set_instance_casts_shadow(s, ignored, 0u);
    if (k3d_scene_new_cube(scene, wall_t, d.h, d.d, left_x, wall_y, d.center_z, d.left_wall_color, &ignored) != 0)
        return -1;
    k3d_set_instance_casts_shadow(s, ignored, 0u);
    if (k3d_scene_new_cube(scene, wall_t, d.h, d.d, right_x, wall_y, d.center_z, d.right_wall_color, &ignored) != 0)
        return -1;
    k3d_set_instance_casts_shadow(s, ignored, 0u);
    if (k3d_scene_new_cube(scene, d.w, d.h, wall_t, d.center_x, wall_y, front_z, d.front_wall_color, &ignored) != 0)
        return -1;
    k3d_set_instance_casts_shadow(s, ignored, 0u);
    if (k3d_scene_new_cube(scene, d.w, d.h, wall_t, d.center_x, wall_y, back_z, d.back_wall_color, &ignored) != 0)
        return -1;
    k3d_set_instance_casts_shadow(s, ignored, 0u);

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
    m->texture_opaque = 1u;
    return 0;
}

static int k3d_copy_texture(kimg *dst, const kimg *src)
{
    uint64_t pages = 0;
    uint64_t bytes = 0;

    if (!dst || !src || !src->px || !src->w || !src->h)
        return -1;
    bytes = (uint64_t)src->w * (uint64_t)src->h * 4ull;
    if (bytes == 0ull || bytes > (64ull * 1024ull * 1024ull))
        return -1;
    kimg_zero(dst);
    dst->px = (uint32_t *)k3d_alloc_bytes(bytes, &pages);
    if (!dst->px)
        return -1;
    dst->w = src->w;
    dst->h = src->h;
    memcpy(dst->px, src->px, (size_t)bytes);
    return 0;
}

static uint32_t k3d_text_glyph_advance(const kfont *font, uint32_t gi)
{
    uint32_t adv = font->w;
    if (font->tight_width && gi < font->glyph_count)
    {
        uint32_t tw = font->tight_width[gi];
        if (tw == 0u)
            adv = font->space_advance ? font->space_advance : font->w / 2u;
        else
            adv = tw;
    }
    return adv ? adv : 1u;
}

static void k3d_measure_text_texture(const kfont *font, const char *text, uint32_t scale,
                                     uint32_t *out_w, uint32_t *out_h)
{
    uint32_t line_w = 0u;
    uint32_t max_w = 1u;
    uint32_t lines = 1u;
    uint32_t line_h = ktext_line_height(font, scale, 0);

    for (const char *p = text; p && *p; ++p)
    {
        uint8_t ch = (uint8_t)*p;
        if (*p == '\n')
        {
            if (line_w > max_w)
                max_w = line_w;
            line_w = 0u;
            ++lines;
            continue;
        }
        if (*p == KTEXT_INLINE_COLOR_CTRL)
        {
            if (p[1] == '.')
                p += 1;
            else
                p += 6;
            continue;
        }
        line_w += ktext_scale_mul_px(k3d_text_glyph_advance(font, ch), scale);
    }

    if (line_w > max_w)
        max_w = line_w;
    if (!line_h)
        line_h = font->h ? font->h : 1u;
    *out_w = max_w ? max_w : 1u;
    *out_h = lines * line_h;
    if (!*out_h)
        *out_h = 1u;
}

static int k3d_make_text_texture(kimg *out, const kfont *font, const char *text,
                                 kcolor text_color, uint8_t text_alpha,
                                 kcolor bg_color, uint8_t bg_alpha,
                                 uint32_t scale)
{
    uint32_t tex_w = 0u, tex_h = 0u;
    uint64_t pages = 0;
    uint64_t bytes = 0;
    uint32_t bg = ((uint32_t)bg_alpha << 24) | ((uint32_t)bg_color.r << 16) |
                  ((uint32_t)bg_color.g << 8) | bg_color.b;
    uint32_t fg = ((uint32_t)text_alpha << 24) | ((uint32_t)text_color.r << 16) |
                  ((uint32_t)text_color.g << 8) | text_color.b;
    uint32_t pen_x = 0u;
    uint32_t pen_y = 0u;
    uint32_t line_h = 0u;
    uint32_t glyph_h = 0u;
    uint32_t row_bytes = 0u;

    if (!out || !font || !font->glyphs || !text || scale == 0u)
        return -1;

    k3d_measure_text_texture(font, text, scale, &tex_w, &tex_h);
    bytes = (uint64_t)tex_w * (uint64_t)tex_h * 4ull;
    if (bytes == 0ull || bytes > (16ull * 1024ull * 1024ull))
        return -1;

    kimg_zero(out);
    out->px = (uint32_t *)k3d_alloc_bytes(bytes, &pages);
    if (!out->px)
        return -1;
    out->w = tex_w;
    out->h = tex_h;
    for (uint32_t i = 0u; i < tex_w * tex_h; ++i)
        out->px[i] = bg;

    line_h = ktext_line_height(font, scale, 0);
    glyph_h = ktext_scale_mul_px(font->h, scale);
    if (!line_h)
        line_h = font->h ? font->h : 1u;
    if (!glyph_h)
        glyph_h = font->h ? font->h : 1u;
    row_bytes = (font->w + 7u) >> 3;

    for (const char *p = text; *p; ++p)
    {
        uint32_t gi = (uint8_t)*p;
        uint32_t left = 0u;
        uint32_t tw = font->w;
        uint32_t adv = 0u;
        uint32_t draw_w = 0u;

        if (*p == '\n')
        {
            pen_x = 0u;
            pen_y += line_h;
            continue;
        }
        if (*p == KTEXT_INLINE_COLOR_CTRL)
        {
            if (p[1] == '.')
                p += 1;
            else
                p += 6;
            continue;
        }
        if (gi >= font->glyph_count)
            gi = 0u;
        if (font->tight_left && font->tight_width)
        {
            left = font->tight_left[gi];
            tw = font->tight_width[gi];
        }
        adv = k3d_text_glyph_advance(font, gi);
        draw_w = tw ? ktext_scale_mul_px(tw, scale) : 0u;

        if (draw_w && left < font->w)
        {
            const uint8_t *glyph = font->glyphs + (uint64_t)gi * font->bytes_per_glyph;
            for (uint32_t oy = 0u; oy < glyph_h && pen_y + oy < tex_h; ++oy)
            {
                uint32_t src_y = (oy * font->h) / glyph_h;
                for (uint32_t ox = 0u; ox < draw_w && pen_x + ox < tex_w; ++ox)
                {
                    uint32_t src_x = left + (ox * tw) / draw_w;
                    uint8_t bits;
                    if (src_x >= font->w)
                        continue;
                    bits = glyph[src_y * row_bytes + (src_x >> 3)];
                    if (bits & (uint8_t)(0x80u >> (src_x & 7u)))
                        out->px[(pen_y + oy) * tex_w + pen_x + ox] = fg;
                }
            }
        }
        pen_x += ktext_scale_mul_px(adv, scale);
    }
    return 0;
}

static void k3d_surface_vertices(k3d_vertex *v, uint32_t face, float w, float h)
{
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    float e = K3D_SURFACE_EPSILON;

#define K3D_SURFACE_SET(ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz, nx, ny, nz)       \
    do                                                                                   \
    {                                                                                    \
        v[0] = (k3d_vertex){(ax), (ay), (az), (nx), (ny), (nz), 1.0f, 0.0f};              \
        v[1] = (k3d_vertex){(bx), (by), (bz), (nx), (ny), (nz), 0.0f, 0.0f};              \
        v[2] = (k3d_vertex){(cx), (cy), (cz), (nx), (ny), (nz), 0.0f, 1.0f};              \
        v[3] = (k3d_vertex){(dx), (dy), (dz), (nx), (ny), (nz), 1.0f, 1.0f};              \
    } while (0)

    switch (face)
    {
    case K3D_SURFACE_BACK:
        K3D_SURFACE_SET(hw, -hh, -e, -hw, -hh, -e, -hw, hh, -e, hw, hh, -e, 0.0f, 0.0f, -1.0f);
        break;
    case K3D_SURFACE_RIGHT:
        K3D_SURFACE_SET(e, -hh, hw, e, -hh, -hw, e, hh, -hw, e, hh, hw, 1.0f, 0.0f, 0.0f);
        break;
    case K3D_SURFACE_LEFT:
        K3D_SURFACE_SET(-e, -hh, -hw, -e, -hh, hw, -e, hh, hw, -e, hh, -hw, -1.0f, 0.0f, 0.0f);
        break;
    case K3D_SURFACE_TOP:
        K3D_SURFACE_SET(-hw, e, hw, hw, e, hw, hw, e, -hw, -hw, e, -hw, 0.0f, 1.0f, 0.0f);
        break;
    case K3D_SURFACE_BOTTOM:
        K3D_SURFACE_SET(-hw, -e, -hw, hw, -e, -hw, hw, -e, hw, -hw, -e, hw, 0.0f, -1.0f, 0.0f);
        break;
    case K3D_SURFACE_FRONT:
    default:
        K3D_SURFACE_SET(-hw, -hh, e, hw, -hh, e, hw, hh, e, -hw, hh, e, 0.0f, 0.0f, 1.0f);
        break;
    }
#undef K3D_SURFACE_SET
}

static int k3d_scene_add_surface_owned_texture(k3d_scene *s, kimg *texture,
                                               uint32_t face, float x, float y, float z,
                                               float w, float h,
                                               k3d_instance_handle *out_instance)
{
    uint16_t mat_idx = 0u, mesh_idx = 0u;
    k3d_mesh *m = 0;
    k3d_instance_handle inst;

    if (!s || !texture || !texture->px || !texture->w || !texture->h || !out_instance || w <= 0.0f || h <= 0.0f)
        return -1;
    if (k3d_add_material(s, "surface", (kcolor){255, 255, 255}, &mat_idx) != 0)
        return -1;
    s->materials[mat_idx].texture = *texture;
    s->materials[mat_idx].has_texture = 1u;
    s->materials[mat_idx].texture_opaque = k3d_texture_is_opaque(texture);
    kimg_zero(texture);

    if (k3d_add_mesh(s, &mesh_idx) != 0)
        return -1;
    m = &s->meshes[mesh_idx];
    m->vert_count = 4u;
    m->tri_count = 2u;
    m->verts = (k3d_vertex *)k3d_alloc_bytes(sizeof(k3d_vertex) * m->vert_count, &m->vert_pages);
    m->tris = (k3d_tri *)k3d_alloc_bytes(sizeof(k3d_tri) * m->tri_count, &m->tri_pages);
    if (!m->verts || !m->tris)
    {
        k3d_destroy_mesh(m);
        return -1;
    }

    k3d_surface_vertices(m->verts, face, w, h);
    m->tris[0] = (k3d_tri){0u, 1u, 2u, mat_idx};
    m->tris[1] = (k3d_tri){0u, 2u, 3u, mat_idx};
    if (k3d_add_instance(s, mesh_idx, x, y, z, &inst) != 0)
        return -1;
    k3d_set_instance_casts_shadow(s, inst, 0u);
    *out_instance = inst;
    return 0;
}

int k3d_scene_add_surface_image(k3d_scene_handle scene, const kimg *image,
                                uint32_t face, float x, float y, float z,
                                float w, float h,
                                k3d_instance_handle *out_instance)
{
    k3d_scene *s = k3d_scene_ref(scene);
    kimg texture;

    if (!s || !image || !out_instance)
        return -1;
    if (k3d_copy_texture(&texture, image) != 0)
        return -1;
    if (k3d_scene_add_surface_owned_texture(s, &texture, face, x, y, z, w, h, out_instance) != 0)
    {
        k3d_free_image(&texture);
        return -1;
    }
    return 0;
}

int k3d_scene_add_surface_text(k3d_scene_handle scene, const kfont *font, const char *text,
                               uint32_t face, float x, float y, float z,
                               float w, float h,
                               kcolor text_color, uint8_t text_alpha,
                               kcolor bg_color, uint8_t bg_alpha,
                               uint32_t scale,
                               k3d_instance_handle *out_instance)
{
    k3d_scene *s = k3d_scene_ref(scene);
    kimg texture;

    if (!s || !font || !text || !out_instance)
        return -1;
    if (k3d_make_text_texture(&texture, font, text, text_color, text_alpha, bg_color, bg_alpha, scale ? scale : 1u) != 0)
        return -1;
    if (k3d_scene_add_surface_owned_texture(s, &texture, face, x, y, z, w, h, out_instance) != 0)
    {
        k3d_free_image(&texture);
        return -1;
    }
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

static void k3d_render_ctx_init(const k3d_scene *s, k3d_render_ctx *ctx)
{
    float aspect = (s->h > 0u) ? ((float)s->w / (float)s->h) : 1.0f;
    float tan_y = k3d_tan_deg(s->camera.fov_deg * 0.5f);
    float f;
    float fog_span = s->fog.end - s->fog.start;

    if (k3d_absf(tan_y) < 0.001f)
        tan_y = tan_y < 0.0f ? -0.001f : 0.001f;
    f = 1.0f / tan_y;

    ctx->cam_yaw_sin = k3d_sin_deg(-s->camera.yaw_deg);
    ctx->cam_yaw_cos = k3d_cos_deg(-s->camera.yaw_deg);
    ctx->cam_pitch_sin = k3d_sin_deg(-s->camera.pitch_deg);
    ctx->cam_pitch_cos = k3d_cos_deg(-s->camera.pitch_deg);
    ctx->near_z = s->camera.near_z > 0.001f ? s->camera.near_z : 0.001f;
    ctx->tan_y = tan_y;
    ctx->tan_x = tan_y * aspect;
    ctx->proj_x = (f / aspect) * 0.5f;
    ctx->proj_y = f * 0.5f;
    ctx->screen_w = (float)s->w;
    ctx->screen_h = (float)s->h;

    ctx->dir_light_dir = k3d_vec3_norm(s->dir_light_dir);
    ctx->to_dir_light = (k3d_vec3){-ctx->dir_light_dir.x, -ctx->dir_light_dir.y, -ctx->dir_light_dir.z};
    ctx->ambient_r = s->ambient_intensity * ((float)s->ambient_color.r / 255.0f);
    ctx->ambient_g = s->ambient_intensity * ((float)s->ambient_color.g / 255.0f);
    ctx->ambient_b = s->ambient_intensity * ((float)s->ambient_color.b / 255.0f);
    ctx->dir_r = (float)s->dir_light_color.r / 255.0f;
    ctx->dir_g = (float)s->dir_light_color.g / 255.0f;
    ctx->dir_b = (float)s->dir_light_color.b / 255.0f;
    ctx->dir_intensity = s->dir_light_intensity;
    ctx->shadow_tris = 0;
    ctx->shadow_tri_count = 0u;
    ctx->shadow_grid = 0;
    ctx->shadow_grid_min_x = 0.0f;
    ctx->shadow_grid_min_z = 0.0f;
    ctx->shadow_grid_max_x = 0.0f;
    ctx->shadow_grid_max_z = 0.0f;
    ctx->shadow_grid_cell_x = 1.0f;
    ctx->shadow_grid_cell_z = 1.0f;
    ctx->shadow_grid_inv_cell_x = 1.0f;
    ctx->shadow_grid_inv_cell_z = 1.0f;
    ctx->shadow_grid_ready = 0u;
    ctx->light_tiles = 0;
    ctx->light_tile_w = 0u;
    ctx->light_tile_h = 0u;
    ctx->fast_shadow_hulls = 0;
    ctx->fast_shadow_hull_count = 0u;
    ctx->point_light_shadows_enabled = s->point_light_shadows_enabled ? 1u : 0u;
    ctx->directional_ray_shadows_enabled = s->directional_ray_shadows_enabled ? 1u : 0u;
    ctx->projected_shadows_enabled = s->projected_shadows_enabled ? 1u : 0u;
    ctx->tiled_lights_enabled = s->tiled_lights_enabled ? 1u : 0u;
    ctx->point_light_scan_count = 0u;
    for (uint32_t i = K3D_MAX_POINT_LIGHTS; i > 0u; --i)
    {
        if (s->point_lights[i - 1u].enabled)
        {
            ctx->point_light_scan_count = i;
            break;
        }
    }

    if (fog_span <= 0.01f)
        fog_span = 0.01f;
    ctx->fog_enabled = s->fog.enabled ? 1u : 0u;
    ctx->fog_start = s->fog.start;
    ctx->fog_inv_span = 1.0f / fog_span;
    ctx->fog_r = (float)s->fog.color.r;
    ctx->fog_g = (float)s->fog.color.g;
    ctx->fog_b = (float)s->fog.color.b;
}

static void k3d_instance_xform_init(const k3d_instance *in, k3d_instance_xform *xf)
{
    xf->pos_x = in->x;
    xf->pos_y = in->y;
    xf->pos_z = in->z;
    xf->scale_x = in->sx;
    xf->scale_y = in->sy;
    xf->scale_z = in->sz;
    xf->yaw_sin = k3d_sin_deg(in->yaw);
    xf->yaw_cos = k3d_cos_deg(in->yaw);
    xf->pitch_sin = k3d_sin_deg(in->pitch);
    xf->pitch_cos = k3d_cos_deg(in->pitch);
    xf->roll_sin = k3d_sin_deg(in->roll);
    xf->roll_cos = k3d_cos_deg(in->roll);
}

static void k3d_transform_vertex(const k3d_instance_xform *xf, const k3d_vertex *src, k3d_pipe_vert *dst)
{
    float x = src->x * xf->scale_x;
    float y = src->y * xf->scale_y;
    float z = src->z * xf->scale_z;
    float nx = src->nx, ny = src->ny, nz = src->nz;
    float sy = xf->yaw_sin, cy = xf->yaw_cos;
    float sp = xf->pitch_sin, cp = xf->pitch_cos;
    float sr = xf->roll_sin, cr = xf->roll_cos;
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

    dst->x = x3 + xf->pos_x;
    dst->y = y3 + xf->pos_y;
    dst->z = z2 + xf->pos_z;
    dst->nx = nx3;
    dst->ny = ny3;
    dst->nz = nz2;
    dst->lr = 1.0f;
    dst->lg = 1.0f;
    dst->lb = 1.0f;
    dst->u = src->u;
    dst->v = src->v;
}

static void k3d_shadow_cache_add(k3d_render_ctx *ctx,
                                 const k3d_pipe_vert *a,
                                 const k3d_pipe_vert *b,
                                 const k3d_pipe_vert *c)
{
    k3d_shadow_tri_cache *tri;

    if (!ctx || !a || !b || !c || ctx->shadow_tri_count >= K3D_MAX_SHADOW_TRIS)
        return;
    tri = &G_shadow_tri_cache[ctx->shadow_tri_count++];
    tri->a = (k3d_vec3){a->x, a->y, a->z};
    tri->b = (k3d_vec3){b->x, b->y, b->z};
    tri->c = (k3d_vec3){c->x, c->y, c->z};
    tri->min_x = k3d_min3f(a->x, b->x, c->x) - K3D_LIGHT_SHADOW_BIAS;
    tri->min_y = k3d_min3f(a->y, b->y, c->y) - K3D_LIGHT_SHADOW_BIAS;
    tri->min_z = k3d_min3f(a->z, b->z, c->z) - K3D_LIGHT_SHADOW_BIAS;
    tri->max_x = k3d_max3f(a->x, b->x, c->x) + K3D_LIGHT_SHADOW_BIAS;
    tri->max_y = k3d_max3f(a->y, b->y, c->y) + K3D_LIGHT_SHADOW_BIAS;
    tri->max_z = k3d_max3f(a->z, b->z, c->z) + K3D_LIGHT_SHADOW_BIAS;
}

static void k3d_build_shadow_cache(const k3d_scene *s, k3d_render_ctx *ctx)
{
    if (!s || !ctx)
        return;

    ctx->shadow_tris = G_shadow_tri_cache;
    ctx->shadow_tri_count = 0u;

    for (uint32_t ii = 0u; ii < K3D_MAX_INSTANCES; ++ii)
    {
        const k3d_instance *in = &s->instances[ii];
        k3d_instance_xform xf;
        const k3d_mesh *m;

        if (!in->used || !in->visible || !in->casts_shadow || in->mesh_idx >= K3D_MAX_MESHES)
            continue;
        m = &s->meshes[in->mesh_idx];
        if (!m->used || !m->verts || !m->tris)
            continue;

        k3d_instance_xform_init(in, &xf);
        if (m->vert_count <= K3D_MAX_FRAME_VERTS)
        {
            for (uint32_t vi = 0u; vi < m->vert_count; ++vi)
                k3d_transform_vertex(&xf, &m->verts[vi], &G_frame_world_verts[vi]);
        }
        for (uint32_t ti = 0u; ti < m->tri_count && ctx->shadow_tri_count < K3D_MAX_SHADOW_TRIS; ++ti)
        {
            const k3d_tri *tr = &m->tris[ti];
            k3d_pipe_vert a, b, c;

            if (tr->a >= m->vert_count || tr->b >= m->vert_count || tr->c >= m->vert_count)
                continue;
            if (m->vert_count <= K3D_MAX_FRAME_VERTS)
            {
                k3d_shadow_cache_add(ctx,
                                     &G_frame_world_verts[tr->a],
                                     &G_frame_world_verts[tr->b],
                                     &G_frame_world_verts[tr->c]);
            }
            else
            {
                k3d_transform_vertex(&xf, &m->verts[tr->a], &a);
                k3d_transform_vertex(&xf, &m->verts[tr->b], &b);
                k3d_transform_vertex(&xf, &m->verts[tr->c], &c);
                k3d_shadow_cache_add(ctx, &a, &b, &c);
            }
        }
    }
}

static int32_t k3d_shadow_grid_x(const k3d_render_ctx *ctx, float x)
{
    return k3d_clamp_i32(k3d_floor_i32((x - ctx->shadow_grid_min_x) * ctx->shadow_grid_inv_cell_x),
                         0, (int32_t)K3D_SHADOW_GRID_W - 1);
}

static int32_t k3d_shadow_grid_z(const k3d_render_ctx *ctx, float z)
{
    return k3d_clamp_i32(k3d_floor_i32((z - ctx->shadow_grid_min_z) * ctx->shadow_grid_inv_cell_z),
                         0, (int32_t)K3D_SHADOW_GRID_H - 1);
}

static void k3d_build_shadow_grid(k3d_render_ctx *ctx)
{
    float min_x, min_z, max_x, max_z;
    float span_x, span_z;

    if (!ctx || !ctx->shadow_tris || ctx->shadow_tri_count == 0u)
        return;

    min_x = ctx->shadow_tris[0].min_x;
    min_z = ctx->shadow_tris[0].min_z;
    max_x = ctx->shadow_tris[0].max_x;
    max_z = ctx->shadow_tris[0].max_z;
    for (uint32_t i = 1u; i < ctx->shadow_tri_count; ++i)
    {
        const k3d_shadow_tri_cache *tri = &ctx->shadow_tris[i];
        if (tri->min_x < min_x)
            min_x = tri->min_x;
        if (tri->min_z < min_z)
            min_z = tri->min_z;
        if (tri->max_x > max_x)
            max_x = tri->max_x;
        if (tri->max_z > max_z)
            max_z = tri->max_z;
    }

    min_x -= 0.5f;
    min_z -= 0.5f;
    max_x += 0.5f;
    max_z += 0.5f;
    span_x = max_x - min_x;
    span_z = max_z - min_z;
    if (span_x < 1.0f)
    {
        max_x = min_x + 1.0f;
        span_x = 1.0f;
    }
    if (span_z < 1.0f)
    {
        max_z = min_z + 1.0f;
        span_z = 1.0f;
    }

    memset(G_shadow_grid, 0, sizeof(G_shadow_grid));
    ctx->shadow_grid = G_shadow_grid;
    ctx->shadow_grid_min_x = min_x;
    ctx->shadow_grid_min_z = min_z;
    ctx->shadow_grid_max_x = max_x;
    ctx->shadow_grid_max_z = max_z;
    ctx->shadow_grid_cell_x = span_x / (float)K3D_SHADOW_GRID_W;
    ctx->shadow_grid_cell_z = span_z / (float)K3D_SHADOW_GRID_H;
    ctx->shadow_grid_inv_cell_x = 1.0f / ctx->shadow_grid_cell_x;
    ctx->shadow_grid_inv_cell_z = 1.0f / ctx->shadow_grid_cell_z;
    ctx->shadow_grid_ready = 1u;

    for (uint32_t i = 0u; i < ctx->shadow_tri_count; ++i)
    {
        const k3d_shadow_tri_cache *tri = &ctx->shadow_tris[i];
        int32_t x0 = k3d_shadow_grid_x(ctx, tri->min_x);
        int32_t x1 = k3d_shadow_grid_x(ctx, tri->max_x);
        int32_t z0 = k3d_shadow_grid_z(ctx, tri->min_z);
        int32_t z1 = k3d_shadow_grid_z(ctx, tri->max_z);

        for (int32_t z = z0; z <= z1; ++z)
        {
            for (int32_t x = x0; x <= x1; ++x)
            {
                k3d_shadow_cell *cell = &G_shadow_grid[(uint32_t)z * K3D_SHADOW_GRID_W + (uint32_t)x];
                if (cell->count >= K3D_SHADOW_CELL_MAX)
                {
                    ctx->shadow_grid_ready = 0u;
                    ctx->shadow_grid = 0;
                    return;
                }
                cell->tri_indices[cell->count++] = (uint16_t)i;
            }
        }
    }
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
    o.lr = a.lr + (b.lr - a.lr) * t;
    o.lg = a.lg + (b.lg - a.lg) * t;
    o.lb = a.lb + (b.lb - a.lb) * t;
    o.u = a.u + (b.u - a.u) * t;
    o.v = a.v + (b.v - a.v) * t;
    return o;
}

static int k3d_camera_transform(const k3d_scene *s, const k3d_render_ctx *ctx,
                                const k3d_pipe_vert *src, k3d_pipe_vert *dst)
{
    float x = src->x - s->camera.pos.x;
    float y = src->y - s->camera.pos.y;
    float z = src->z - s->camera.pos.z;
    float sy = ctx->cam_yaw_sin, cy = ctx->cam_yaw_cos;
    float sp = ctx->cam_pitch_sin, cp = ctx->cam_pitch_cos;
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

static void k3d_camera_transform_point(const k3d_scene *s, const k3d_render_ctx *ctx,
                                       k3d_vec3 p, float *out_x, float *out_y, float *out_z)
{
    float x = p.x - s->camera.pos.x;
    float y = p.y - s->camera.pos.y;
    float z = p.z - s->camera.pos.z;
    float x1 = x * ctx->cam_yaw_cos + z * ctx->cam_yaw_sin;
    float z1 = -x * ctx->cam_yaw_sin + z * ctx->cam_yaw_cos;
    float y2 = y * ctx->cam_pitch_cos - z1 * ctx->cam_pitch_sin;
    float z2 = y * ctx->cam_pitch_sin + z1 * ctx->cam_pitch_cos;

    *out_x = x1;
    *out_y = y2;
    *out_z = z2;
}

static void k3d_light_tile_add(uint32_t tile_x, uint32_t tile_y, uint32_t light_idx)
{
    k3d_light_tile *tile = &G_light_tiles[tile_y * K3D_MAX_LIGHT_TILES_X + tile_x];

    if (tile->count >= K3D_TILE_MAX_LIGHTS || light_idx > 255u)
        return;
    tile->light_indices[tile->count++] = (uint8_t)light_idx;
}

static void k3d_build_light_tiles(const k3d_scene *s, k3d_render_ctx *ctx)
{
    if (!s || !ctx || !ctx->tiled_lights_enabled || ctx->point_light_scan_count == 0u)
        return;

    ctx->light_tile_w = (s->w + K3D_SCREEN_TILE - 1u) / K3D_SCREEN_TILE;
    ctx->light_tile_h = (s->h + K3D_SCREEN_TILE - 1u) / K3D_SCREEN_TILE;
    if (!ctx->light_tile_w || !ctx->light_tile_h ||
        ctx->light_tile_w > K3D_MAX_LIGHT_TILES_X ||
        ctx->light_tile_h > K3D_MAX_LIGHT_TILES_Y)
    {
        ctx->light_tile_w = 0u;
        ctx->light_tile_h = 0u;
        return;
    }

    memset(G_light_tiles, 0, sizeof(G_light_tiles));
    ctx->light_tiles = G_light_tiles;

    for (uint32_t li = 0u; li < ctx->point_light_scan_count; ++li)
    {
        const k3d_point_light *pl = &s->point_lights[li];
        float cx, cy, cz;
        int32_t min_tx, max_tx, min_ty, max_ty;

        if (!pl->enabled || pl->radius <= 0.01f || pl->intensity <= 0.0f)
            continue;

        k3d_camera_transform_point(s, ctx, pl->pos, &cx, &cy, &cz);
        if (cz <= ctx->near_z)
        {
            if (cz + pl->radius <= ctx->near_z)
                continue;
            min_tx = 0;
            min_ty = 0;
            max_tx = (int32_t)ctx->light_tile_w - 1;
            max_ty = (int32_t)ctx->light_tile_h - 1;
        }
        else
        {
            float inv_z = 1.0f / cz;
            float sx = ((cx * inv_z) * ctx->proj_x + 0.5f) * ctx->screen_w;
            float sy = (0.5f - (cy * inv_z) * ctx->proj_y) * ctx->screen_h;
            float rx = pl->radius * ctx->proj_x * ctx->screen_w * inv_z;
            float ry = pl->radius * ctx->proj_y * ctx->screen_h * inv_z;
            float sr = (rx > ry ? rx : ry) + (float)K3D_SCREEN_TILE;

            if (sx + sr < 0.0f || sy + sr < 0.0f ||
                sx - sr >= ctx->screen_w || sy - sr >= ctx->screen_h)
                continue;

            min_tx = k3d_floor_i32((sx - sr) / (float)K3D_SCREEN_TILE);
            max_tx = k3d_floor_i32((sx + sr) / (float)K3D_SCREEN_TILE);
            min_ty = k3d_floor_i32((sy - sr) / (float)K3D_SCREEN_TILE);
            max_ty = k3d_floor_i32((sy + sr) / (float)K3D_SCREEN_TILE);
            min_tx = k3d_clamp_i32(min_tx, 0, (int32_t)ctx->light_tile_w - 1);
            max_tx = k3d_clamp_i32(max_tx, 0, (int32_t)ctx->light_tile_w - 1);
            min_ty = k3d_clamp_i32(min_ty, 0, (int32_t)ctx->light_tile_h - 1);
            max_ty = k3d_clamp_i32(max_ty, 0, (int32_t)ctx->light_tile_h - 1);
        }

        for (int32_t ty = min_ty; ty <= max_ty; ++ty)
        {
            for (int32_t tx = min_tx; tx <= max_tx; ++tx)
                k3d_light_tile_add((uint32_t)tx, (uint32_t)ty, li);
        }
    }
}

static const k3d_light_tile *k3d_ctx_light_tile(const k3d_render_ctx *ctx, int32_t px, int32_t py)
{
    uint32_t tx, ty;

    if (!ctx || !ctx->light_tiles || !ctx->light_tile_w || !ctx->light_tile_h || px < 0 || py < 0)
        return 0;
    tx = (uint32_t)px / K3D_SCREEN_TILE;
    ty = (uint32_t)py / K3D_SCREEN_TILE;
    if (tx >= ctx->light_tile_w || ty >= ctx->light_tile_h)
        return 0;
    return &ctx->light_tiles[ty * K3D_MAX_LIGHT_TILES_X + tx];
}

static uint8_t k3d_point_in_fast_shadow_hull_xz(const k3d_fast_shadow_hull *h, float x, float z)
{
    uint8_t inside = 0u;
    uint32_t j;

    if (!h || h->count < 3u ||
        x < h->min_x || x > h->max_x ||
        z < h->min_z || z > h->max_z)
    {
        return 0u;
    }

    j = (uint32_t)h->count - 1u;
    for (uint32_t i = 0u; i < h->count; ++i)
    {
        float zi = h->points[i].z;
        float zj = h->points[j].z;
        if ((zi > z) != (zj > z))
        {
            float denom = zj - zi;
            if (k3d_absf(denom) > 0.00001f)
            {
                float ix = h->points[i].x + (z - zi) * (h->points[j].x - h->points[i].x) / denom;
                if (x < ix)
                    inside = inside ? 0u : 1u;
            }
        }
        j = i;
    }
    return inside;
}

static uint8_t k3d_fast_shadow_blocks_light(const k3d_render_ctx *ctx, uint8_t light_idx,
                                            float wx, float wy, float wz)
{
    if (!ctx || !ctx->projected_shadows_enabled || !ctx->fast_shadow_hulls ||
        ctx->fast_shadow_hull_count == 0u)
    {
        return 0u;
    }
    if (wy < K3D_SHADOW_GROUND_Y - 0.50f ||
        wy > K3D_SHADOW_GROUND_Y + K3D_FAST_SHADOW_RECEIVER_Y_TOLERANCE)
    {
        return 0u;
    }

    for (uint32_t i = 0u; i < ctx->fast_shadow_hull_count; ++i)
    {
        const k3d_fast_shadow_hull *h = &ctx->fast_shadow_hulls[i];
        if (h->light_idx != light_idx)
            continue;
        if (k3d_point_in_fast_shadow_hull_xz(h, wx, wz))
            return 1u;
    }
    return 0u;
}

static uint8_t k3d_fast_shadow_blocks_any_light(const k3d_render_ctx *ctx,
                                                float wx, float wy, float wz)
{
    if (!ctx || !ctx->projected_shadows_enabled || !ctx->fast_shadow_hulls ||
        ctx->fast_shadow_hull_count == 0u)
    {
        return 0u;
    }
    if (wy < K3D_SHADOW_GROUND_Y - 0.50f ||
        wy > K3D_SHADOW_GROUND_Y + K3D_FAST_SHADOW_RECEIVER_Y_TOLERANCE)
    {
        return 0u;
    }

    for (uint32_t i = 0u; i < ctx->fast_shadow_hull_count; ++i)
    {
        if (k3d_point_in_fast_shadow_hull_xz(&ctx->fast_shadow_hulls[i], wx, wz))
            return 1u;
    }
    return 0u;
}

static void k3d_tri_shade_init(const k3d_scene *s, const k3d_render_ctx *ctx, uint16_t mat_idx,
                               k3d_tri_shade *out)
{
    const k3d_material *m = 0;

    out->texels = 0;
    out->tex_w = 0u;
    out->tex_h = 0u;
    out->tex_mask_w = 0u;
    out->tex_mask_h = 0u;
    out->tex_pow2 = 0u;
    out->opaque = 1u;
    out->diffuse = 0xFFFFFFFFu;
    out->fog_enabled = ctx->fog_enabled;
    out->fog_start = ctx->fog_start;
    out->fog_inv_span = ctx->fog_inv_span;
    out->fog_r = ctx->fog_r;
    out->fog_g = ctx->fog_g;
    out->fog_b = ctx->fog_b;

    if (s && mat_idx < K3D_MAX_MATERIALS && s->materials[mat_idx].used)
    {
        m = &s->materials[mat_idx];
        out->diffuse = 0xFF000000u | ((uint32_t)m->diffuse.r << 16) |
                       ((uint32_t)m->diffuse.g << 8) | m->diffuse.b;
        if (m->has_texture && m->texture.px && m->texture.w && m->texture.h)
        {
            out->texels = m->texture.px;
            out->tex_w = m->texture.w;
            out->tex_h = m->texture.h;
            out->tex_mask_w = m->texture.w - 1u;
            out->tex_mask_h = m->texture.h - 1u;
            out->tex_pow2 = (uint8_t)(k3d_is_pow2_u32(m->texture.w) &&
                                      k3d_is_pow2_u32(m->texture.h));
            out->opaque = m->texture_opaque ? 1u : 0u;
        }
    }
}

static uint32_t k3d_sample_tri_shade(const k3d_tri_shade *shade, float u, float v)
{
    if (shade->texels && shade->tex_w && shade->tex_h)
    {
        int32_t ix = (int32_t)(u * (float)shade->tex_w);
        int32_t iy = (int32_t)((1.0f - v) * (float)shade->tex_h);
        if (shade->tex_pow2)
        {
            ix = (int32_t)((uint32_t)ix & shade->tex_mask_w);
            iy = (int32_t)((uint32_t)iy & shade->tex_mask_h);
        }
        else
        {
            if (ix < 0)
                ix = 0;
            else if (ix >= (int32_t)shade->tex_w)
                ix = (int32_t)shade->tex_w - 1;
            if (iy < 0)
                iy = 0;
            else if (iy >= (int32_t)shade->tex_h)
                iy = (int32_t)shade->tex_h - 1;
        }
        return shade->texels[(uint32_t)iy * shade->tex_w + (uint32_t)ix];
    }
    return shade->diffuse;
}

static uint32_t k3d_shade_texel_fast(const k3d_tri_shade *shade, uint32_t tex, float z,
                                     float light_r, float light_g, float light_b)
{
    float r = (float)((tex >> 16) & 255u) * light_r;
    float g = (float)((tex >> 8) & 255u) * light_g;
    float b = (float)(tex & 255u) * light_b;

    if (shade->fog_enabled)
    {
        float t = k3d_clampf((z - shade->fog_start) * shade->fog_inv_span, 0.0f, 1.0f);
        r = r + (shade->fog_r - r) * t;
        g = g + (shade->fog_g - g) * t;
        b = b + (shade->fog_b - b) * t;
    }

    return 0xFF000000u | ((uint32_t)k3d_clamp_u8(r) << 16) |
           ((uint32_t)k3d_clamp_u8(g) << 8) | k3d_clamp_u8(b);
}

static uint32_t k3d_blend_argb(uint32_t dst, uint32_t src, uint8_t alpha)
{
    uint32_t inv = 255u - (uint32_t)alpha;
    uint32_t sr = (src >> 16) & 255u;
    uint32_t sg = (src >> 8) & 255u;
    uint32_t sb = src & 255u;
    uint32_t dr = (dst >> 16) & 255u;
    uint32_t dg = (dst >> 8) & 255u;
    uint32_t db = dst & 255u;

    dr = (sr * (uint32_t)alpha + dr * inv + 127u) / 255u;
    dg = (sg * (uint32_t)alpha + dg * inv + 127u) / 255u;
    db = (sb * (uint32_t)alpha + db * inv + 127u) / 255u;

    return 0xFF000000u | (dr << 16) | (dg << 8) | db;
}

static uint8_t k3d_ray_hits_tri(k3d_vec3 o, k3d_vec3 d, float max_t,
                                k3d_vec3 a, k3d_vec3 b, k3d_vec3 c)
{
    k3d_vec3 e1 = {b.x - a.x, b.y - a.y, b.z - a.z};
    k3d_vec3 e2 = {c.x - a.x, c.y - a.y, c.z - a.z};
    k3d_vec3 p = k3d_cross3(d, e2);
    float det = k3d_dot3(e1, p);
    float inv_det;
    k3d_vec3 tv;
    float u, v, t;

    if (det > -0.00001f && det < 0.00001f)
        return 0u;
    inv_det = 1.0f / det;
    tv = (k3d_vec3){o.x - a.x, o.y - a.y, o.z - a.z};
    u = k3d_dot3(tv, p) * inv_det;
    if (u < 0.0f || u > 1.0f)
        return 0u;
    {
        k3d_vec3 q = k3d_cross3(tv, e1);
        v = k3d_dot3(d, q) * inv_det;
        if (v < 0.0f || u + v > 1.0f)
            return 0u;
        t = k3d_dot3(e2, q) * inv_det;
    }
    return (t > K3D_LIGHT_SHADOW_BIAS && t < max_t) ? 1u : 0u;
}

static uint8_t k3d_ray_hits_shadow_aabb(const k3d_shadow_tri_cache *tri, k3d_vec3 o, k3d_vec3 d, float max_t)
{
    float tmin = K3D_LIGHT_SHADOW_BIAS;
    float tmax = max_t;

#define K3D_RAY_AABB_AXIS(origin, dir, mn, mx)                     \
    do                                                             \
    {                                                              \
        if (k3d_absf(dir) < 0.00001f)                              \
        {                                                          \
            if ((origin) < (mn) || (origin) > (mx))                 \
                return 0u;                                         \
        }                                                          \
        else                                                       \
        {                                                          \
            float inv_d = 1.0f / (dir);                            \
            float t1 = ((mn) - (origin)) * inv_d;                  \
            float t2 = ((mx) - (origin)) * inv_d;                  \
            if (t1 > t2)                                           \
            {                                                      \
                float tmp = t1;                                    \
                t1 = t2;                                           \
                t2 = tmp;                                          \
            }                                                      \
            if (t1 > tmin)                                         \
                tmin = t1;                                         \
            if (t2 < tmax)                                         \
                tmax = t2;                                         \
            if (tmin > tmax)                                       \
                return 0u;                                         \
        }                                                          \
    } while (0)

    K3D_RAY_AABB_AXIS(o.x, d.x, tri->min_x, tri->max_x);
    K3D_RAY_AABB_AXIS(o.y, d.y, tri->min_y, tri->max_y);
    K3D_RAY_AABB_AXIS(o.z, d.z, tri->min_z, tri->max_z);
#undef K3D_RAY_AABB_AXIS

    return 1u;
}

static uint16_t k3d_next_shadow_ray_stamp(void)
{
    ++G_shadow_ray_stamp;
    if (G_shadow_ray_stamp == 0u)
    {
        memset(G_shadow_tri_seen, 0, sizeof(G_shadow_tri_seen));
        G_shadow_ray_stamp = 1u;
    }
    return G_shadow_ray_stamp;
}

static uint8_t k3d_light_ray_blocked_full(const k3d_render_ctx *ctx,
                                          k3d_vec3 o, k3d_vec3 d, float max_t)
{
    if (!ctx || !ctx->shadow_tris || ctx->shadow_tri_count == 0u || max_t <= K3D_LIGHT_SHADOW_BIAS)
        return 0u;

    for (uint32_t i = 0u; i < ctx->shadow_tri_count; ++i)
    {
        const k3d_shadow_tri_cache *tri = &ctx->shadow_tris[i];
        if (!k3d_ray_hits_shadow_aabb(tri, o, d, max_t))
            continue;
        if (k3d_ray_hits_tri(o, d, max_t, tri->a, tri->b, tri->c))
        {
            return 1u;
        }
    }
    return 0u;
}

static uint8_t k3d_shadow_grid_clip_axis(float origin, float dir, float mn, float mx,
                                         float *tmin, float *tmax)
{
    if (k3d_absf(dir) < 0.00001f)
        return (origin >= mn && origin <= mx) ? 1u : 0u;
    else
    {
        float inv = 1.0f / dir;
        float t0 = (mn - origin) * inv;
        float t1 = (mx - origin) * inv;
        if (t0 > t1)
        {
            float tmp = t0;
            t0 = t1;
            t1 = tmp;
        }
        if (t0 > *tmin)
            *tmin = t0;
        if (t1 < *tmax)
            *tmax = t1;
        return (*tmin <= *tmax) ? 1u : 0u;
    }
}

static uint8_t k3d_light_ray_blocked_grid(const k3d_render_ctx *ctx,
                                          k3d_vec3 o, k3d_vec3 d, float max_t)
{
    float tmin = K3D_LIGHT_SHADOW_BIAS;
    float tmax = max_t;
    float sx, sz, ex, ez;
    int32_t cell_x, cell_z, end_x, end_z;
    int32_t step_x, step_z;
    float next_x, next_z;
    float t_max_x, t_max_z;
    float t_delta_x, t_delta_z;
    uint16_t stamp;

    if (!ctx || !ctx->shadow_grid_ready || !ctx->shadow_grid || !ctx->shadow_tris)
        return k3d_light_ray_blocked_full(ctx, o, d, max_t);
    if (max_t <= K3D_LIGHT_SHADOW_BIAS)
        return 0u;
    if (!k3d_shadow_grid_clip_axis(o.x, d.x, ctx->shadow_grid_min_x, ctx->shadow_grid_max_x, &tmin, &tmax) ||
        !k3d_shadow_grid_clip_axis(o.z, d.z, ctx->shadow_grid_min_z, ctx->shadow_grid_max_z, &tmin, &tmax))
        return 0u;
    if (tmin < K3D_LIGHT_SHADOW_BIAS)
        tmin = K3D_LIGHT_SHADOW_BIAS;
    if (tmax > max_t)
        tmax = max_t;
    if (tmin > tmax)
        return 0u;

    sx = o.x + d.x * tmin;
    sz = o.z + d.z * tmin;
    ex = o.x + d.x * tmax;
    ez = o.z + d.z * tmax;
    cell_x = k3d_shadow_grid_x(ctx, sx);
    cell_z = k3d_shadow_grid_z(ctx, sz);
    end_x = k3d_shadow_grid_x(ctx, ex);
    end_z = k3d_shadow_grid_z(ctx, ez);
    step_x = d.x >= 0.0f ? 1 : -1;
    step_z = d.z >= 0.0f ? 1 : -1;

    if (k3d_absf(d.x) < 0.00001f)
    {
        t_max_x = 1.0e30f;
        t_delta_x = 1.0e30f;
    }
    else
    {
        next_x = ctx->shadow_grid_min_x +
                 (float)(cell_x + (step_x > 0 ? 1 : 0)) * ctx->shadow_grid_cell_x;
        t_max_x = (next_x - o.x) / d.x;
        t_delta_x = ctx->shadow_grid_cell_x / k3d_absf(d.x);
    }

    if (k3d_absf(d.z) < 0.00001f)
    {
        t_max_z = 1.0e30f;
        t_delta_z = 1.0e30f;
    }
    else
    {
        next_z = ctx->shadow_grid_min_z +
                 (float)(cell_z + (step_z > 0 ? 1 : 0)) * ctx->shadow_grid_cell_z;
        t_max_z = (next_z - o.z) / d.z;
        t_delta_z = ctx->shadow_grid_cell_z / k3d_absf(d.z);
    }

    stamp = k3d_next_shadow_ray_stamp();
    for (;;)
    {
        const k3d_shadow_cell *cell;

        if (cell_x < 0 || cell_x >= (int32_t)K3D_SHADOW_GRID_W ||
            cell_z < 0 || cell_z >= (int32_t)K3D_SHADOW_GRID_H)
            break;

        cell = &ctx->shadow_grid[(uint32_t)cell_z * K3D_SHADOW_GRID_W + (uint32_t)cell_x];
        for (uint32_t i = 0u; i < cell->count; ++i)
        {
            uint32_t tri_idx = cell->tri_indices[i];
            const k3d_shadow_tri_cache *tri;

            if (tri_idx >= ctx->shadow_tri_count || G_shadow_tri_seen[tri_idx] == stamp)
                continue;
            G_shadow_tri_seen[tri_idx] = stamp;
            tri = &ctx->shadow_tris[tri_idx];
            if (!k3d_ray_hits_shadow_aabb(tri, o, d, max_t))
                continue;
            if (k3d_ray_hits_tri(o, d, max_t, tri->a, tri->b, tri->c))
                return 1u;
        }

        if (cell_x == end_x && cell_z == end_z)
            break;
        if (t_max_x < t_max_z)
        {
            if (t_max_x > tmax)
                break;
            cell_x += step_x;
            t_max_x += t_delta_x;
        }
        else
        {
            if (t_max_z > tmax)
                break;
            cell_z += step_z;
            t_max_z += t_delta_z;
        }
    }

    return 0u;
}

static uint8_t k3d_light_ray_blocked(const k3d_render_ctx *ctx,
                                     float ox, float oy, float oz,
                                     float dx, float dy, float dz,
                                     float max_t)
{
    k3d_vec3 o = {ox, oy, oz};
    k3d_vec3 d = {dx, dy, dz};

    if (ctx && ctx->shadow_grid_ready)
        return k3d_light_ray_blocked_grid(ctx, o, d, max_t);
    return k3d_light_ray_blocked_full(ctx, o, d, max_t);
}

static void k3d_accum_point_light(const k3d_scene *s, const k3d_render_ctx *ctx,
                                  uint32_t light_idx, k3d_vec3 n,
                                  float wx, float wy, float wz,
                                  uint8_t use_fast_shadows,
                                  float *br, float *bg, float *bb)
{
    const k3d_point_light *pl;
    float lx, ly, lz, dist2, r2, dist, inv, atten, pd;

    if (!s || !ctx || !br || !bg || !bb || light_idx >= K3D_MAX_POINT_LIGHTS)
        return;
    pl = &s->point_lights[light_idx];
    if (!pl->enabled)
        return;
    lx = pl->pos.x - wx;
    ly = pl->pos.y - wy;
    lz = pl->pos.z - wz;
    dist2 = lx * lx + ly * ly + lz * lz;
    r2 = pl->radius * pl->radius;
    if (dist2 >= r2 || r2 <= 0.0001f)
        return;
    if (use_fast_shadows && k3d_fast_shadow_blocks_light(ctx, (uint8_t)light_idx, wx, wy, wz))
        return;
    dist = k3d_sqrtf(dist2);
    inv = dist > 0.0001f ? 1.0f / dist : 1.0f;
    atten = 1.0f - (dist2 / r2);
    pd = k3d_clampf(n.x * lx * inv + n.y * ly * inv + n.z * lz * inv, 0.0f, 1.0f) *
         atten * atten * pl->intensity;
    if (pd <= 0.0f)
        return;
    if (ctx->point_light_shadows_enabled &&
        k3d_light_ray_blocked(ctx,
                              wx + n.x * K3D_LIGHT_SHADOW_BIAS,
                              wy + n.y * K3D_LIGHT_SHADOW_BIAS,
                              wz + n.z * K3D_LIGHT_SHADOW_BIAS,
                              lx * inv, ly * inv, lz * inv,
                              dist - K3D_LIGHT_SHADOW_BIAS))
    {
        return;
    }
    *br += pd * ((float)pl->color.r / 255.0f);
    *bg += pd * ((float)pl->color.g / 255.0f);
    *bb += pd * ((float)pl->color.b / 255.0f);
}

static void k3d_light_at(const k3d_scene *s, const k3d_render_ctx *ctx,
                         const k3d_light_tile *tile,
                         uint8_t use_fast_shadows,
                         float wx, float wy, float wz,
                         float nx, float ny, float nz,
                         float z,
                         float *out_r, float *out_g, float *out_b)
{
    k3d_vec3 n = k3d_vec3_norm((k3d_vec3){nx, ny, nz});
    float br = ctx->ambient_r;
    float bg = ctx->ambient_g;
    float bb = ctx->ambient_b;
    float ndl = k3d_clampf(k3d_dot3(n, ctx->to_dir_light), 0.0f, 1.0f) * ctx->dir_intensity;

    if (ndl > 0.0f &&
        (!use_fast_shadows ||
         !k3d_fast_shadow_blocks_light(ctx, K3D_FAST_SHADOW_DIRECTIONAL, wx, wy, wz)) &&
        (!ctx->directional_ray_shadows_enabled ||
         !k3d_light_ray_blocked(ctx,
                                wx + n.x * K3D_LIGHT_SHADOW_BIAS,
                                wy + n.y * K3D_LIGHT_SHADOW_BIAS,
                                wz + n.z * K3D_LIGHT_SHADOW_BIAS,
                                ctx->to_dir_light.x, ctx->to_dir_light.y, ctx->to_dir_light.z,
                                s->camera.far_z)))
    {
        br += ndl * ctx->dir_r;
        bg += ndl * ctx->dir_g;
        bb += ndl * ctx->dir_b;
    }

    if (ctx->tiled_lights_enabled && tile)
    {
        for (uint32_t i = 0u; i < tile->count; ++i)
            k3d_accum_point_light(s, ctx, tile->light_indices[i], n, wx, wy, wz,
                                  use_fast_shadows, &br, &bg, &bb);
    }
    else
    {
        for (uint32_t i = 0u; i < ctx->point_light_scan_count; ++i)
            k3d_accum_point_light(s, ctx, i, n, wx, wy, wz,
                                  use_fast_shadows, &br, &bg, &bb);
    }

    {
        float ao = 0.76f + 0.20f * k3d_absf(n.y) + 0.04f * k3d_clampf(1.0f - z / 80.0f, 0.0f, 1.0f);
        *out_r = br * ao;
        *out_g = bg * ao;
        *out_b = bb * ao;
    }
}

static void k3d_light_from_interp(const k3d_scene *s, const k3d_render_ctx *ctx,
                                  const k3d_light_tile *tile,
                                  float iz,
                                  float wxiz, float wyiz, float wziz,
                                  float nxiz, float nyiz, float nziz,
                                  float *out_r, float *out_g, float *out_b)
{
    if (iz <= 0.0f)
    {
        *out_r = ctx->ambient_r;
        *out_g = ctx->ambient_g;
        *out_b = ctx->ambient_b;
        return;
    }

    {
        float z = 1.0f / iz;
        k3d_light_at(s, ctx, tile, 0u, wxiz * z, wyiz * z, wziz * z,
                     nxiz * z, nyiz * z, nziz * z, z,
                     out_r, out_g, out_b);
    }
}

static float k3d_edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void k3d_raster_tri(k3d_scene *s, const k3d_render_ctx *ctx, const k3d_tri_shade *shade,
                           const k3d_screen_vert *a,
                           const k3d_screen_vert *b,
                           const k3d_screen_vert *c)
{
    float area = k3d_edge(a->sx, a->sy, b->sx, b->sy, c->sx, c->sy);
    int32_t min_x, max_x, min_y, max_y;
    float min_sx, max_sx, min_sy, max_sy;
    float inv_area;
    float e0_dx, e1_dx, e2_dx;
    float e0_dy, e1_dy, e2_dy;
    float start_x, start_y;
    float e0_start, e1_start, e2_start;
    float iz_a, iz_b, iz_c;
    float uiz_a, uiz_b, uiz_c;
    float viz_a, viz_b, viz_c;
    float wxiz_a, wxiz_b, wxiz_c;
    float wyiz_a, wyiz_b, wyiz_c;
    float wziz_a, wziz_b, wziz_c;
    float nxiz_a, nxiz_b, nxiz_c;
    float nyiz_a, nyiz_b, nyiz_c;
    float nziz_a, nziz_b, nziz_c;
    float iz_start, iz_dx, iz_dy;
    float uiz_start, uiz_dx, uiz_dy;
    float viz_start, viz_dx, viz_dy;
    float wxiz_start, wxiz_dx, wxiz_dy;
    float wyiz_start, wyiz_dx, wyiz_dy;
    float wziz_start, wziz_dx, wziz_dy;
    float nxiz_start, nxiz_dx, nxiz_dy;
    float nyiz_start, nyiz_dx, nyiz_dy;
    float nziz_start, nziz_dx, nziz_dy;
    uint8_t flip_edges;
    uint8_t use_pixel_fast_shadows = ctx->fast_shadow_hull_count ? 1u : 0u;

    if (k3d_absf(area) < 0.0001f)
        return;
    flip_edges = area < 0.0f ? 1u : 0u;
    if (flip_edges)
        area = -area;
    min_sx = k3d_min3f(a->sx, b->sx, c->sx) - 1.0f;
    max_sx = k3d_max3f(a->sx, b->sx, c->sx) + 1.0f;
    min_sy = k3d_min3f(a->sy, b->sy, c->sy) - 1.0f;
    max_sy = k3d_max3f(a->sy, b->sy, c->sy) + 1.0f;
    if (max_sx < 0.0f || max_sy < 0.0f || min_sx > (float)(s->w - 1u) || min_sy > (float)(s->h - 1u))
        return;

    inv_area = 1.0f / area;
    min_x = (int32_t)k3d_clampf(min_sx, 0.0f, (float)(s->w - 1u));
    max_x = (int32_t)k3d_clampf(max_sx, 0.0f, (float)(s->w - 1u));
    min_y = (int32_t)k3d_clampf(min_sy, 0.0f, (float)(s->h - 1u));
    max_y = (int32_t)k3d_clampf(max_sy, 0.0f, (float)(s->h - 1u));

    e0_dx = c->sy - b->sy;
    e0_dy = b->sx - c->sx;
    e1_dx = a->sy - c->sy;
    e1_dy = c->sx - a->sx;
    e2_dx = b->sy - a->sy;
    e2_dy = a->sx - b->sx;

    start_x = (float)min_x + 0.5f;
    start_y = (float)min_y + 0.5f;
    e0_start = k3d_edge(b->sx, b->sy, c->sx, c->sy, start_x, start_y);
    e1_start = k3d_edge(c->sx, c->sy, a->sx, a->sy, start_x, start_y);
    e2_start = k3d_edge(a->sx, a->sy, b->sx, b->sy, start_x, start_y);
    if (flip_edges)
    {
        e0_dx = -e0_dx;
        e1_dx = -e1_dx;
        e2_dx = -e2_dx;
        e0_dy = -e0_dy;
        e1_dy = -e1_dy;
        e2_dy = -e2_dy;
        e0_start = -e0_start;
        e1_start = -e1_start;
        e2_start = -e2_start;
    }

    iz_a = a->inv_z;
    iz_b = b->inv_z;
    iz_c = c->inv_z;
    uiz_a = a->u * iz_a;
    uiz_b = b->u * iz_b;
    uiz_c = c->u * iz_c;
    viz_a = a->v * iz_a;
    viz_b = b->v * iz_b;
    viz_c = c->v * iz_c;
    wxiz_a = a->wx * iz_a;
    wxiz_b = b->wx * iz_b;
    wxiz_c = c->wx * iz_c;
    wyiz_a = a->wy * iz_a;
    wyiz_b = b->wy * iz_b;
    wyiz_c = c->wy * iz_c;
    wziz_a = a->wz * iz_a;
    wziz_b = b->wz * iz_b;
    wziz_c = c->wz * iz_c;
    nxiz_a = a->nx * iz_a;
    nxiz_b = b->nx * iz_b;
    nxiz_c = c->nx * iz_c;
    nyiz_a = a->ny * iz_a;
    nyiz_b = b->ny * iz_b;
    nyiz_c = c->ny * iz_c;
    nziz_a = a->nz * iz_a;
    nziz_b = b->nz * iz_b;
    nziz_c = c->nz * iz_c;

    iz_start = (e0_start * iz_a + e1_start * iz_b + e2_start * iz_c) * inv_area;
    iz_dx = (e0_dx * iz_a + e1_dx * iz_b + e2_dx * iz_c) * inv_area;
    iz_dy = (e0_dy * iz_a + e1_dy * iz_b + e2_dy * iz_c) * inv_area;
    uiz_start = (e0_start * uiz_a + e1_start * uiz_b + e2_start * uiz_c) * inv_area;
    uiz_dx = (e0_dx * uiz_a + e1_dx * uiz_b + e2_dx * uiz_c) * inv_area;
    uiz_dy = (e0_dy * uiz_a + e1_dy * uiz_b + e2_dy * uiz_c) * inv_area;
    viz_start = (e0_start * viz_a + e1_start * viz_b + e2_start * viz_c) * inv_area;
    viz_dx = (e0_dx * viz_a + e1_dx * viz_b + e2_dx * viz_c) * inv_area;
    viz_dy = (e0_dy * viz_a + e1_dy * viz_b + e2_dy * viz_c) * inv_area;
    wxiz_start = (e0_start * wxiz_a + e1_start * wxiz_b + e2_start * wxiz_c) * inv_area;
    wxiz_dx = (e0_dx * wxiz_a + e1_dx * wxiz_b + e2_dx * wxiz_c) * inv_area;
    wxiz_dy = (e0_dy * wxiz_a + e1_dy * wxiz_b + e2_dy * wxiz_c) * inv_area;
    wyiz_start = (e0_start * wyiz_a + e1_start * wyiz_b + e2_start * wyiz_c) * inv_area;
    wyiz_dx = (e0_dx * wyiz_a + e1_dx * wyiz_b + e2_dx * wyiz_c) * inv_area;
    wyiz_dy = (e0_dy * wyiz_a + e1_dy * wyiz_b + e2_dy * wyiz_c) * inv_area;
    wziz_start = (e0_start * wziz_a + e1_start * wziz_b + e2_start * wziz_c) * inv_area;
    wziz_dx = (e0_dx * wziz_a + e1_dx * wziz_b + e2_dx * wziz_c) * inv_area;
    wziz_dy = (e0_dy * wziz_a + e1_dy * wziz_b + e2_dy * wziz_c) * inv_area;
    nxiz_start = (e0_start * nxiz_a + e1_start * nxiz_b + e2_start * nxiz_c) * inv_area;
    nxiz_dx = (e0_dx * nxiz_a + e1_dx * nxiz_b + e2_dx * nxiz_c) * inv_area;
    nxiz_dy = (e0_dy * nxiz_a + e1_dy * nxiz_b + e2_dy * nxiz_c) * inv_area;
    nyiz_start = (e0_start * nyiz_a + e1_start * nyiz_b + e2_start * nyiz_c) * inv_area;
    nyiz_dx = (e0_dx * nyiz_a + e1_dx * nyiz_b + e2_dx * nyiz_c) * inv_area;
    nyiz_dy = (e0_dy * nyiz_a + e1_dy * nyiz_b + e2_dy * nyiz_c) * inv_area;
    nziz_start = (e0_start * nziz_a + e1_start * nziz_b + e2_start * nziz_c) * inv_area;
    nziz_dx = (e0_dx * nziz_a + e1_dx * nziz_b + e2_dx * nziz_c) * inv_area;
    nziz_dy = (e0_dy * nziz_a + e1_dy * nziz_b + e2_dy * nziz_c) * inv_area;

    for (int32_t tile_y = min_y; tile_y <= max_y; tile_y += K3D_LIGHT_SAMPLE_STEP)
    {
        int32_t tile_y_end = tile_y + K3D_LIGHT_SAMPLE_STEP;
        if (tile_y_end > max_y + 1)
            tile_y_end = max_y + 1;

        for (int32_t tile_x = min_x; tile_x <= max_x; tile_x += K3D_LIGHT_SAMPLE_STEP)
        {
            int32_t tile_x_end = tile_x + K3D_LIGHT_SAMPLE_STEP;
            int32_t tile_w = 0;
            int32_t tile_h = tile_y_end - tile_y;
            int32_t sx0 = 0, sx1 = 0, sy0 = 0, sy1 = 0;
            float l00_r = ctx->ambient_r, l00_g = ctx->ambient_g, l00_b = ctx->ambient_b;
            float l10_r = ctx->ambient_r, l10_g = ctx->ambient_g, l10_b = ctx->ambient_b;
            float l01_r = ctx->ambient_r, l01_g = ctx->ambient_g, l01_b = ctx->ambient_b;
            float l11_r = ctx->ambient_r, l11_g = ctx->ambient_g, l11_b = ctx->ambient_b;
            const k3d_light_tile *light_tile = k3d_ctx_light_tile(ctx, tile_x, tile_y);

            if (tile_x_end > max_x + 1)
                tile_x_end = max_x + 1;
            tile_w = tile_x_end - tile_x;
            if (tile_w <= 0 || tile_h <= 0)
                continue;

            sx0 = tile_x - min_x;
            sx1 = tile_x_end - min_x;
            sy0 = tile_y - min_y;
            sy1 = tile_y_end - min_y;

            k3d_light_from_interp(s, ctx, light_tile,
                                  iz_start + iz_dx * (float)sx0 + iz_dy * (float)sy0,
                                  wxiz_start + wxiz_dx * (float)sx0 + wxiz_dy * (float)sy0,
                                  wyiz_start + wyiz_dx * (float)sx0 + wyiz_dy * (float)sy0,
                                  wziz_start + wziz_dx * (float)sx0 + wziz_dy * (float)sy0,
                                  nxiz_start + nxiz_dx * (float)sx0 + nxiz_dy * (float)sy0,
                                  nyiz_start + nyiz_dx * (float)sx0 + nyiz_dy * (float)sy0,
                                  nziz_start + nziz_dx * (float)sx0 + nziz_dy * (float)sy0,
                                  &l00_r, &l00_g, &l00_b);
            k3d_light_from_interp(s, ctx, light_tile,
                                  iz_start + iz_dx * (float)sx1 + iz_dy * (float)sy0,
                                  wxiz_start + wxiz_dx * (float)sx1 + wxiz_dy * (float)sy0,
                                  wyiz_start + wyiz_dx * (float)sx1 + wyiz_dy * (float)sy0,
                                  wziz_start + wziz_dx * (float)sx1 + wziz_dy * (float)sy0,
                                  nxiz_start + nxiz_dx * (float)sx1 + nxiz_dy * (float)sy0,
                                  nyiz_start + nyiz_dx * (float)sx1 + nyiz_dy * (float)sy0,
                                  nziz_start + nziz_dx * (float)sx1 + nziz_dy * (float)sy0,
                                  &l10_r, &l10_g, &l10_b);
            k3d_light_from_interp(s, ctx, light_tile,
                                  iz_start + iz_dx * (float)sx0 + iz_dy * (float)sy1,
                                  wxiz_start + wxiz_dx * (float)sx0 + wxiz_dy * (float)sy1,
                                  wyiz_start + wyiz_dx * (float)sx0 + wyiz_dy * (float)sy1,
                                  wziz_start + wziz_dx * (float)sx0 + wziz_dy * (float)sy1,
                                  nxiz_start + nxiz_dx * (float)sx0 + nxiz_dy * (float)sy1,
                                  nyiz_start + nyiz_dx * (float)sx0 + nyiz_dy * (float)sy1,
                                  nziz_start + nziz_dx * (float)sx0 + nziz_dy * (float)sy1,
                                  &l01_r, &l01_g, &l01_b);
            k3d_light_from_interp(s, ctx, light_tile,
                                  iz_start + iz_dx * (float)sx1 + iz_dy * (float)sy1,
                                  wxiz_start + wxiz_dx * (float)sx1 + wxiz_dy * (float)sy1,
                                  wyiz_start + wyiz_dx * (float)sx1 + wyiz_dy * (float)sy1,
                                  wziz_start + wziz_dx * (float)sx1 + wziz_dy * (float)sy1,
                                  nxiz_start + nxiz_dx * (float)sx1 + nxiz_dy * (float)sy1,
                                  nyiz_start + nyiz_dx * (float)sx1 + nyiz_dy * (float)sy1,
                                  nziz_start + nziz_dx * (float)sx1 + nziz_dy * (float)sy1,
                                  &l11_r, &l11_g, &l11_b);

            for (int32_t y = tile_y; y < tile_y_end; ++y)
            {
                int32_t row_dx = tile_x - min_x;
                int32_t row_dy = y - min_y;
                float fy = (float)(y - tile_y) / (float)tile_h;
                float left_r = l00_r + (l01_r - l00_r) * fy;
                float left_g = l00_g + (l01_g - l00_g) * fy;
                float left_b = l00_b + (l01_b - l00_b) * fy;
                float right_r = l10_r + (l11_r - l10_r) * fy;
                float right_g = l10_g + (l11_g - l10_g) * fy;
                float right_b = l10_b + (l11_b - l10_b) * fy;
                float light_r = left_r;
                float light_g = left_g;
                float light_b = left_b;
                float light_dr = (right_r - left_r) / (float)tile_w;
                float light_dg = (right_g - left_g) / (float)tile_w;
                float light_db = (right_b - left_b) / (float)tile_w;
                float e0 = e0_start + e0_dx * (float)row_dx + e0_dy * (float)row_dy;
                float e1 = e1_start + e1_dx * (float)row_dx + e1_dy * (float)row_dy;
                float e2 = e2_start + e2_dx * (float)row_dx + e2_dy * (float)row_dy;
                float iz = iz_start + iz_dx * (float)row_dx + iz_dy * (float)row_dy;
                float uiz = uiz_start + uiz_dx * (float)row_dx + uiz_dy * (float)row_dy;
                float viz = viz_start + viz_dx * (float)row_dx + viz_dy * (float)row_dy;
                float wxiz = wxiz_start + wxiz_dx * (float)row_dx + wxiz_dy * (float)row_dy;
                float wyiz = wyiz_start + wyiz_dx * (float)row_dx + wyiz_dy * (float)row_dy;
                float wziz = wziz_start + wziz_dx * (float)row_dx + wziz_dy * (float)row_dy;
                float nxiz = nxiz_start + nxiz_dx * (float)row_dx + nxiz_dy * (float)row_dy;
                float nyiz = nyiz_start + nyiz_dx * (float)row_dx + nyiz_dy * (float)row_dy;
                float nziz = nziz_start + nziz_dx * (float)row_dx + nziz_dy * (float)row_dy;
                uint32_t idx = (uint32_t)y * s->w + (uint32_t)tile_x;

                for (int32_t x = tile_x; x < tile_x_end; ++x)
                {
                    if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f)
                    {
                        if (iz > 0.0f && iz > s->depth[idx])
                        {
                            float z = 1.0f / iz;
                            float u = uiz * z;
                            float v = viz * z;
                            uint32_t tex = k3d_sample_tri_shade(shade, u, v);
                            uint8_t alpha = shade->opaque ? 255u : (uint8_t)(tex >> 24);
                            if (alpha)
                            {
                                float pixel_light_r = light_r;
                                float pixel_light_g = light_g;
                                float pixel_light_b = light_b;

                                if (use_pixel_fast_shadows)
                                {
                                    float wx = wxiz * z;
                                    float wy = wyiz * z;
                                    float wz = wziz * z;
                                    if (k3d_fast_shadow_blocks_any_light(ctx, wx, wy, wz))
                                    {
                                        k3d_light_at(s, ctx, light_tile, 1u,
                                                     wx, wy, wz,
                                                     nxiz * z, nyiz * z, nziz * z, z,
                                                     &pixel_light_r, &pixel_light_g, &pixel_light_b);
                                    }
                                }

                                {
                                    uint32_t shaded = k3d_shade_texel_fast(shade, tex, z,
                                                                           pixel_light_r,
                                                                           pixel_light_g,
                                                                           pixel_light_b);
                                    s->depth[idx] = iz;
                                    s->color[idx] = shade->opaque ? shaded : k3d_blend_argb(s->color[idx], shaded, alpha);
                                }
                            }
                        }
                    }

                    e0 += e0_dx;
                    e1 += e1_dx;
                    e2 += e2_dx;
                    iz += iz_dx;
                    uiz += uiz_dx;
                    viz += viz_dx;
                    if (use_pixel_fast_shadows)
                    {
                        wxiz += wxiz_dx;
                        wyiz += wyiz_dx;
                        wziz += wziz_dx;
                        nxiz += nxiz_dx;
                        nyiz += nyiz_dx;
                        nziz += nziz_dx;
                    }
                    light_r += light_dr;
                    light_g += light_dg;
                    light_b += light_db;
                    ++idx;
                }
            }
        }
    }
}

static int k3d_project(const k3d_render_ctx *ctx, const k3d_pipe_vert *cam, const k3d_pipe_vert *world, k3d_screen_vert *out)
{
    if (cam->z <= ctx->near_z)
        return 0;
    out->inv_z = 1.0f / cam->z;
    out->cam_z = cam->z;
    out->sx = ((cam->x * out->inv_z) * ctx->proj_x + 0.5f) * ctx->screen_w;
    out->sy = (0.5f - (cam->y * out->inv_z) * ctx->proj_y) * ctx->screen_h;
    out->wx = world->x;
    out->wy = world->y;
    out->wz = world->z;
    out->nx = world->nx;
    out->ny = world->ny;
    out->nz = world->nz;
    out->lr = world->lr;
    out->lg = world->lg;
    out->lb = world->lb;
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

static void k3d_draw_clipped_tri_pretransformed(k3d_scene *s, const k3d_render_ctx *ctx, uint16_t mat_idx,
                                                const k3d_pipe_vert *wa,
                                                const k3d_pipe_vert *wb,
                                                const k3d_pipe_vert *wc,
                                                const k3d_pipe_vert *ca,
                                                const k3d_pipe_vert *cb,
                                                const k3d_pipe_vert *cc)
{
    k3d_pipe_vert world_a[16];
    k3d_pipe_vert cam_a[16];
    k3d_pipe_vert world_b[16];
    k3d_pipe_vert cam_b[16];
    uint32_t count = 3u;
    float near_z = ctx->near_z;
    float tan_y = ctx->tan_y;
    float tan_x = ctx->tan_x;
    k3d_tri_shade shade;

    k3d_tri_shade_init(s, ctx, mat_idx, &shade);
    world_a[0] = *wa;
    world_a[1] = *wb;
    world_a[2] = *wc;
    cam_a[0] = *ca;
    cam_a[1] = *cb;
    cam_a[2] = *cc;

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
        if (!k3d_project(ctx, &cam_a[0], &world_a[0], &sv0) ||
            !k3d_project(ctx, &cam_a[i], &world_a[i], &sv1) ||
            !k3d_project(ctx, &cam_a[i + 1u], &world_a[i + 1u], &sv2))
            continue;
        k3d_raster_tri(s, ctx, &shade, &sv0, &sv1, &sv2);
    }
}

static void k3d_draw_clipped_tri(k3d_scene *s, const k3d_render_ctx *ctx, uint16_t mat_idx,
                                 const k3d_pipe_vert *wa,
                                 const k3d_pipe_vert *wb,
                                 const k3d_pipe_vert *wc)
{
    k3d_pipe_vert ca, cb, cc;

    (void)k3d_camera_transform(s, ctx, wa, &ca);
    (void)k3d_camera_transform(s, ctx, wb, &cb);
    (void)k3d_camera_transform(s, ctx, wc, &cc);
    k3d_draw_clipped_tri_pretransformed(s, ctx, mat_idx, wa, wb, wc, &ca, &cb, &cc);
}

static void k3d_raster_shadow_tri(k3d_scene *s,
                                  const k3d_screen_vert *a,
                                  const k3d_screen_vert *b,
                                  const k3d_screen_vert *c,
                                  uint8_t shade_pct,
                                  float depth_tolerance)
{
    float area = k3d_edge(a->sx, a->sy, b->sx, b->sy, c->sx, c->sy);
    int32_t min_x, max_x, min_y, max_y;
    float min_sx, max_sx, min_sy, max_sy;
    float inv_area;
    float e0_dx, e1_dx, e2_dx;
    float e0_dy, e1_dy, e2_dy;
    float start_x, start_y;
    float e0_start, e1_start, e2_start;
    float iz_start, iz_dx, iz_dy;
    uint8_t flip_edges;

    if (k3d_absf(area) < 0.0001f)
        return;
    flip_edges = area < 0.0f ? 1u : 0u;
    if (flip_edges)
        area = -area;
    min_sx = k3d_min3f(a->sx, b->sx, c->sx) - 1.0f;
    max_sx = k3d_max3f(a->sx, b->sx, c->sx) + 1.0f;
    min_sy = k3d_min3f(a->sy, b->sy, c->sy) - 1.0f;
    max_sy = k3d_max3f(a->sy, b->sy, c->sy) + 1.0f;
    if (max_sx < 0.0f || max_sy < 0.0f || min_sx > (float)(s->w - 1u) || min_sy > (float)(s->h - 1u))
        return;

    inv_area = 1.0f / area;
    min_x = (int32_t)k3d_clampf(min_sx, 0.0f, (float)(s->w - 1u));
    max_x = (int32_t)k3d_clampf(max_sx, 0.0f, (float)(s->w - 1u));
    min_y = (int32_t)k3d_clampf(min_sy, 0.0f, (float)(s->h - 1u));
    max_y = (int32_t)k3d_clampf(max_sy, 0.0f, (float)(s->h - 1u));

    e0_dx = c->sy - b->sy;
    e0_dy = b->sx - c->sx;
    e1_dx = a->sy - c->sy;
    e1_dy = c->sx - a->sx;
    e2_dx = b->sy - a->sy;
    e2_dy = a->sx - b->sx;

    start_x = (float)min_x + 0.5f;
    start_y = (float)min_y + 0.5f;
    e0_start = k3d_edge(b->sx, b->sy, c->sx, c->sy, start_x, start_y);
    e1_start = k3d_edge(c->sx, c->sy, a->sx, a->sy, start_x, start_y);
    e2_start = k3d_edge(a->sx, a->sy, b->sx, b->sy, start_x, start_y);
    if (flip_edges)
    {
        e0_dx = -e0_dx;
        e1_dx = -e1_dx;
        e2_dx = -e2_dx;
        e0_dy = -e0_dy;
        e1_dy = -e1_dy;
        e2_dy = -e2_dy;
        e0_start = -e0_start;
        e1_start = -e1_start;
        e2_start = -e2_start;
    }

    iz_start = (e0_start * a->inv_z + e1_start * b->inv_z + e2_start * c->inv_z) * inv_area;
    iz_dx = (e0_dx * a->inv_z + e1_dx * b->inv_z + e2_dx * c->inv_z) * inv_area;
    iz_dy = (e0_dy * a->inv_z + e1_dy * b->inv_z + e2_dy * c->inv_z) * inv_area;

    for (int32_t y = min_y; y <= max_y; ++y)
    {
        float e0 = e0_start;
        float e1 = e1_start;
        float e2 = e2_start;
        float iz = iz_start;
        uint32_t idx = (uint32_t)y * s->w + (uint32_t)min_x;
        for (int32_t x = min_x; x <= max_x; ++x)
        {
            if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f)
            {
                if (iz > 0.0f)
                {
                    float z = 1.0f / iz;
                    if (s->depth[idx] > 0.0f && k3d_absf(z - (1.0f / s->depth[idx])) <= depth_tolerance)
                    {
                        uint32_t p = s->color[idx];
                        uint8_t r = (uint8_t)((((p >> 16) & 255u) * (uint32_t)shade_pct) / 100u);
                        uint8_t g = (uint8_t)((((p >> 8) & 255u) * (uint32_t)shade_pct) / 100u);
                        uint8_t bch = (uint8_t)(((p & 255u) * (uint32_t)shade_pct) / 100u);
                        s->color[idx] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | bch;
                    }
                }
            }
            e0 += e0_dx;
            e1 += e1_dx;
            e2 += e2_dx;
            iz += iz_dx;
            ++idx;
        }
        e0_start += e0_dy;
        e1_start += e1_dy;
        e2_start += e2_dy;
        iz_start += iz_dy;
    }
}

static void k3d_draw_shadow_clipped_tri(k3d_scene *s, const k3d_render_ctx *ctx,
                                        const k3d_pipe_vert *wa,
                                        const k3d_pipe_vert *wb,
                                        const k3d_pipe_vert *wc,
                                        uint8_t shade_pct,
                                        float depth_tolerance)
{
    k3d_pipe_vert world_a[16];
    k3d_pipe_vert cam_a[16];
    k3d_pipe_vert world_b[16];
    k3d_pipe_vert cam_b[16];
    uint32_t count = 3u;
    float near_z = ctx->near_z;
    float tan_y = ctx->tan_y;
    float tan_x = ctx->tan_x;

    world_a[0] = *wa;
    world_a[1] = *wb;
    world_a[2] = *wc;
    for (uint32_t i = 0; i < count; ++i)
        (void)k3d_camera_transform(s, ctx, &world_a[i], &cam_a[i]);

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
        if (!k3d_project(ctx, &cam_a[0], &world_a[0], &sv0) ||
            !k3d_project(ctx, &cam_a[i], &world_a[i], &sv1) ||
            !k3d_project(ctx, &cam_a[i + 1u], &world_a[i + 1u], &sv2))
            continue;
        k3d_raster_shadow_tri(s, &sv0, &sv1, &sv2, shade_pct, depth_tolerance);
    }
}

static int k3d_project_shadow_vertex(k3d_vec3 light_dir, const k3d_pipe_vert *src, k3d_pipe_vert *out)
{
    if (k3d_absf(light_dir.y) < 0.04f || src->y <= 0.03f)
        return 0;
    float t = (K3D_SHADOW_GROUND_Y - src->y) / light_dir.y;
    if (t <= 0.0f)
        return 0;
    *out = *src;
    out->x = src->x + light_dir.x * t;
    out->y = K3D_SHADOW_GROUND_Y;
    out->z = src->z + light_dir.z * t;
    out->nx = 0.0f;
    out->ny = 1.0f;
    out->nz = 0.0f;
    return 1;
}

static int k3d_project_point_shadow_vertex(const k3d_point_light *light, const k3d_pipe_vert *src, k3d_pipe_vert *out)
{
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
    float t = 0.0f;
    float max_dist = 0.0f;
    float cast_dx = 0.0f;
    float cast_dz = 0.0f;
    float cast_len2 = 0.0f;
    float max_len2 = 0.0f;

    if (!light || !light->enabled || light->intensity <= 0.0f || light->pos.y <= K3D_SHADOW_GROUND_Y + 0.04f)
        return 0;
    if (src->y <= K3D_SHADOW_GROUND_Y + 0.01f || src->y >= light->pos.y - 0.01f)
        return 0;

    dx = src->x - light->pos.x;
    dy = src->y - light->pos.y;
    dz = src->z - light->pos.z;
    if (dy >= -0.0001f)
        return 0;

    t = (K3D_SHADOW_GROUND_Y - src->y) / dy;
    if (t <= 0.0f)
        return 0;

    *out = *src;
    out->x = src->x + dx * t;
    out->y = K3D_SHADOW_GROUND_Y;
    out->z = src->z + dz * t;

    cast_dx = out->x - src->x;
    cast_dz = out->z - src->z;
    max_dist = light->radius * K3D_POINT_SHADOW_MAX_CAST_RADIUS_MUL + K3D_POINT_SHADOW_MAX_CAST_EXTRA;
    cast_len2 = cast_dx * cast_dx + cast_dz * cast_dz;
    max_len2 = max_dist * max_dist;
    if (max_dist > 0.0f && cast_len2 > max_len2 && cast_len2 > 0.0001f)
    {
        float scale = max_dist / k3d_sqrtf(cast_len2);
        out->x = src->x + cast_dx * scale;
        out->z = src->z + cast_dz * scale;
    }

    out->nx = 0.0f;
    out->ny = 1.0f;
    out->nz = 0.0f;
    return 1;
}

static float k3d_shadow_orient_xz(const k3d_shadow_point *a, const k3d_shadow_point *b, const k3d_shadow_point *c)
{
    float abx = b->v.x - a->v.x;
    float abz = b->v.z - a->v.z;
    float acx = c->v.x - a->v.x;
    float acz = c->v.z - a->v.z;
    return abx * acz - abz * acx;
}

static float k3d_shadow_dist2_xz(const k3d_shadow_point *a, const k3d_shadow_point *b)
{
    float dx = b->v.x - a->v.x;
    float dz = b->v.z - a->v.z;
    return dx * dx + dz * dz;
}

static void k3d_shadow_add_point(k3d_shadow_point *points, uint32_t *count, const k3d_pipe_vert *p)
{
    if (!points || !count || !p)
        return;
    for (uint32_t i = 0; i < *count; ++i)
    {
        float dx = points[i].v.x - p->x;
        float dz = points[i].v.z - p->z;
        if (dx * dx + dz * dz < 0.0004f)
            return;
    }
    if (*count >= K3D_SHADOW_HULL_MAX_POINTS)
        return;
    points[*count].v = *p;
    ++(*count);
}

static uint32_t k3d_shadow_build_hull(const k3d_shadow_point *points, uint32_t count,
                                      uint32_t *hull, uint32_t hull_cap)
{
    uint32_t start = 0;
    uint32_t p = 0;
    uint32_t out_count = 0;

    if (!points || !hull || count < 3u || hull_cap < 3u)
        return 0;

    for (uint32_t i = 1u; i < count; ++i)
    {
        if (points[i].v.x < points[start].v.x ||
            (k3d_absf(points[i].v.x - points[start].v.x) < 0.0001f && points[i].v.z < points[start].v.z))
        {
            start = i;
        }
    }

    p = start;
    do
    {
        uint32_t q = (p == 0u) ? 1u : 0u;
        if (out_count >= hull_cap)
            break;
        hull[out_count++] = p;

        for (uint32_t r = 0; r < count; ++r)
        {
            float orient;
            if (r == p || r == q)
                continue;
            orient = k3d_shadow_orient_xz(&points[p], &points[q], &points[r]);
            if (orient < -0.0001f ||
                (k3d_absf(orient) <= 0.0001f &&
                 k3d_shadow_dist2_xz(&points[p], &points[r]) > k3d_shadow_dist2_xz(&points[p], &points[q])))
            {
                q = r;
            }
        }

        p = q;
    } while (p != start && out_count < hull_cap);

    return out_count;
}

static void k3d_draw_shadow_hull(k3d_scene *s, const k3d_render_ctx *ctx,
                                 const k3d_shadow_point *points, uint32_t count,
                                 uint8_t shade_pct,
                                 float depth_tolerance)
{
    uint32_t hull[K3D_SHADOW_HULL_MAX_POINTS];
    uint32_t hull_count = k3d_shadow_build_hull(points, count, hull, K3D_SHADOW_HULL_MAX_POINTS);

    if (hull_count < 3u)
        return;

    for (uint32_t i = 1u; i + 1u < hull_count; ++i)
    {
        k3d_draw_shadow_clipped_tri(s, ctx,
                                    &points[hull[0]].v,
                                    &points[hull[i]].v,
                                    &points[hull[i + 1u]].v,
                                    shade_pct,
                                    depth_tolerance);
    }
}

static void k3d_fast_shadow_reset(k3d_render_ctx *ctx)
{
    if (!ctx)
        return;
    ctx->fast_shadow_hulls = G_fast_shadow_hulls;
    ctx->fast_shadow_hull_count = 0u;
}

static void k3d_fast_shadow_add_hull(k3d_render_ctx *ctx, uint8_t light_idx,
                                     const k3d_shadow_point *points, uint32_t count,
                                     uint8_t shade_pct,
                                     float depth_tolerance)
{
    uint32_t hull_idx[K3D_SHADOW_HULL_MAX_POINTS];
    uint32_t hull_count;
    k3d_fast_shadow_hull *h;

    if (!ctx || !points || count < 3u || ctx->fast_shadow_hull_count >= K3D_MAX_FAST_SHADOW_HULLS)
        return;
    hull_count = k3d_shadow_build_hull(points, count, hull_idx, K3D_SHADOW_HULL_MAX_POINTS);
    if (hull_count < 3u || hull_count > K3D_SHADOW_HULL_MAX_POINTS)
        return;

    h = &G_fast_shadow_hulls[ctx->fast_shadow_hull_count++];
    memset(h, 0, sizeof(*h));
    h->light_idx = light_idx;
    h->shade_pct = shade_pct;
    h->depth_tolerance = depth_tolerance;
    h->count = (uint8_t)hull_count;
    h->min_x = h->max_x = points[hull_idx[0]].v.x;
    h->min_z = h->max_z = points[hull_idx[0]].v.z;

    for (uint32_t i = 0u; i < hull_count; ++i)
    {
        const k3d_pipe_vert *p = &points[hull_idx[i]].v;
        h->points[i].x = p->x;
        h->points[i].z = p->z;
        if (p->x < h->min_x)
            h->min_x = p->x;
        if (p->x > h->max_x)
            h->max_x = p->x;
        if (p->z < h->min_z)
            h->min_z = p->z;
        if (p->z > h->max_z)
            h->max_z = p->z;
    }
}

static k3d_pipe_vert k3d_fast_shadow_pipe(float x, float z)
{
    k3d_pipe_vert v;
    v.x = x;
    v.y = K3D_SHADOW_GROUND_Y;
    v.z = z;
    v.nx = 0.0f;
    v.ny = 1.0f;
    v.nz = 0.0f;
    v.lr = 1.0f;
    v.lg = 1.0f;
    v.lb = 1.0f;
    v.u = 0.0f;
    v.v = 0.0f;
    return v;
}

static void k3d_draw_cached_shadow_hull(k3d_scene *s, const k3d_render_ctx *ctx,
                                        const k3d_fast_shadow_hull *h)
{
    if (!s || !ctx || !h || h->count < 3u)
        return;

    for (uint32_t i = 1u; i + 1u < h->count; ++i)
    {
        k3d_pipe_vert a = k3d_fast_shadow_pipe(h->points[0].x, h->points[0].z);
        k3d_pipe_vert b = k3d_fast_shadow_pipe(h->points[i].x, h->points[i].z);
        k3d_pipe_vert c = k3d_fast_shadow_pipe(h->points[i + 1u].x, h->points[i + 1u].z);
        k3d_draw_shadow_clipped_tri(s, ctx, &a, &b, &c, h->shade_pct, h->depth_tolerance);
    }
}

static void k3d_build_fast_shadow_hulls(const k3d_scene *s, k3d_render_ctx *ctx)
{
    k3d_vec3 light_dir;
    uint8_t cast_directional;

    if (!s || !ctx)
        return;
    k3d_fast_shadow_reset(ctx);
    if (!ctx->projected_shadows_enabled)
        return;
    light_dir = ctx->dir_light_dir;
    cast_directional = (s->dir_light_intensity > 0.0f && k3d_absf(light_dir.y) >= 0.04f) ? 1u : 0u;
    if (s->ambient_intensity <= 0.001f)
        cast_directional = 0u;
    if (!cast_directional && ctx->point_light_scan_count == 0u)
        return;

    for (uint32_t ii = 0; ii < K3D_MAX_INSTANCES; ++ii)
    {
        const k3d_instance *in = &s->instances[ii];
        if (!in->used || !in->visible || !in->casts_shadow || in->mesh_idx >= K3D_MAX_MESHES)
            continue;
        const k3d_mesh *m = &s->meshes[in->mesh_idx];
        if (!m->used || !m->verts || !m->tris)
            continue;
        k3d_instance_xform xf;
        k3d_shadow_point points[K3D_SHADOW_HULL_MAX_POINTS];
        uint32_t point_count = 0;
        uint8_t use_vert_cache = m->vert_count <= K3D_MAX_FRAME_VERTS ? 1u : 0u;

        k3d_instance_xform_init(in, &xf);
        if (use_vert_cache)
        {
            for (uint32_t vi = 0u; vi < m->vert_count; ++vi)
                k3d_transform_vertex(&xf, &m->verts[vi], &G_frame_world_verts[vi]);
        }

        if (cast_directional)
        {
            for (uint32_t vi = 0; vi < m->vert_count; ++vi)
            {
                k3d_pipe_vert v;
                k3d_pipe_vert sv;
                const k3d_pipe_vert *src = &v;

                if (use_vert_cache)
                    src = &G_frame_world_verts[vi];
                else
                    k3d_transform_vertex(&xf, &m->verts[vi], &v);
                if (k3d_project_shadow_vertex(light_dir, src, &sv))
                    k3d_shadow_add_point(points, &point_count, &sv);
            }

            k3d_fast_shadow_add_hull(ctx, K3D_FAST_SHADOW_DIRECTIONAL,
                                     points, point_count, 52u, K3D_SHADOW_DEPTH_TOLERANCE);
        }

        for (uint32_t li = 0; li < ctx->point_light_scan_count; ++li)
        {
            const k3d_point_light *pl = &s->point_lights[li];
            uint8_t shade_pct = 0u;

            if (!pl->enabled || pl->intensity <= 0.0f)
                continue;

            point_count = 0u;
            for (uint32_t vi = 0; vi < m->vert_count; ++vi)
            {
                k3d_pipe_vert v;
                k3d_pipe_vert sv;
                const k3d_pipe_vert *src = &v;

                if (use_vert_cache)
                    src = &G_frame_world_verts[vi];
                else
                    k3d_transform_vertex(&xf, &m->verts[vi], &v);
                if (k3d_project_point_shadow_vertex(pl, src, &sv))
                    k3d_shadow_add_point(points, &point_count, &sv);
            }

            if (point_count < 3u)
                continue;

            shade_pct = (uint8_t)(84u - (uint32_t)k3d_clampf(pl->intensity * 9.0f, 0.0f, 28.0f));
            if (shade_pct < 52u)
                shade_pct = 52u;
            k3d_fast_shadow_add_hull(ctx, (uint8_t)li,
                                     points, point_count, shade_pct, K3D_SHADOW_DEPTH_TOLERANCE);
        }
    }
}

static void k3d_render_shadows(k3d_scene *s, const k3d_render_ctx *ctx)
{
    if (!s || !ctx || !ctx->fast_shadow_hulls || ctx->fast_shadow_hull_count == 0u)
        return;

    for (uint32_t i = 0u; i < ctx->fast_shadow_hull_count; ++i)
        k3d_draw_cached_shadow_hull(s, ctx, &ctx->fast_shadow_hulls[i]);
}

int k3d_scene_render(k3d_scene_handle scene)
{
    k3d_scene *s = k3d_scene_ref(scene);
    k3d_render_ctx ctx;
    if (!s || !s->color || !s->depth)
        return -1;
    k3d_render_ctx_init(s, &ctx);
    k3d_build_light_tiles(s, &ctx);
    k3d_build_fast_shadow_hulls(s, &ctx);
    if (ctx.point_light_shadows_enabled || ctx.directional_ray_shadows_enabled)
    {
        k3d_build_shadow_cache(s, &ctx);
        k3d_build_shadow_grid(&ctx);
    }
    uint32_t bg = 0xFF000000u | ((uint32_t)s->fog.color.r << 16) | ((uint32_t)s->fog.color.g << 8) | s->fog.color.b;
    for (uint32_t i = 0; i < s->w * s->h; ++i)
    {
        s->color[i] = bg;
        s->depth[i] = 0.0f;
    }

    for (uint32_t ii = 0; ii < K3D_MAX_INSTANCES; ++ii)
    {
        k3d_instance *in = &s->instances[ii];
        if (!in->used || !in->visible || in->mesh_idx >= K3D_MAX_MESHES)
            continue;
        k3d_mesh *m = &s->meshes[in->mesh_idx];
        if (!m->used || !m->verts || !m->tris)
            continue;
        k3d_instance_xform xf;
        uint8_t use_vert_cache = m->vert_count <= K3D_MAX_FRAME_VERTS ? 1u : 0u;

        k3d_instance_xform_init(in, &xf);
        if (use_vert_cache)
        {
            for (uint32_t vi = 0u; vi < m->vert_count; ++vi)
            {
                k3d_transform_vertex(&xf, &m->verts[vi], &G_frame_world_verts[vi]);
                (void)k3d_camera_transform(s, &ctx, &G_frame_world_verts[vi], &G_frame_cam_verts[vi]);
            }
        }
        for (uint32_t ti = 0; ti < m->tri_count; ++ti)
        {
            const k3d_tri *tr = &m->tris[ti];
            if (tr->a >= m->vert_count || tr->b >= m->vert_count || tr->c >= m->vert_count)
                continue;
            if (use_vert_cache)
            {
                k3d_draw_clipped_tri_pretransformed(s, &ctx, tr->material,
                                                    &G_frame_world_verts[tr->a],
                                                    &G_frame_world_verts[tr->b],
                                                    &G_frame_world_verts[tr->c],
                                                    &G_frame_cam_verts[tr->a],
                                                    &G_frame_cam_verts[tr->b],
                                                    &G_frame_cam_verts[tr->c]);
            }
            else
            {
                k3d_pipe_vert a, b, c;
                k3d_transform_vertex(&xf, &m->verts[tr->a], &a);
                k3d_transform_vertex(&xf, &m->verts[tr->b], &b);
                k3d_transform_vertex(&xf, &m->verts[tr->c], &c);
                k3d_draw_clipped_tri(s, &ctx, tr->material, &a, &b, &c);
            }
        }
    }
    /* Fast shadow hulls already mask lighting per pixel; do not draw a second dark overlay. */
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
                s->materials[current].texture_opaque = k3d_texture_is_opaque(&img);
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

int k3d_instance_set_casts_shadow(k3d_scene_handle scene, k3d_instance_handle inst, uint32_t casts_shadow)
{
    k3d_instance *in = k3d_instance_ref(k3d_scene_ref(scene), inst);
    if (!in)
        return -1;
    in->casts_shadow = casts_shadow ? 1u : 0u;
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
