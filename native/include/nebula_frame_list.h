/* What a game frame hands the renderer: passes in draw order, each naming a range of items, and the blocks, views, light
   sets and constants those items read. The game builds it in memory it owns and hands it over by pointer (submit_frame); the
   renderer reads it until it releases it (`released`), keeping the last two to interpolate between.

   Plain C structs, used by the game module and the host. */
#ifndef NEBULA_FRAME_LIST_H
#define NEBULA_FRAME_LIST_H

#include <stdint.h>
#include "nebula_limits.h"

#ifdef __cplusplus
extern "C" {
#endif


/* What a camera is for: two frames' cameras pair by this. */
enum { NEBULA_CAMERA_SCENE = 0, NEBULA_CAMERA_MIRROR = 1, NEBULA_CAMERA_OTHER = 2, NEBULA_CAMERA_SHADOW = 3, NEBULA_CAMERA_ROLE_COUNT = 4 };

/* A camera: eye from world, for the blocks made under it. */
typedef struct NebulaFrameCamera {
    float view[3][4];
    uint32_t role;
} NebulaFrameCamera;

/* How draws project and which part of the target they cover. Projection `terms`: the six of a perspective or orthographic
   matrix. */
typedef struct NebulaFrameView {
    float terms[6];
    float viewport[6]; /* left, top, width, height (NEBULA_FRAME_UNITS: all of the pass's target), near, far */
    float scissor[4];  /* left, top, width, height */
    uint8_t orthographic;
    uint8_t reserved[3];
} NebulaFrameView;

/* A light of a light set, as the shaders read it (program.hlsli light_term): colour as 0-255 floats, attenuation terms,
   position and direction in eye space. */
typedef struct NebulaFrameLight {
    float color[4];
    float angle[3];
    float distance[3];
    float position[3];
    float direction[3];
} NebulaFrameLight;

/* A light set: `count` lights, list->lights[first ...), as many as the game lit. A material names a light by its number in
   the set; a number past `count` lights nothing. Bit n % 32 of list->light_follow[follow_first + n / 32] says light n's
   position moves with the camera (ceil(count / 32) words). */
typedef struct NebulaFrameLightSet {
    uint32_t first;
    uint32_t count;
    uint32_t follow_first;
    uint32_t camera_plus_one; /* camera whose eye space holds these lights; 0 when unknown */
} NebulaFrameLightSet;

/* What a world light is. */
enum {
    NEBULA_WORLD_LIGHT_KEY = 0,   /* an area's light for one kind of actor: its first light (light 0 of the sets it fills) */
    NEBULA_WORLD_LIGHT_FILL,      /* and its second (light 1) */
    NEBULA_WORLD_LIGHT_POINT,     /* the game's one point light (light 4): `reach` the distance where it halves */
    NEBULA_WORLD_LIGHT_REQUEST,   /* an actor's request for that light, chosen or not: every lamp of the scene */
    NEBULA_WORLD_LIGHT_SUN,       /* the sun's model: `position` the unit direction from the camera to it, `reach` its distance */
    NEBULA_WORLD_LIGHT_STAR_CORE, /* a glowing core: its place, `reach` its scale */
    NEBULA_WORLD_LIGHT_KIND_COUNT,
};

/* World light flags. */
enum {
    NEBULA_WORLD_LIGHT_FOLLOWS_CAMERA = 1u << 0, /* `position` is in the scene camera's eye space and moves with it */
};

/* A light of the world, for what needs where a light is rather than how a material sums it: its place in the world,
   its colour as the art gives it (0 to 1), and whose it is. `id` names it from frame to frame: its owner (the area's
   light record, the actor asking, the core) and its part there. A list holds each id once, in ascending order. */
typedef struct NebulaFrameWorldLight {
    uint64_t id;
    uint32_t kind;  /* NEBULA_WORLD_LIGHT_* */
    uint32_t flags; /* NEBULA_WORLD_LIGHT_FOLLOWS_CAMERA */
    float color[3];
    float reach;
    float position[3];
    float reserved;
} NebulaFrameWorldLight;

/* A clip area's volume this frame (Matter Splatter, the dark rooms: ClipArea::draw). The map parts the game draws as
   NEBULA_CATEGORY_CLIPPED_MAP_PARTS are drawn whole and painted over where the list's clip_rule says: what shows of
   them is their points inside some volume (NEBULA_CLIP_RULE_INSIDE) or outside every one (NEBULA_CLIP_RULE_OUTSIDE).
   `place`: the world from the volume's own space (rows of a 3x4 matrix), where it is the unit shape `shape`. */
enum {
    NEBULA_CLIP_SPHERE,    /* |p| < 1 */
    NEBULA_CLIP_BOX,       /* |x|, |y|, |z| <= 1 */
    NEBULA_CLIP_BOX_UP,    /* |x|, |z| <= 1, 0 <= y <= 2: the box standing on its origin */
    NEBULA_CLIP_CONE_UP,   /* 0 <= y <= 1, x^2 + z^2 < y^2: the tip at the origin */
    NEBULA_CLIP_CONE_DOWN, /* 0 <= y <= 1, x^2 + z^2 < (1 - y)^2: the tip at y 1 */
};
enum {
    NEBULA_CLIP_RULE_NONE,    /* no clip areas paint this frame: the clipped map parts show whole */
    NEBULA_CLIP_RULE_INSIDE,  /* they show inside the volumes */
    NEBULA_CLIP_RULE_OUTSIDE, /* they show outside every volume */
};
typedef struct NebulaFrameClipVolume {
    float place[3][4];
    uint32_t shape; /* NEBULA_CLIP_* */
    uint32_t reserved[3];
} NebulaFrameClipVolume;

/* Whose lights a light set holds, one per set (list->light_set_owners, in the sets' order). `actor_light`: the world
   lights `actor_light` (the key, the set's light 0) and `actor_light + 1` (the fill, its light 1); 0 when the set's
   lights are no light of the world (every light white at the eye). `point_light`: the id of the world light that is
   the set's light 4, or 0. `ambient`: the ambient colour loaded with the lights, 0 to 1. `kind`: which of an area's
   light records the key and fill are (NEBULA_LIGHT_OWNER_*; the game lights characters, enemies and objects from the
   player, strong and weak records and the environment from the planet record), none where unknown. */
enum {
    NEBULA_LIGHT_OWNER_NONE = 0,
    NEBULA_LIGHT_OWNER_PLAYER,
    NEBULA_LIGHT_OWNER_STRONG,
    NEBULA_LIGHT_OWNER_WEAK,
    NEBULA_LIGHT_OWNER_PLANET,
    NEBULA_LIGHT_OWNER_COIN,
};
typedef struct NebulaFrameLightOwner {
    uint64_t actor_light;
    uint64_t point_light;
    float ambient[3];
    uint32_t kind; /* NEBULA_LIGHT_OWNER_* */
} NebulaFrameLightOwner;

/* A point light an actor asked for this frame (MR::requestPointLight: a Luma, a power star, a fireball, a Boo, an
   item block ...). The game itself keeps one of them, blended toward whoever is nearest the player; a renderer that
   lights from every source takes them all. `position` in the world; `half_distance`: where its light is half what
   it is at the source (the request's brightness over the game's reference distance); `color` 0 to 1; `candela`:
   its intensity where the caller knows it (a star bit), else 0 and the half distance says it; `radius`: the glowing
   body's own radius in the world's units where the caller knows it (the actor's binder), else 0. */
typedef struct NebulaFramePointLight {
    float position[3];
    float half_distance;
    float color[3];
    float candela;
    float radius;
    float reserved[3];
} NebulaFramePointLight;

/* A drop shadow: what the game draws as a stencil volume under an actor, for a renderer that traces it instead. It
   falls from `position` along `direction` (unit length) for `length`, in the world, and is cast by the draws of the
   model `caster` (their items' `slot`; 0 when its actor draws no model). `volume`: the slot of the volume's own
   draws in this list, which such a renderer leaves out. */
typedef struct NebulaFrameShadowDrop {
    float position[3];
    float length;
    float direction[3];
    uint32_t caster;
    uint32_t volume;
    uint32_t reserved[3];
} NebulaFrameShadowDrop;

/* Block flags. */
enum {
    NEBULA_BLOCK_WORLD_ROWS = 1u << 0, /* a material reads world position or normal: keep its world rows for the vertex stage */
    NEBULA_BLOCK_EYE_SPACE = 1u << 1,  /* its matrices are already in eye space: the view's matrix is not applied */
    NEBULA_BLOCK_NO_MOTION = 1u << 2,  /* drawn at its real position on every frame */
};

/* One model's or one draw site's world matrices for this frame, under a camera. Identity is `slot` and `generation`.

   `joints` is `joint_count` matrices of 12 floats each, world from the joint's own space. `model`: the registered model whose
   draw matrix table turns them into the rows the vertex stage reads (0: each joint is one draw matrix). `scale_flags`: one bit
   per joint, set when the joint carries no scale (its normal matrix is its place). `camera`: index in the list's cameras,
   NEBULA_NONE32 when the matrices are already in eye space. */
typedef struct NebulaFrameBlock {
    const float (*joints)[4];
    const uint32_t* scale_flags;
    uint32_t model;
    uint32_t joint_count;
    uint32_t slot;
    uint32_t generation;
    uint32_t flags;
    uint32_t camera;
    uint32_t calc_mode;        /* 0 camera * joint, 1 the joint as it is, 2 camera * base * joint */
    float base[3][4];          /* calc_mode 2 */
    float normal_scale[3];     /* a bump model's normal rows, 0 none */
    uint32_t normals_from;     /* the earlier block whose normal rows it reads, or NEBULA_NONE32 */
} NebulaFrameBlock;

/* Vertices and indices built once (mesh_register): one interleaved stream. */
typedef struct NebulaFrameMesh {
    const void* vertices;
    uint32_t vertex_bytes;
    uint32_t stride;
    const uint32_t* indices;
    uint32_t index_count;
} NebulaFrameMesh;

/* A material's constants as an item reads them: a registered slot (static), or rows in this frame's list, `vertex_rows` then
   `pixel_rows` of four floats. Rows animate between frames by `identity`. */
typedef struct NebulaFrameConstants {
    uint32_t slot;               /* material_register's id, or NEBULA_NONE32 */
    uint32_t vertex_rows, pixel_rows;
    const float (*rows)[4];
    const uint32_t* coord_rows;  /* each names the first of three rows moving as a coordinate matrix */
    uint32_t coord_row_count;
} NebulaFrameConstants;

/* A texture of a table: a registered texture and how it is sampled. */
typedef struct NebulaFrameTexture {
    uint32_t texture;           /* texture_register or a target's id; 0 none */
    uint8_t wrap_s, wrap_t;     /* 0 clamp, 1 repeat, 2 mirror */
    uint8_t min_linear, mag_linear;
    uint8_t mip_filter;         /* 0 the first level only, 1 the nearest level, 2 between levels */
    uint8_t anisotropy, reserved[2];
    float min_lod, max_lod, lod_bias;
} NebulaFrameTexture;

/* Item flags. */
enum {
    NEBULA_ITEM_RING = 1u << 0,        /* vertices in the frame's ring, not a registered mesh */
    NEBULA_ITEM_INDEXED = 1u << 1,     /* ring only: 32-bit indices in the frame's index ring */
    NEBULA_ITEM_REFLECT_AO = 1u << 2,  /* writes depth and colour before the opaque end: an object for occlusion */
    NEBULA_ITEM_PLAIN_MESH = 1u << 3,  /* never the curved form of its mesh */
    NEBULA_ITEM_NO_CULL = 1u << 4,     /* not culled by its bounds */
    NEBULA_ITEM_PARTICLE = 1u << 5,    /* ring vertices are particle corners: position, position before, two colours, coordinate */
    NEBULA_ITEM_DISCRETE_VERTICES = 1u << 6, /* regenerated or sorted topology: vertex ordinals have no identity */
    NEBULA_ITEM_INSTANCES = 1u << 7,   /* `ring_count` instances of `mesh` rows each from row `ring_at` of the list's
                                          `instance_rows`, `count` vertices each: the pipeline's vertex stage reads them */
    NEBULA_ITEM_WIDE = 1u << 8,        /* ring corners of widened lines or points */
    NEBULA_ITEM_ONE_MATRIX = 1u << 9,  /* a registered mesh under one matrix, whatever its vertices name */
    NEBULA_ITEM_SPREAD_LINES = 1u << 10,
    NEBULA_ITEM_SPREAD_POINTS = 1u << 11,
    NEBULA_ITEM_CATEGORY_SHIFT = 16,   /* bits 16-23: the item's category, NEBULA_CATEGORY_* */
    NEBULA_ITEM_CATEGORY_MASK = 0xFFu << 16,
};

/* What kind of thing the game drew an item as: the draw buffer its actor registered in (the game's
   MR::DrawBufferType + 1), or NEBULA_CATEGORY_DRAW + the draw type of a draw made by code; 0 none. The game knows it
   for every draw, a mod's actors too: a renderer that needs to tell the sky from the world, crystal from stone or a
   glow from a surface reads it here. */
enum {
    NEBULA_CATEGORY_NONE = 0,
    NEBULA_CATEGORY_CLIPPED_MAP_PARTS = 1,
    NEBULA_CATEGORY_SKY = 2,
    NEBULA_CATEGORY_AIR = 3,
    NEBULA_CATEGORY_SUN = 4,
    NEBULA_CATEGORY_PLANET = 5,
    NEBULA_CATEGORY_PLANET_LOW = 6,
    NEBULA_CATEGORY_ENVIRONMENT = 7,
    NEBULA_CATEGORY_ENVIRONMENT_STRONG_LIGHT = 8,
    NEBULA_CATEGORY_MAP_OBJ = 9,
    NEBULA_CATEGORY_MAP_OBJ_WEAK_LIGHT = 10,
    NEBULA_CATEGORY_MAP_OBJ_STRONG_LIGHT = 11,
    NEBULA_CATEGORY_NO_SHADOWED_MAP_OBJ = 12,
    NEBULA_CATEGORY_NO_SHADOWED_MAP_OBJ_STRONG_LIGHT = 13,
    NEBULA_CATEGORY_NO_SILHOUETTED_MAP_OBJ = 14,
    NEBULA_CATEGORY_NO_SILHOUETTED_MAP_OBJ_WEAK_LIGHT = 15,
    NEBULA_CATEGORY_NO_SILHOUETTED_MAP_OBJ_STRONG_LIGHT = 16,
    NEBULA_CATEGORY_NPC = 17,
    NEBULA_CATEGORY_RIDE = 18,
    NEBULA_CATEGORY_ENEMY = 19,
    NEBULA_CATEGORY_ENEMY_DECORATION = 20,
    NEBULA_CATEGORY_PLAYER = 21,
    NEBULA_CATEGORY_PLAYER_DECORATION = 22,
    NEBULA_CATEGORY_CRYSTAL_BOX = 23,
    NEBULA_CATEGORY_PLAYER_PARTS = 24,
    NEBULA_CATEGORY_INDIRECT_MAP_OBJ = 25,
    NEBULA_CATEGORY_INDIRECT_MAP_OBJ_STRONG_LIGHT = 26,
    NEBULA_CATEGORY_INDIRECT_NPC = 27,
    NEBULA_CATEGORY_INDIRECT_ENEMY = 28,
    NEBULA_CATEGORY_INDIRECT_PLANET = 29,
    NEBULA_CATEGORY_BLOOM_MODEL = 30,
    NEBULA_CATEGORY_TRIPOD_BOSS = 31,
    NEBULA_CATEGORY_CRYSTAL = 32,
    NEBULA_CATEGORY_CRYSTAL_ITEM = 33,
    NEBULA_CATEGORY_GLARING_LIGHT = 34,
    NEBULA_CATEGORY_ASTRO_DOME_SKY = 35,
    NEBULA_CATEGORY_MODEL_3D_FOR_2D = 36,
    NEBULA_CATEGORY_MIRROR_MAP_OBJ = 37,
    NEBULA_CATEGORY_GAME_SHADOW = 127, /* a shadow the game draws itself: a drop shadow's volume and its fill, the
                                          player's alpha shadow, a shadow surface. A renderer that traces shadows
                                          draws none of them. */
    NEBULA_CATEGORY_DRAW = 128, /* + the game's MR::DrawType (those under 128) */
};

/* A particle a NEBULA_ITEM_INSTANCES draw places (particle_instances.hlsl): ten rows of four floats. Its corners, in the
   particle's own units, are the draw's (the particle material's quad); the vertex stage takes each through `now` and
   `before` and places it at the presented frame's time. */
typedef struct NebulaParticleInstance {
    float now[3][4];     /* the particle's matrix in the world this game frame */
    float before[3][4];  /* and the one before (the same when it did not move on from one) */
    float coord[3][4];   /* coord_mode 1: s, t from the corner's (rows 0-1); 2: s, t, q from its place in the particle */
    uint32_t prm, env;   /* R << 24 | G << 16 | B << 8 | A */
    uint32_t coord_mode; /* 0: the corner's own coordinates */
    uint32_t reserved;
} NebulaParticleInstance;

/* A glyph a NEBULA_ITEM_INSTANCES draw places (glyph_instances.hlsl, Ui::CharWriter's runs): three rows of four. Its
   quad in the draw matrix's plane z = 0, corners top left, top right, bottom right, bottom left. */
typedef struct NebulaGlyphInstance {
    float left, top, right, bottom;
    float s0, t0, s1, t1;  /* the sheet's coordinates at the top left and the bottom right */
    uint32_t colors[4];    /* the corners' colours in that order, R << 24 | G << 16 | B << 8 | A */
} NebulaGlyphInstance;

/* One draw. Everything it needs is named here. `slot`, `generation`, `part` and the view name it from frame to frame. */
typedef struct NebulaFrameItem {
    uint32_t mesh;             /* registered mesh; the bytes of a ring vertex with NEBULA_ITEM_RING; an instance's rows
                                  with NEBULA_ITEM_INSTANCES */
    uint32_t pipeline;         /* pipeline_register's id */
    uint32_t constants;        /* index in the list's constants */
    uint32_t textures;         /* texture_table's id */
    uint32_t block;            /* index in the list's blocks */
    uint32_t ring_at;          /* ring only: the byte its vertices start at */
    uint32_t ring_count;       /* ring only: how many vertices */
    uint32_t first, count;     /* ring only: first index (with NEBULA_ITEM_INDEXED) and how many indices or vertices */
    uint32_t slot;
    uint32_t source_hash;      /* material or effect name's hash; 0 none */
    uint32_t generation;
    uint32_t part;
    uint32_t matrix;           /* the draw matrix meshes without a matrix per vertex take */
    uint32_t view;
    uint32_t light_set;        /* NEBULA_NONE32: none */
    uint32_t flags;
    float line_width, point_size; /* NATIVE_WIDE programs, in pixels */
} NebulaFrameItem;

/* What a pass does, in list order. */
typedef enum NebulaPassKind {
    NEBULA_PASS_DRAW = 0,    /* the items, into `target` */
    NEBULA_PASS_CLEAR,       /* `clear_channels` of `rect` to `clear_rgba` and far, in the open target */
    NEBULA_PASS_COPY_FRAME,  /* the frame so far becomes `target`'s picture, carrying `channels`; the frame goes on */
    NEBULA_PASS_TAKE_FRAME,  /* the frame so far is `target` now; the frame goes on in a new picture, the next draw fills it */
    NEBULA_PASS_OPAQUE_END,  /* the scene's opaque surfaces are drawn: occlusion goes here */
    NEBULA_PASS_SCENE_END,   /* the 3D scene ends: the upscaled, antialiased picture is the frame from here on */
    NEBULA_PASS_BLOOM_HOLD,  /* the game's bloom, named by host (game_bloom.h), `arg0`, `arg1` its values */
    NEBULA_PASS_BLOOM_NORMAL,
    NEBULA_PASS_BLOOM_SIMPLE,
    NEBULA_PASS_PROBE,       /* depth at `probes`, in `probe_channel`; `arg0` NEBULA_PROBE_* */
    NEBULA_PASS_SHOW,        /* the frame drawn so far is the picture this frame shows */
    NEBULA_PASS_SCENE_BEGIN, /* the 3D scene starts: `scene_camera`, `scene_view` */
    NEBULA_PASS_TRACED_DRAW, /* items in the ray tracer's scene alone, as things that stop light: never drawn (a body the
                                game hides from its own camera, which still casts: first-person view) */
} NebulaPassKind;

/* A probe pass's `arg0`. */
enum {
    NEBULA_PROBE_DEPTH = 0,    /* each point's value is the depth there */
    NEBULA_PROBE_COMPARED = 1, /* the game only asks value >= user_z (seen): the host may answer 1 seen and 0 hidden, which a
                                  depth test of the point at user_z gives where the scene is still being drawn */
};

/* Pass layers: which output variant a draw pass's items take. */
enum {
    NEBULA_LAYER_SCENE = 0,  /* the 3D scene: jittered, motion and reactive where the settings want them */
    NEBULA_LAYER_MIRROR = 1, /* the mirror's own views */
    NEBULA_LAYER_SCREEN = 2, /* after the scene: at output resolution */
    NEBULA_LAYER_TARGET = 3, /* a texture target */
};

/* A pass. Consecutive draws in one target share `open` = 0 and just continue it; load and store are what tile GPUs need. */
typedef struct NebulaFramePass {
    uint8_t kind;              /* NebulaPassKind */
    uint8_t layer;             /* NEBULA_LAYER_* */
    uint8_t phase;             /* ScenePhase of the game's draw3D, 0xFF none */
    uint8_t clear_channels;    /* 1 colour, 2 alpha, 4 depth */
    uint32_t target;           /* a target's texture id; 0 the frame */
    uint32_t first_item, item_count;
    uint32_t clear_rgba;       /* R << 24 | G << 16 | B << 8 | A */
    uint32_t width, height;    /* DRAW into a target: NEBULA_FRAME_UNITS relative to the frame, else pixels by `pixels` */
    uint32_t channels;         /* COPY_FRAME and the end of a target pass: NebulaTargetChannels */
    uint32_t arg0, arg1;
    uint8_t pixels;            /* width and height are pixels */
    uint8_t load;              /* DRAW: 0 keep, 1 clear to clear_rgba and far */
    uint32_t probe_channel;
    uint8_t reserved;
    uint32_t over_frame_texture, over_frame_depth; /* a target pass reading the frame while the frame stays open; 0 none */
    uint32_t over_frame_channels;
    uint32_t first_probe, probe_count;
    uint32_t scene_camera;     /* the scene's camera, for occlusion, the upscaler and the sky */
    uint32_t scene_view;       /* its projection */
} NebulaFramePass;

/* Frame flags. */
enum {
    NEBULA_FRAME_CAMERA_CUT = 1u << 0, /* the view jumped since the last list: nothing blends across it */
    NEBULA_FRAME_HAS_SCENE = 1u << 1,  /* draws a 3D scene */
    NEBULA_FRAME_HOLD = 1u << 2,       /* nothing new to show: the picture shown stays */
};

struct NebulaScreenProbe;

/* The list. */
typedef struct NebulaFrameList {
    const NebulaFramePass* passes;
    NebulaFrameItem* items;     /* the host orders a pass's opaque items */
    const NebulaFrameView* views;
    const NebulaFrameCamera* cameras;
    const NebulaFrameBlock* blocks;
    const NebulaFrameLightSet* light_sets;
    const NebulaFrameLight* lights;  /* every set's lights, one run per set */
    const uint32_t* light_follow;    /* every set's follow words */
    const NebulaFrameLightOwner* light_set_owners; /* light_set_count of them */
    const NebulaFrameWorldLight* world_lights;     /* the world's lights this frame, ascending by id */
    const NebulaFrameShadowDrop* shadow_drops;     /* the drop shadows the game drew this frame, in its order */
    const NebulaFramePointLight* point_lights;     /* the point lights actors asked for this frame, in their order */
    const NebulaFrameConstants* constants;
    const void* ring_vertices;
    const uint32_t* ring_indices;
    const struct NebulaScreenProbe* probes;
    const float (*instance_rows)[4]; /* NEBULA_ITEM_INSTANCES draws' instances (NebulaParticleInstance, NebulaGlyphInstance) */
    uint32_t pass_count, item_count, view_count, camera_count;
    uint32_t block_count, light_set_count, constants_count, probe_count;
    uint32_t light_count, light_follow_count;
    uint32_t world_light_count;
    uint32_t shadow_drop_count;
    uint32_t point_light_count;
    uint32_t stage_light_known;  /* stage_light and stage_ambient are set */
    uint64_t stage_light;        /* the light of the stage's surfaces where the player is (the area's planet light): the
                                    world lights `stage_light` and `stage_light + 1` */
    float stage_ambient[3];      /* and its ambient, 0 to 1 */
    uint32_t stage_hash;         /* the stage being played: FNV-1a (32 bits) of its name as the game gives it
                                    (MR::getCurrentStageName), 0 becoming 1; 0: none */
    uint64_t ring_vertex_bytes;
    uint32_t ring_index_count;
    uint32_t instance_row_count;
    uint32_t flags;
    uint32_t camera_epoch;
    float sky_up[3];           /* the player's up, unit length, in eye space of the scene's view */
    uint32_t sky_up_known;
    uint32_t game_frame;       /* set by the host: the game frame that made the list */
    const NebulaFrameClipVolume* clip_volumes; /* the clip areas' volumes this frame, clip_volume_count of them */
    uint32_t clip_volume_count;
    uint32_t clip_rule;        /* NEBULA_CLIP_RULE_*: where the clipped map parts show */
    /* Set by the renderer when it no longer reads this list. */
    volatile uint32_t released;
    uint32_t reserved;
} NebulaFrameList;

/* A pipeline: two programs named by their texts' hashes, or for a written shader NEBULA_PROGRAM_SITE | shader number << 1
   (vertex: | 1 its widened lines and points' variant), a state word (NEBULA_STATE_* in nebula_model.h) and a vertex layout:
   position first (stream 0, `position_stride`), the vertex's matrix number (stream 2, or zero for a ring), then
   `input_count` NebulaLayoutInput, as many as the layout names. */
#define NEBULA_PROGRAM_SITE (1ull << 63)
enum { NEBULA_STREAM_ZERO = 15 /* no buffer: the input reads zero */ };

typedef struct NebulaLayoutInput {
    uint16_t semantic; /* NEBULA_INPUT_* (nebula_model.h); a coordinate NEBULA_INPUT_COORD0 + the program's number, in the order of its form's inputs */
    uint8_t stream;
    uint8_t format;   /* 0 three floats, 1 two floats, 2 RGBA8 unorm, 3 one uint */
    uint32_t offset, stride;
} NebulaLayoutInput;

typedef struct NebulaPipelineDesc {
    uint64_t vertex, pixel;
    uint32_t state;
    uint32_t input_count;
    uint32_t position_stride;
} NebulaPipelineDesc;

/* Pipelines.bin (NebulaData/, written by the cooker): "NBPIPEL2", the record count u32, 4 reserved bytes; then each record,
   NEBULA_PIPELINE_RECORD_BYTES of NebulaPipelineDesc's fields in order (no padding) and its `input_count` NebulaLayoutInput
   (12 bytes each) straight after, so a record is NEBULA_PIPELINE_RECORD_BYTES + 12 * input_count bytes. Records are not aligned:
   read them with memcpy. */
#define NEBULA_PIPELINE_FILE_MAGIC "NBPIPEL2"
enum { NEBULA_PIPELINE_FILE_HEADER_BYTES = 16, NEBULA_PIPELINE_RECORD_BYTES = 28, NEBULA_LAYOUT_INPUT_BYTES = 12 };

#ifdef __cplusplus
}
#endif

#endif
