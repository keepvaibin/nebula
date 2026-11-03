/* The game module's interface to Nebula's host (docs/NATIVE_PORT.md, "Module layout"): plain C, so the game module
   (built against the game's own C++ library) and the host (the host's C++ library) share nothing else. The host fills
   NebulaHostServices and calls nebula_game_attach, then runs the game on its simulation thread with nebula_game_run;
   events from host threads (the renderer's PE tokens and stream consumption, the video retrace, audio, input, file reads)
   come back through the nebula_game_* entry points, which may be called from any thread. */
#ifndef NEBULA_GAME_API_H
#define NEBULA_GAME_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(NEBULA_GAME_MODULE)
#define NEBULA_GAME_API __declspec(dllexport)
#else
#define NEBULA_GAME_API __declspec(dllimport)
#endif
#else
#define NEBULA_GAME_API __attribute__((visibility("default")))
#endif

#define NEBULA_GAME_API_VERSION 112u

/* Viewports, scissors, clears, target sizes and picture points count this many units across a frame on each axis. */
#define NEBULA_FRAME_UNITS 65536.0f

/* A colour word, R << 24 | G << 16 | B << 8 | A. */
#define NEBULA_RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

typedef struct NebulaHostThread NebulaHostThread;

#define NEBULA_THREAD_SLOTS 4

/* Audio (the host's mixer): the game's voices mix at the output device's rate, one subframe (1/400 s: 80 samples
   of the game's 32 kHz sound clock) per voices_mix call. A voice plays one source (PCM16 or an oscillator) through
   its filters into mix buses or the surround panner; the game writes its settings, the mixer advances its playback.
   Volumes are gains (a send of 0.5 adds half the voice to its bus; voices_mix's gain scales the output). */
enum {
    NEBULA_AUDIO_BUS_NONE = 0,
    NEBULA_AUDIO_BUS_LEFT,
    NEBULA_AUDIO_BUS_RIGHT,
    NEBULA_AUDIO_BUS_EFFECT0, /* the inputs of the four effect lines */
    NEBULA_AUDIO_BUS_EFFECT1,
    NEBULA_AUDIO_BUS_EFFECT2,
    NEBULA_AUDIO_BUS_EFFECT3,
    NEBULA_AUDIO_BUS_SURROUND_RIGHT, /* matrix-encoded into left and right one subframe later */
    NEBULA_AUDIO_BUS_SURROUND_LEFT,
    NEBULA_AUDIO_BUS_RIGHT_INVERTED, /* subtracted from the right */
    NEBULA_AUDIO_BUS_DISCARD,
    NEBULA_AUDIO_BUS_CENTER, /* both sides at -3 dB */
    NEBULA_AUDIO_BUSES
};

/* A voice's source (NebulaVoice.format). Wave data is PCM16 in host order (the importer decodes the disc's forms). */
enum {
    NEBULA_AUDIO_SILENT = 0,
    NEBULA_AUDIO_PCM16,
    NEBULA_AUDIO_OSCILLATOR, /* waveform `oscillator`, no wave data */
};

/* Oscillator waveforms (NebulaVoice.oscillator); NebulaVoice.rate is its frequency in Hz. */
enum {
    NEBULA_AUDIO_SQUARE = 0,
    NEBULA_AUDIO_SAWTOOTH = 1,
    NEBULA_AUDIO_SQUARE_FM = 2, /* the square, its rate modulated by the surround-right bus (which it consumes) */
    NEBULA_AUDIO_PULSE25 = 3,   /* 25 % duty */
    NEBULA_AUDIO_TRIANGLE = 4,
    NEBULA_AUDIO_CONSTANT = 6,  /* the value `rate` */
    NEBULA_AUDIO_SINE = 7,
    NEBULA_AUDIO_SINE_PM = 10,
    NEBULA_AUDIO_NOISE_A = 11,
    NEBULA_AUDIO_NOISE_B = 12,
};

/* The volume settings a voice can follow (NebulaVoice.category). */
enum {
    NEBULA_SOUND_MUSIC = 0,
    NEBULA_SOUND_VOICE = 1,
    NEBULA_SOUND_EFFECT = 2,
    NEBULA_SOUND_AMBIENCE = 3,
    NEBULA_SOUND_CUTSCENE = 4,
};

typedef struct NebulaAudioSend {
    uint32_t bus;   /* NEBULA_AUDIO_BUS_* */
    float target;   /* gain reached over the next subframe */
    float current;  /* gain now (the mixer's) */
} NebulaAudioSend;

typedef struct NebulaVoice {
    uint16_t active;         /* the game plays it */
    uint16_t finished;       /* the mixer's: the source ended, or a forced stop faded out */
    uint16_t starting;       /* set at play start: the next mix starts the source over (the mixer clears it) */
    uint16_t paused;         /* holds its last output */
    uint16_t forced_stop;    /* every volume halves each subframe; finished once all are zero */
    float rate;              /* source samples played a second */
    uint16_t format;         /* NEBULA_AUDIO_SILENT..OSCILLATOR */
    uint16_t oscillator;     /* NEBULA_AUDIO_SQUARE.. */
    const uint8_t* wave;     /* the source's first byte */
    const uint8_t* wave_end; /* the end of its bytes */
    uint32_t restart;        /* the sample it starts at */
    uint32_t loop_start;     /* samples */
    uint32_t end;            /* samples: the end, or the loop's end when loop is set */
    uint16_t loop;           /* nonzero: plays on from loop_start after end */
    /* The position, advanced by the mixer each subframe (the stream player reads and moves it): */
    uint32_t address;        /* the next sample, in bytes from wave */
    uint32_t left;           /* samples before end */
    uint8_t fir_taps;        /* 0: no FIR (coefficients past the eighth are zero) */
    uint8_t iir_on;
    /* FIR: y[n] = sum fir[k] x[n - taps + k], k < taps; IIR: y[n] = iir[0] x[n] + iir[1] x[n-1] + iir[2] y[n-1] +
       iir[3] y[n-2]; designed for 32 kHz (the mixer keeps their responses at its own rate). */
    float fir[8];
    float iir[4];
    NebulaAudioSend sends[6];
    uint16_t panner;         /* nonzero: the surround panner plays the voice instead of its sends */
    float pan;               /* 0 left .. 1 right */
    float rear;              /* 0 front .. 1 rear */
    float panner_send;       /* effect send */
    float panner_volume;     /* gain now (the mixer's) */
    float panner_target;     /* gain reached over the next subframe */
    uint8_t category;        /* NEBULA_SOUND_*: the volume setting it follows */
    uint8_t named;           /* the game knows the sound playing it (else its source's format chose the category) */
    uint16_t reserved_category;
} NebulaVoice;

/* One of the four effect lines: a delay of `subframes` subframes fed by its input bus, an 8-tap FIR over the delayed
   signal (fir[k] at 8 - k samples of 32 kHz back) applied before (filter bit 0) or after (bit 1) its sends to `bus_a`
   and `bus_b`; the delayed signal (filtered when a bit is set) goes back into the delay with the input. `generation`
   changes when the game starts the line over (its delay then starts silent). filter 0: the line is off. */
typedef struct NebulaEffectLine {
    uint16_t filter;
    uint16_t subframes;
    uint16_t bus_a;
    uint16_t bus_b;
    float volume_a;
    float volume_b;
    float fir[8];
    uint32_t generation;
} NebulaEffectLine;

/* A matrix a draw carries (shape_draw, immediate), named by its place in the draw: position matrix 0-9 (rows of
   four) and its normal matrix (rows of three), which a corner's matrix index names together; texture matrix 0-9 (two
   or three rows of four; 10 the identity's place) and post-transform texture matrix 0-19 (three rows of four; 20 the
   identity's), which texture coordinates name. A draw carries every position and normal matrix its corners read; a
   texture or post-transform matrix stays until a draw or material gives it again. `model_matrix`: the model's draw
   matrix it is (its index + 1; 0 for any other), which names it from frame to frame. */
enum {
    NEBULA_DRAW_MATRIX_POSITION = 0,
    NEBULA_DRAW_MATRIX_NORMAL = 1,
    NEBULA_DRAW_MATRIX_TEXTURE = 2,
    NEBULA_DRAW_MATRIX_TEXTURE_2X4 = 3,
    NEBULA_DRAW_MATRIX_POST_TEXTURE = 4,
};

typedef struct NebulaDrawMatrix {
    uint8_t kind;
    uint8_t place;
    uint16_t model_matrix;
    float values[12];
} NebulaDrawMatrix;

/* What a draw does to the target (shape_draw, immediate): the game's current values, whichever way it came by them. */
typedef struct NebulaPassState {
    uint8_t blend_op, blend_src, blend_dst, logic_op; /* NEBULA_BLEND_*, NEBULA_FACTOR_* */
    uint8_t depth_test, depth_func, depth_write;      /* depth_func NEBULA_COMPARE_* */
    uint8_t cull;                                     /* NEBULA_CULL_* */
    uint8_t color_write, alpha_write;
    uint8_t constant_alpha, constant_alpha_value;     /* the picture's alpha written is this value */
    uint8_t position_matrix;                          /* the place (0-9) vertices without their own take */
    uint8_t line_divisor, point_divisor;              /* lines' and points' texture offset n (1/n texel; 0 none) */
    uint8_t line_offsets, point_offsets;              /* bit per texture coordinate those offsets move */
    uint8_t reserved[3];
    float line_width, point_size;                     /* pixels */
} NebulaPassState;

/* A light: colour, angle and distance terms, position and direction in view space. */
typedef struct NebulaLight {
    uint8_t color[4];
    float angle[3];
    float distance[3];
    float position[3];
    float direction[3];
} NebulaLight;

/* An image (texture_image): its identity (the game's address of its bytes), format (GXTexFmt), size, mipmap levels
   and its bytes (`size` of them, in the format's layout). */
typedef struct NebulaTextureImage {
    const void* bytes;
    uint32_t size;
    uint16_t width, height;
    uint8_t format, levels;
} NebulaTextureImage;

/* How immediate vertices group (NebulaImmediate::primitive). */
typedef enum NebulaPrimitive {
    NEBULA_PRIMITIVE_QUADS = 0,          /* drawn as (0 1 2)(0 2 3) */
    NEBULA_PRIMITIVE_TRIANGLES = 1,
    NEBULA_PRIMITIVE_TRIANGLE_STRIP = 2, /* odd triangles turned to keep the winding */
    NEBULA_PRIMITIVE_TRIANGLE_FAN = 3,   /* around vertex 0 */
    NEBULA_PRIMITIVE_LINES = 4,
    NEBULA_PRIMITIVE_LINE_STRIP = 5,
    NEBULA_PRIMITIVE_POINTS = 6
} NebulaPrimitive;

/* Vertices the game sends as it draws (immediate): `count` of them grouped as `primitive` (NebulaPrimitive), each with the values `attributes` names (NEBULA_SHAPE_NORMAL,
   _COLOR0, _COLOR1, _TEXCOORD0 << n, _POSITION_MATRIX): 3 floats of position and of normal, an R << 24 | G << 16 |
   B << 8 | A word per colour, 2 floats per texture coordinate, a position matrix place (0-9) per position matrix.
   Without a position matrix the vertices take the current one. `matrices` (`matrix_count`): the position and normal
   matrices they read and texture matrices given since the last record (NebulaDrawMatrix). */
typedef struct NebulaImmediate {
    uint8_t primitive;
    uint8_t reserved[3];
    uint32_t attributes;
    uint32_t count;
    uint32_t reserved2;
    const float* position;
    const float* normal;
    const uint32_t* color[2];
    const float* texcoord[8];
    const uint8_t* position_matrix;
    const struct NebulaDrawMatrix* matrices;
    uint32_t matrix_count;
    NebulaPassState pass;
} NebulaImmediate;

/* A texture coordinate a shape draw generates differently from its material (shape_draw): type and source as
   J3DNativeTexGen encodes them, normalize, the post-transform row. */
typedef struct NebulaShapeTexGen {
    uint8_t coord, type, source, normalize, post_matrix;
    uint8_t reserved[3];
} NebulaShapeTexGen;

/* The matrices a shape draw's corners take when they carry none (shape_draw): the position matrix's place (0-9) and
   texture coordinates 0-7's texture matrix places (0-9, 10 the identity). */
typedef struct NebulaShapeCurrent {
    uint8_t position;
    uint8_t texture[8];
} NebulaShapeCurrent;

typedef struct NebulaMaterialTexture {
    uint64_t image;               /* the texture: its record's address (0: no texture) */
    uint16_t width, height;       /* its size (the material's coordinate scales) */
    uint8_t wrap_s, wrap_t;       /* 0 clamp, 1 repeat, 2 mirror */
    uint8_t min_linear, mag_linear; /* minified within a level / magnified: 1 linear, 0 nearest */
    uint8_t mip_filter;           /* 0 the first level only, 1 the nearest level, 2 between levels */
    uint8_t edge_lod, anisotropy, bias_clamp;
    float min_lod, max_lod, lod_bias;
} NebulaMaterialTexture;

typedef struct NebulaMaterialLight {
    float position[3], direction[3];
    float angle[3], distance[3];  /* attenuation coefficients */
    uint8_t color[4];
} NebulaMaterialLight;

/* A native material (native_material): a shader compiled at build (runtime/src/render/materials/<name>.hlsl, one
   NebulaShaderId each), its constants (the shader's MaterialConstants: floats, in its declaration order), its textures
   (texture i at the shader's t<i>; image 0: none) and its fixed-function state. The draws that follow use it until the
   next material (native or not) or drawing-state change. */
typedef enum NebulaShaderId {
    NEBULA_SHADER_NONE = 0,
    NEBULA_SHADER_WHIRLPOOL = 1, /* WhirlPool: the swirl over the warped screen behind */
    NEBULA_SHADER_OCEAN_BOWL = 2, /* OceanBowl: Beach Bowl's sea over the warped screen behind */
    NEBULA_SHADER_SCREEN_BLUR = 3, /* ScreenBlurEffect: the blurred scene where its depth lies beyond a threshold */
    NEBULA_SHADER_FLAT_COLOR = 4,  /* a flat colour (constants: rgba) */
    NEBULA_SHADER_CLIP_FILL = 5,   /* ClipFieldFillDraw: a colour where the screen alpha mask is set (or clear) */
    NEBULA_SHADER_BIG_BUBBLE = 6,  /* BigBubbleDrawer: the rainbow bubble over its environment map */
    NEBULA_SHADER_WHIRLPOOL_ACCELERATOR = 7, /* WhirlPoolAccelerator: two colours mixed by the stream's texture */
    NEBULA_SHADER_SPIN_DRIVER_PATH = 8,      /* SpinDriverPathDrawer: a launch star path's colour under its mask */
    NEBULA_SHADER_SPIDER_THREAD = 9,         /* SpiderThread: the web's thread warped by its indirect texture */
    NEBULA_SHADER_TEX_MODULATE = 10,         /* a texture times the vertex colour, its alpha times a constant */
    NEBULA_SHADER_WATER_PLANT = 11,          /* WaterPlant: the plant texture, alpha-tested, in the water's fog */
    NEBULA_SHADER_OCEAN_RING_PIPE_INSIDE = 12, /* OceanRingPipeInside: the pipe's inside, warped by its own texture */
    NEBULA_SHADER_OCEAN_SPHERE = 13,         /* OceanSphere: the water sphere's surface */
    NEBULA_SHADER_OCEAN_SPHERE_ENV = 14,     /* OceanSphere: its inside seen from without (environment map) */
    NEBULA_SHADER_OCEAN_RING = 15,           /* OceanRingDrawer: the ring's water over the warped screen behind */
    NEBULA_SHADER_OCEAN_RING_BLOOM = 16,     /* OceanRingDrawer: its bloom */
    NEBULA_SHADER_WATER_ROAD = 17,           /* WaterRoad: the water tube (high and low detail) */
    NEBULA_SHADER_OCEAN_RING_PIPE_OUTSIDE = 18, /* OceanRingPipeOutside: the pipe's highlight with its rim */
    NEBULA_SHADER_COLOR_TEX_ALPHA = 19,      /* a colour at a texture's alpha */
    NEBULA_SHADER_VERTEX_COLOR = 20,         /* TDDraw draw mode 0: the vertex colour */
    NEBULA_SHADER_DD_TEXTURE = 21,           /* TDDraw draw mode 1: a texture */
    NEBULA_SHADER_DD_TEXTURE_COLOR = 22,     /* TDDraw draw mode 2: a texture plus a colour */
    NEBULA_SHADER_TEX_VERTEX_ALPHA = 23,     /* a texture, its alpha times the vertex alpha */
    NEBULA_SHADER_TEX_ALPHA_LERP = 24,       /* two colours mixed by a texture's alpha (MarioFoo, WarpPod) */
    NEBULA_SHADER_TRAMPLE_STAR = 25,         /* TrampleStar: the lit star with Mario's shadow */
    NEBULA_SHADER_TEX_CONST_ALPHA = 26,      /* a texture's colour at a constant alpha (FullScreenBlur) */
    NEBULA_SHADER_FONT_TEXT = 27,            /* nw4r CharWriter: glyphs mapped between two colours, by the vertex colour */
    NEBULA_SHADER_J2D_PICTURE = 28,          /* J2DPicture: up to four textures blended by constant ratios, remapped */
    NEBULA_SHADER_FUR = 29,                  /* FurDrawer: a fur shell over the body texture, lit */
    NEBULA_SHADER_PARTICLE = 30,             /* JPA particles: the texture by the emitter's colours, its colour mode */
    NEBULA_SHADER_OCEAN_BOWL_BLOOM = 31,     /* Beach Bowl's sea from under water: the water at two scrolls over grey */
    NEBULA_SHADER_MII_FACE = 32,             /* Mii faces: a part's form, RFL's light or the Mii NPCs' stages */
    NEBULA_SHADER_MII_TEXTURE = 33,          /* a Mii's face texture parts: eyes, eyebrows, mouth, mustache, mole */
    NEBULA_SHADER_IMAGE_EFFECT = 34,         /* the game's screen texture passes: modulate, threshold, masks, edge */
    NEBULA_SHADER_GAUSSIAN_BLUR = 35,        /* ImageEffectLocalUtil::blurTexture: one axis of a Gaussian */
    NEBULA_SHADER_DOF_FINAL = 36,            /* the game's depth of field: blurred over sharp by the depth's ramp */
    NEBULA_SHADER_NORMAL_MAP = 37,           /* NormalMapBase: a normal-mapped shape lit through a light gradient */
    NEBULA_SHADER_MOVIE = 38,      /* MovieDraw (THPDraw): a movie frame's Y, U and V planes to RGB */
    NEBULA_SHADER_WATER_CAMERA = 39, /* WaterCameraFilter: the screen warped and darkened under water */
    NEBULA_SHADER_FRAME_RIPPLE = 40, /* MarioActor's heat and cold ripples: the frame, each row moved by a wave */
    NEBULA_SHADER_COUNT,
    /* The J3D material set (tools/dev/gen_j3d_materials.py): J3D_FIRST + n - 1 for its material n. */
    NEBULA_SHADER_J3D_FIRST = 256
} NebulaShaderId;

enum {
    NEBULA_BLEND_REPLACE = 0,  /* blend_op: the source replaces */
    NEBULA_BLEND_ADD = 1,      /* source * blend_src + destination * blend_dst */
    NEBULA_BLEND_SUBTRACT = 2, /* destination - source */
    NEBULA_BLEND_LOGIC = 3,    /* logic_op (VkLogicOp's order: clear, and, reverse and, copy, ..., set) */
};
enum {
    NEBULA_FACTOR_ZERO = 0, NEBULA_FACTOR_ONE, NEBULA_FACTOR_SRC_COLOR, NEBULA_FACTOR_INV_SRC_COLOR,
    NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA, NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_INV_DST_ALPHA,
    NEBULA_FACTOR_DST_COLOR, NEBULA_FACTOR_INV_DST_COLOR,
};
enum {
    NEBULA_COMPARE_NEVER = 0, NEBULA_COMPARE_LESS, NEBULA_COMPARE_EQUAL, NEBULA_COMPARE_LEQUAL,
    NEBULA_COMPARE_GREATER, NEBULA_COMPARE_NEQUAL, NEBULA_COMPARE_GEQUAL, NEBULA_COMPARE_ALWAYS,
};
/* Culling of the game's back or front faces, in its own winding. */
enum { NEBULA_CULL_NONE = 0, NEBULA_CULL_BACK = 1, NEBULA_CULL_FRONT = 2, NEBULA_CULL_ALL = 3 /* draw nothing */ };
/* blend_op, depth_test or cull: the drawing state's blending, depth or culling as it is (a material that replaces only
   the shader of the draws after the state the game set: J3DModelX's override passes). */
enum { NEBULA_NATIVE_KEPT = 0xFF };
/* A texture's image: the drawing state's map as it is (its image, size and sampling: textures the game loads per draw). */

typedef struct NebulaNativeMaterial {
    uint32_t shader;           /* NebulaShaderId */
    uint8_t blend_op, blend_src, blend_dst, logic_op;
    uint8_t depth_test, depth_func, depth_write, early_depth; /* depth_func NEBULA_COMPARE_* */
    uint8_t cull, color_write, alpha_write; /* writes: 0 off, 1 on, 2 as they were */
    uint8_t light_mask;        /* the lights below the material sets (the others keep what was set before) */
    float constants[128];
    struct {
        uint64_t image;        /* the texture: its record's address (or a capture's key); the record has its size */
        uint8_t repeat_s, repeat_t; /* 0 clamp, 1 repeat, 2 mirror */
        uint8_t mag_linear;    /* magnified: 1 linear, 0 nearest */
        uint8_t min_linear;    /* minified within a level: 1 linear, 0 nearest */
        uint8_t mip_filter;    /* 0 the first level only, 1 the nearest level, 2 between levels */
        uint8_t anisotropy;    /* 0 off, 1 two, 2 four samples */
        uint8_t edge_lod;      /* 1 the level from the texel's edges */
        float lod_bias, min_lod, max_lod;
        uint8_t reserved[3];
    } textures[8];
    NebulaMaterialLight light[8];
    /* Texgen i's post-transform matrix row (GX_PTTEXMTX0 + 3k as 3k; 61 the identity), | 0x80 to normalize first: the
       drawing state's until a draw's own texgens replace it (a shape's env map or projection); NEBULA_NATIVE_KEPT keeps the
       draw's (an instance's differed list that loads no texgens). */
    uint8_t texgen_post[8];
    uint32_t constants_only; /* bit 0: only the constants of the material the draws use change (the game folding a state
                                change of one of its values in); its state, textures and lights stay as set since; bit 1: the first 84 constants are
                                the register file (j3d_material.hlsli's layout), which the game keeps current) */
} NebulaNativeMaterial;

/* A model's vertex array as values (shape_array): `count` entries of `kind`, each `key` (the game's address of the
   array it converts; an array made there later replaces it) naming it. Positions and normals are 3 floats, a bump-mapped model's normals 9 (normal, tangent,
   binormal), colours one uint32_t each (R << 24 | G << 16 | B << 8 | A), texture coordinates 2 floats. */
enum {
    NEBULA_SHAPE_ARRAY_POSITION = 0,
    NEBULA_SHAPE_ARRAY_NORMAL = 1,
    NEBULA_SHAPE_ARRAY_NBT = 2,
    NEBULA_SHAPE_ARRAY_COLOR = 3,
    NEBULA_SHAPE_ARRAY_TEXCOORD = 4,
};

typedef struct NebulaShapeArray {
    uint64_t key;
    uint32_t kind;
    uint32_t count;
    const void* values;
} NebulaShapeArray;

/* One corner of a shape group: the entry it takes from each array and its matrices' places in a draw
   (NebulaDrawMatrix: position 0-9, texture 0-9 or 10 the identity). */
typedef struct NebulaShapeCorner {
    uint16_t position;
    uint16_t normal; /* for a bump-mapped group with three normal indices: the normal's */
    uint16_t tangent;
    uint16_t binormal;
    uint16_t color[2];
    uint16_t texcoord[8];
    uint8_t position_matrix;
    uint8_t texture_matrix[8];
    uint8_t reserved[3];
} NebulaShapeCorner;

/* Which arrays a shape group's corners read and which matrix places they carry (NebulaShapeGeometry::attributes). */
enum {
    NEBULA_SHAPE_NORMAL = 1u << 0,
    NEBULA_SHAPE_COLOR0 = 1u << 1,
    NEBULA_SHAPE_COLOR1 = 1u << 2,
    NEBULA_SHAPE_TEXCOORD0 = 1u << 3,         /* to 1u << 10 for texture coordinate 7 */
    NEBULA_SHAPE_NBT = 1u << 11,              /* normals from the NBT array */
    NEBULA_SHAPE_NBT_THREE = 1u << 12,        /* separate normal, tangent and binormal entries */
    NEBULA_SHAPE_POSITION_MATRIX = 1u << 13,  /* corners carry their position matrix place */
    NEBULA_SHAPE_TEXTURE_MATRIX0 = 1u << 14,  /* to 1u << 21: corners carry texture coordinate 0-7's matrix place */
    NEBULA_SHAPE_OWN_POSITIONS = 1u << 22,    /* positions (and colours) are the group's own values, not an array a
                                                 model or the game keeps: geometry the game builds */
};

/* A shape group (shape_geometry): its corners, each distinct one once, and its triangles (three corner indices each,
   front faces as the game winds them). `arrays` name the arrays its corners read by default (position, normal or NBT,
   colour 0, colour 1, texture coordinates 0-7; 0 where it reads none); shape_draw names the first three per draw. */
typedef struct NebulaShapeGeometry {
    uint64_t key; /* the group's identity: the game's address of it (a group made there later replaces it) */
    uint32_t attributes;
    uint32_t reserved;
    uint64_t arrays[12];
    uint32_t corner_count;
    uint32_t index_count;
    const NebulaShapeCorner* corners;
    const uint16_t* indices;
} NebulaShapeGeometry;

/* A file or folder of the asset store. */
typedef struct NebulaAssetEntry {
    int32_t index;      /* in the file table; 0 is the root folder */
    uint32_t length;    /* a file's length in bytes; a folder's, from its first file to its last one's end */
    uint64_t position;  /* the first byte in the store */
    uint32_t children;  /* a folder's entries */
    uint8_t folder;
    uint8_t reserved[3];
    const char* name;   /* valid for the process's life */
} NebulaAssetEntry;

/* The settings the game reads from the player's system (system_setting). */
enum {
    NEBULA_SETTING_LANGUAGE = 1,
    NEBULA_SETTING_SOUND_MODE,
    NEBULA_SETTING_WIDESCREEN,
};

/* A player's controller as the game reads it (input_read): one sample per moment, oldest first. */
enum {
    NEBULA_BUTTON_A = 1u << 0,
    NEBULA_BUTTON_B = 1u << 1,
    NEBULA_BUTTON_1 = 1u << 2,
    NEBULA_BUTTON_2 = 1u << 3,
    NEBULA_BUTTON_PLUS = 1u << 4,
    NEBULA_BUTTON_MINUS = 1u << 5,
    NEBULA_BUTTON_HOME = 1u << 6,
    NEBULA_BUTTON_UP = 1u << 7,
    NEBULA_BUTTON_DOWN = 1u << 8,
    NEBULA_BUTTON_LEFT = 1u << 9,
    NEBULA_BUTTON_RIGHT = 1u << 10,
    NEBULA_BUTTON_C = 1u << 11, /* the Nunchuk's */
    NEBULA_BUTTON_Z = 1u << 12,
};
enum {
    NEBULA_SHAKE_REMOTE = 1u << 0,  /* held while the remote (or the controller standing in for it) is shaken */
    NEBULA_SHAKE_NUNCHUK = 1u << 1,
};
typedef struct NebulaInputSample {
    uint32_t buttons;       /* NEBULA_BUTTON_* held */
    float stick[2];         /* the Nunchuk stick, -1..1: right, up */
    float pointer[2];       /* where the remote points, -1..1 across the picture: right, down (pointer_on) */
    float roll[2];          /* the remote's roll as the screen sees it, a unit vector: (1, 0) level */
    float distance;         /* from the screen, in the sensor bar's units (pointer_on) */
    float gravity[3];       /* the remote's acceleration in g (1 at rest): x out of its left side, y toward the
                               expansion port, z out of the A face */
    float nunchuk_gravity[3]; /* the Nunchuk's, the same axes */
    uint8_t pointer_on;     /* 1: the remote points at the picture */
    uint8_t shake;          /* NEBULA_SHAKE_* */
    uint8_t reserved[2];
} NebulaInputSample;
typedef struct NebulaInputDevice {
    uint8_t connected;
    uint8_t nunchuk;        /* 1: a Nunchuk is attached */
    uint8_t battery;        /* 0 empty .. 4 full */
    uint8_t reserved;
} NebulaInputDevice;
/* What the game asks of a player's controller (input_control). */
enum {
    NEBULA_INPUT_RUMBLE = 1, /* value: 0 off, 1 on */
    NEBULA_INPUT_RELEASE,    /* the game lets the controller go (players 3 and 4 in play) */
};

/* The HOME Menu (hbm). NEBULA_HBM_POLL returns the choice so far: -1 none yet, 0 close, 1 leave the game, 2 reset.
   The host keeps the menu (its keys and controllers' buttons) and the game draws it: NEBULA_HBM_VIEW returns -1 while
   it is closed, else the selected answer in bits 0-7 and in bits 8-15 the choice it asks about (0 none: the list,
   whose rows are close, reset and leave; 1 leave or 2 reset: "are you sure?", whose answers are no and yes); its
   value is 1 when the game draws what it returns (the host then draws no menu of its own). */
enum {
    NEBULA_HBM_OPEN = 1, /* value: 1 on a 16:9 screen */
    NEBULA_HBM_POLL,
    NEBULA_HBM_CLOSE,    /* closed at once (a reset or power event) */
    NEBULA_HBM_VIEW,     /* value: 1 drawing it */
    NEBULA_HBM_POINT,    /* value: the answer under the pointer, selected */
    NEBULA_HBM_CHOOSE,   /* value: the answer clicked */
};

/* A hidden test run's request (test_scene_frame): tools/test_driver/test_actions.h, absent from a release. */
#if defined(NEBULA_RELEASE)
typedef struct NebulaTestActionAbsent NebulaTestAction;
#else
#include "../../tools/test_driver/test_actions.h"
#endif

/* The picture's shape as the game draws it (NebulaFrameClock::display), from the output's shape the host latches at
   each frame (display_extent): the camera, the 2D screen and the layouts widen with it. */
typedef struct NebulaDisplay {
    int32_t wide;          /* 0: 16:9, nothing changes */
    float content_aspect;  /* width / height, 4:3 to 32:9 */
    int32_t screen_width;  /* MR::getScreenWidth in the 16:9 mode (832 at 16:9) */
    float layout_width;    /* the NW4R layout canvas (608 at 16:9 and narrower) */
    float vertical_fit;    /* the layouts' height scale under 16:9 (1 at 16:9 and wider) */
} NebulaDisplay;

/* The input mode the player chose (NEBULA_OPTION_INPUT_MODE). */
typedef enum NebulaInputMode {
    NEBULA_INPUT_MODE_AUTO = 0,
    NEBULA_INPUT_MODE_KEYBOARD_MOUSE,
    NEBULA_INPUT_MODE_CONTROLLER,
    NEBULA_INPUT_MODE_WII_REMOTE
} NebulaInputMode;

/* Nebula options the game follows (option). */
enum {
    NEBULA_OPTION_TEXT_SPEED = 1, /* 1 to 3: text steps revealed per frame; 4: each page at once */
    NEBULA_OPTION_TEST_DUMP_LAYOUTS, /* tests: 1 logs each layout's pane tree once (GALAXY_TEST_DUMP_LAYOUTS) */
    NEBULA_OPTION_DRAW_IDENTITIES, /* 1 while the renderer reads host markers (NebulaRender::hostMarker) */
    NEBULA_OPTION_INPUT_MODE,        /* NebulaInputMode: the input mode the player chose */
    NEBULA_OPTION_BLOOM,             /* 1 when the renderer's bloom is built: BloomEffect and BloomEffectSimple give it
                                        their values (NebulaRender::bloomMarker); 0 no bloom */
    NEBULA_OPTION_RT_PREPARATION,    /* ray tracing's shader preparation, which the title waits for (NebulaRtPrepare):
                                        0 none, 1 the renderer opens its pipeline library, 2 it compiles the pipelines
                                        (the preparation screen shows) */
    NEBULA_OPTION_RT_PREPARE_PROGRESS, /* while NEBULA_OPTION_RT_PREPARATION is 2: its progress, 0 to 10000, never
                                          going back */
    NEBULA_OPTION_TEST_NATIVE_J3D,     /* tests (GALAXY_TEST_NATIVE_J3D=lo-hi): first << 16 | last J3D material number
                                          (from 1) drawn, the others not; 0 all */
    NEBULA_OPTION_TEST_ARENA_FILL,     /* tests (GALAXY_TEST_ARENA_FILL=<hex byte>): 0x100 | the byte every arena block starts as; 0 without it */
    NEBULA_OPTION_TEST_TRACE_LOADS,    /* tests (GALAXY_TRACE_LOADS): 1 logs what each scene change loads */
    NEBULA_OPTION_TEST_DUMP_DRAW_INPUTS, /* tests (GALAXY_DUMP_DRAW_INPUTS=<shader id>): the native shader id whose
                                            material's draws the game tags on both paths (0 none) */
};

/* game_light: the game's lights, from the objects that own them; `values` per kind. */
enum {
    NEBULA_LIGHT_SUN = 0,       /* BrightSun: direction from the camera towards it (3), its distance */
    NEBULA_LIGHT_STAR_CORE,     /* the Comet Observatory's AstroCore: position (3), scale */
    NEBULA_LIGHT_POINT,         /* LightPointCtrl's point light: position (3), colour (3), distance where it halves */
    NEBULA_LIGHT_POINT_REQUEST, /* one actor's request for it (id the actor), as NEBULA_LIGHT_POINT */
    NEBULA_LIGHT_WORLD,         /* a world-fixed area light far out: position (3), colour (3) */
    NEBULA_LIGHT_AREA,          /* the stage's planet light (LightDirector's default area light), then */
    NEBULA_LIGHT_PLAYER,        /* the player's current light: ambient (3), then the key and the fill light each:
                                   colour (3), position (3; the camera's eye space when it follows the camera),
                                   follows the camera (1) */
};

/* scene_marker: the scene bounds among the records. */
enum {
    NEBULA_SCENE_3D_BEGIN = 0, /* GameScene::draw3D's begin, with the camera and the player's up */
    NEBULA_SCENE_3D_END,
    NEBULA_SCENE_2D_BEGIN, /* GameScene::draw2D's begin */
    NEBULA_SCENE_2D_END,
};

/* The movie player's sound (movie_audio_control). */
enum {
    NEBULA_MOVIE_AUDIO_OPEN = 1, /* value 1: mono output (both sides their mean at -3 dB) */
    NEBULA_MOVIE_AUDIO_CLOSE,
    NEBULA_MOVIE_AUDIO_RESET,    /* the waiting frames dropped, the volume at its target */
    NEBULA_MOVIE_AUDIO_VOLUME,   /* value 0-127 (the gain is its square), reached over `milliseconds` */
    NEBULA_MOVIE_AUDIO_RESUME,   /* after a pause: once two frames wait, 150 ms of silence, then a 31 ms fade in */
};

/* The sound clock (sound_clock). */
enum {
    NEBULA_AUDIO_START = 1, /* value: a frame's length in 32 kHz samples; the host calls nebula_game_sound_frame once
                               a frame's length */
    NEBULA_AUDIO_STOP,
};

/* One operation on a save file of the game's (by name, e.g. "GameData.bin"), in the player's save folder. */
enum {
    NEBULA_SAVE_READ = 1,  /* the whole file into buffer (capacity size); *length = its length */
    NEBULA_SAVE_WRITE,     /* buffer (size bytes) as the whole file, replaced atomically */
    NEBULA_SAVE_DELETE,
};
/* A request's status: pending until the host finishes, then done or what went wrong. */
enum {
    NEBULA_SAVE_PENDING = 1,
    NEBULA_SAVE_DONE = 0,
    NEBULA_SAVE_NO_FILE = -1,   /* read or delete of a file that is not there */
    NEBULA_SAVE_TOO_LARGE = -2, /* read: the file is longer than the buffer */
    NEBULA_SAVE_FAILED = -3,    /* the file system refused */
};
typedef struct NebulaSaveRequest {
    uint32_t op;
    const char* name;
    void* buffer;
    uint32_t size;
    uint32_t* length;          /* read: the file's length (may be NULL) */
    volatile int32_t* status;  /* the host sets NEBULA_SAVE_PENDING at once and the result when done */
} NebulaSaveRequest;

/* Nebula's settings menu as the host has it now (settings_view), for the game to draw in its own windows and words.
   The host keeps the settings, their pages, the draft and its apply, and takes the keyboard and controllers while the
   menu is open; the game shows it (and takes the pointer, settings_input). Texts are the host's own, in English
   (UTF-8): the game names each in the played language (NebulaSettingsMenu). */
#define NEBULA_SETTINGS_PAGES 8
#define NEBULA_SETTINGS_ROWS 96

enum {
    NEBULA_SETTINGS_ROW_HEADING = 1,     /* a section's title: never selected */
    NEBULA_SETTINGS_ROW_CHANGES = 2,     /* left and right change its value */
    NEBULA_SETTINGS_ROW_NAME = 4,        /* its value is a name (a monitor, a controller, a mod, a galaxy): shown as is */
    NEBULA_SETTINGS_ROW_ACTIVE = 8,      /* its value is the controller in use */
    NEBULA_SETTINGS_ROW_LABEL_NAME = 16, /* its label is a name (a mod's, its option's): shown as is */
    NEBULA_SETTINGS_ROW_BUTTONS = 32,    /* its value is its buttons' words, '|' between them ('-' before one:
                                            unavailable): left and right move between them (`selected_button`), A
                                            presses one */
    NEBULA_SETTINGS_ROW_MOD_OFF = 64,    /* the MODS page: a mod in the list of mods off (with HEADING: its title) */
    NEBULA_SETTINGS_ROW_MOD_ON = 128,    /* a mod in the list of mods on, the higher one winning (with HEADING: its title) */
    NEBULA_SETTINGS_ROW_HELD = 256,      /* the mod picked up to move: the arrows move it */
    NEBULA_SETTINGS_ROW_SLIDER = 512,    /* a setting along a range: `slider_steps` from its first value, at `slider_at` */
    NEBULA_SETTINGS_ROW_BINDING = 1024,  /* a control's binding: changing it waits for an input */
    NEBULA_SETTINGS_ROW_SHARED = 2048,   /* a binding with an input another control is bound to as well: shown red */
};

/* The game's button icons (its picture font's A, B, C, Z, 1, 2, Plus, Minus, HOME and Control Pad, in this order):
   their style and, for each, the controller's button it stands for (galaxy::input::ButtonIconSpec). */
#define NEBULA_BUTTON_ICONS 10
enum {
    NEBULA_ICON_STYLE_WII = 0,
    NEBULA_ICON_STYLE_XBOX,
    NEBULA_ICON_STYLE_NINTENDO,
    NEBULA_ICON_STYLE_PLAYSTATION,
    NEBULA_ICON_STYLE_KEYBOARD, /* the keyboard and mouse: each icon a key (NEBULA_ICON_KEY) */
};
enum {
    NEBULA_ICON_KEEP = 0, /* the game's own icon */
    NEBULA_ICON_FACE,     /* place: 0 south, 1 east, 2 west, 3 north */
    NEBULA_ICON_SHOULDER, /* place: 0 left, 1 right */
    NEBULA_ICON_TRIGGER,
    NEBULA_ICON_STICK,    /* the stick's click */
    NEBULA_ICON_START,    /* Xbox Menu, Plus, Options */
    NEBULA_ICON_BACK,     /* Xbox View, Minus, Share or Create */
    NEBULA_ICON_HOME,
    NEBULA_ICON_DPAD,     /* KEYBOARD: the four keys of `pad_keys` */
    NEBULA_ICON_KEY,      /* place: NEBULA_KEY_* */
};
/* The keys and mouse buttons a keyboard icon shows (NEBULA_ICON_KEY's place). Those from NEBULA_KEY_LAYOUT_FIRST print
   what the keyboard's layout puts on them (NebulaButtonIcons::layout); NEBULA_KEY_OTHER is any other key, unnamed. */
enum {
    NEBULA_KEY_A = 0,                        /* A to Z */
    NEBULA_KEY_0 = NEBULA_KEY_A + 26,        /* 0 to 9 */
    NEBULA_KEY_F1 = NEBULA_KEY_0 + 10,       /* F1 to F24 */
    NEBULA_KEY_NUM0 = NEBULA_KEY_F1 + 24,    /* the number pad's 0 to 9 */
    NEBULA_KEY_NUM_ADD = NEBULA_KEY_NUM0 + 10,
    NEBULA_KEY_NUM_SUBTRACT,
    NEBULA_KEY_NUM_MULTIPLY,
    NEBULA_KEY_NUM_DIVIDE,
    NEBULA_KEY_NUM_DECIMAL,
    NEBULA_KEY_SPACE,
    NEBULA_KEY_ENTER,
    NEBULA_KEY_TAB,
    NEBULA_KEY_BACKSPACE,
    NEBULA_KEY_ESCAPE,
    NEBULA_KEY_SHIFT,
    NEBULA_KEY_LEFT_SHIFT,
    NEBULA_KEY_RIGHT_SHIFT,
    NEBULA_KEY_CTRL,
    NEBULA_KEY_LEFT_CTRL,
    NEBULA_KEY_RIGHT_CTRL,
    NEBULA_KEY_ALT,
    NEBULA_KEY_LEFT_ALT,
    NEBULA_KEY_RIGHT_ALT,
    NEBULA_KEY_UP,
    NEBULA_KEY_DOWN,
    NEBULA_KEY_LEFT,
    NEBULA_KEY_RIGHT,
    NEBULA_KEY_INSERT,
    NEBULA_KEY_DELETE,
    NEBULA_KEY_HOME,
    NEBULA_KEY_END,
    NEBULA_KEY_PAGE_UP,
    NEBULA_KEY_PAGE_DOWN,
    NEBULA_KEY_CAPS_LOCK,
    NEBULA_KEY_NUM_LOCK,
    NEBULA_KEY_SCROLL_LOCK,
    NEBULA_KEY_PAUSE,
    NEBULA_KEY_PRINT_SCREEN,
    NEBULA_KEY_MENU,
    NEBULA_KEY_LEFT_WIN,
    NEBULA_KEY_RIGHT_WIN,
    NEBULA_KEY_LAYOUT_FIRST, /* US: ; = , - . / ` [ \ ] ' (none) and the ISO key's \ */
    NEBULA_KEY_MOUSE_LEFT = NEBULA_KEY_LAYOUT_FIRST + 13,
    NEBULA_KEY_MOUSE_RIGHT,
    NEBULA_KEY_MOUSE_MIDDLE,
    NEBULA_KEY_MOUSE_BACK,    /* the side buttons (Mouse4, Mouse5) */
    NEBULA_KEY_MOUSE_FORWARD,
    NEBULA_KEY_OTHER, /* a key without its own icon: its name (NebulaButtonIcons::other) on a key of its own */
    NEBULA_KEYS,
};
#define NEBULA_KEY_NAME_CHARS 8
#define NEBULA_KEY_LAYOUT_KEYS 13
typedef struct NebulaButtonIcons {
    uint8_t style; /* NEBULA_ICON_STYLE_* */
    uint8_t glyphs[NEBULA_BUTTON_ICONS]; /* NEBULA_ICON_KEEP... */
    uint8_t places[NEBULA_BUTTON_ICONS];
    uint8_t wii_remote; /* a real Wii Remote is in use: the game's pictures of one show */
    uint8_t pad_keys[4]; /* KEYBOARD: the Control Pad's up, down, left and right keys (NEBULA_KEY_*) */
    /* KEYBOARD: the capital each layout key prints (from NEBULA_KEY_LAYOUT_FIRST; UCS-2), 0 where the US label shows */
    uint16_t layout[NEBULA_KEY_LAYOUT_KEYS];
    /* KEYBOARD: each icon on NEBULA_KEY_OTHER, its key's name in capitals (ASCII, at most 6), else empty */
    char other[NEBULA_BUTTON_ICONS][NEBULA_KEY_NAME_CHARS];
} NebulaButtonIcons;

/* A picture of Nebula's own (its button icons) for GALAXY_EXPORT_ART: `texels` the texture the game draws, which names
   it in a texture pack (none for a preview alone), `rgba` the same as RGBA8 and `hd` the picture `hd_width` x
   `hd_height` (both straight alpha), `name` its preview's path ("PlayStation/face-south"). */
typedef struct NebulaArtExport {
    const char* name;
    const void* texels;
    uint32_t width;
    uint32_t height;
    uint32_t format; /* GX_TF_* */
    const uint8_t* rgba;
    const uint8_t* hd;
    uint32_t hd_width;
    uint32_t hd_height;
} NebulaArtExport;

/* What the line under the page says (with `status_number` or `status_text` where it has one). */
enum {
    NEBULA_SETTINGS_STATUS_HELP = 0,         /* the selected row's description (`help`) */
    NEBULA_SETTINGS_STATUS_CAPTURE_KEY,      /* a binding waits for a key or mouse button */
    NEBULA_SETTINGS_STATUS_CAPTURE_PAD,      /* a binding waits for a button, stick or trigger */
    NEBULA_SETTINGS_STATUS_MESSAGE,          /* a message for a moment (`status_text`) */
    NEBULA_SETTINGS_STATUS_DLSS_ASK,         /* download DLSS? yes: NEBULA_SETTINGS_APPLY, no: NEBULA_SETTINGS_CLOSE */
    NEBULA_SETTINGS_STATUS_DLSS_INSTALLING,  /* DLSS downloads and installs */
    NEBULA_SETTINGS_STATUS_DLSS_RESTART,     /* DLSS installed: restart now? yes: APPLY, later: CLOSE */
    NEBULA_SETTINGS_STATUS_DLSS_FAILED,      /* DLSS was not installed */
    NEBULA_SETTINGS_STATUS_KEEP_DISPLAY,     /* keep the new display? (`status_number` seconds before it reverts) */
    NEBULA_SETTINGS_STATUS_SAVE_FAILED,      /* the settings file could not be written */
    NEBULA_SETTINGS_STATUS_PENDING,          /* `status_number` changes wait to be applied */
    NEBULA_SETTINGS_STATUS_APPLIED,          /* just applied */
    NEBULA_SETTINGS_STATUS_LANGUAGE_RESTART, /* the new language applies at the next start */
    NEBULA_SETTINGS_STATUS_MODS_RESTART,     /* the mod changes apply at the next start */
    NEBULA_SETTINGS_STATUS_GPU_RESTART,      /* the new graphics card applies at the next start */
    NEBULA_SETTINGS_STATUS_LEAVE,            /* leaving with changes not applied: apply them or discard them? */
    NEBULA_SETTINGS_STATUS_MODS_RESTART_ASK, /* mod changes applied: restart now? yes: APPLY, later: CLOSE */
    NEBULA_SETTINGS_STATUS_JOYCON,           /* Pair Joy-Con: `status_number` NEBULA_JOYCON_* bits; CLOSE leaves */
    NEBULA_SETTINGS_STATUS_RT_RESTART_ASK,   /* ray tracing turned on before its shaders are prepared (they are at the
                                                next start): restart now? yes: APPLY, later: CLOSE */
    NEBULA_SETTINGS_STATUS_DIAGNOSTICS_ASK,  /* restart and record a diagnostics run (the launcher's recording)? yes:
                                                APPLY, no: CLOSE */
};
/* The Pair Joy-Con screen's halves (NEBULA_SETTINGS_STATUS_JOYCON). */
enum {
    NEBULA_JOYCON_LEFT = 1,       /* a left half is connected */
    NEBULA_JOYCON_RIGHT = 2,      /* a right half is connected */
    NEBULA_JOYCON_LEFT_HELD = 4,  /* a left half's L is held */
    NEBULA_JOYCON_RIGHT_HELD = 8, /* a right half's R is held */
    NEBULA_JOYCON_PAIRED = 16,    /* a pair was just made: the screen closes in a moment */
    NEBULA_JOYCON_LEFT_PRESSES_SHIFT = 8,   /* 4 bits: the left halves' button presses, counted round */
    NEBULA_JOYCON_RIGHT_PRESSES_SHIFT = 12, /* 4 bits: the right halves' */
};

/* The hints name the keys or the buttons of what the player last used. A controller's buttons change, close, undo and
   apply as an Xbox controller's A, B, X and Y; a Switch controller's by their labels. */
enum {
    NEBULA_SETTINGS_INPUT_KEYBOARD = 0,
    NEBULA_SETTINGS_INPUT_XBOX,        /* Xbox, Steam and other controllers: A B X Y, LB RB */
    NEBULA_SETTINGS_INPUT_NINTENDO,    /* Switch Pro and Switch 2 Pro: A B X Y, L R */
    NEBULA_SETTINGS_INPUT_PLAYSTATION, /* cross circle square triangle, L1 R1 */
};

typedef struct NebulaSettingsRow {
    char label[64];
    char value[96];
    uint32_t flags; /* NEBULA_SETTINGS_ROW_* */
    uint16_t slider_steps;
    uint16_t slider_at;
} NebulaSettingsRow;

typedef struct NebulaSettingsView {
    uint32_t open;
    uint32_t page;
    uint32_t page_count;
    char pages[NEBULA_SETTINGS_PAGES][24];
    uint32_t controls_page; /* the page whose rows are bindings (its hints differ) */
    uint32_t input;         /* what the player last used, for the hints: NEBULA_SETTINGS_INPUT_* */
    uint32_t total_rows;    /* the page's rows; `rows` holds `row_count` of them from `first_row`, around the selection */
    uint32_t first_row;
    uint32_t row_count;
    uint32_t selected;      /* counted from the page's first row */
    uint32_t selected_button; /* the selected row's, when it has buttons */
    /* The MODS page's lists (off, on): the place in its list of each one's first row sent, and their lengths. */
    uint32_t mod_list_first[2];
    uint32_t mod_list_count[2];
    NebulaSettingsRow rows[NEBULA_SETTINGS_ROWS];
    char help[192];
    uint32_t status; /* NEBULA_SETTINGS_STATUS_* */
    int32_t status_number;
    char status_text[160];
} NebulaSettingsView;

/* The player's input to the settings menu (settings_input); the keyboard and controllers reach it through the host. */
enum {
    NEBULA_SETTINGS_OPEN = 1,
    NEBULA_SETTINGS_CLOSE,       /* as B or Escape: it asks first when changes wait */
    NEBULA_SETTINGS_SELECT_PAGE, /* arg: the page */
    NEBULA_SETTINGS_SELECT_ROW,  /* arg: the row */
    NEBULA_SETTINGS_CHANGE,      /* arg: -1 or 1, the selected row's value; -2 or 2 the same, going round at its ends */
    NEBULA_SETTINGS_APPLY,
    NEBULA_SETTINGS_REVERT,
    NEBULA_SETTINGS_PRESS,       /* arg: the selected row's button */
    NEBULA_SETTINGS_MOVE_MOD,    /* the selected mod dropped: arg >> 16 the list (1 off, 2 on), its low 16 bits the place */
    NEBULA_SETTINGS_SET_VALUE,   /* arg: the step the selected setting's slider is dragged to */
};

/* A Mii as the game draws it (the Mii library's character info): every part, colour, scale and position as a number,
   names UTF-16 (host byte order, 10 characters, unused ones 0), the create ID as a Mii file stores it. */
typedef struct NebulaMii {
    uint8_t sex, birth_month, birth_day, favorite_color, favorite, local_only, height, build;
    uint8_t face_type, face_color, face_texture;
    uint8_t hair_type, hair_color, hair_flip;
    uint8_t eye_type, eye_color, eye_scale, eye_rotate, eye_x, eye_y;
    uint8_t eyebrow_type, eyebrow_color, eyebrow_scale, eyebrow_rotate, eyebrow_x, eyebrow_y;
    uint8_t nose_type, nose_scale, nose_y;
    uint8_t mouth_type, mouth_color, mouth_scale, mouth_y;
    uint8_t mustache, beard_type, beard_color, beard_scale, beard_y;
    uint8_t glass_type, glass_color, glass_scale, glass_y;
    uint8_t mole_type, mole_scale, mole_x, mole_y;
    uint16_t name[10];
    uint16_t creator[10];
    uint8_t create_id[8];
} NebulaMii;

/* The channels a target pass's picture carries for the textures that sample it (target_end). */
typedef enum NebulaTargetChannels {
    NEBULA_TARGET_RGBA = 0,            /* colour and alpha */
    NEBULA_TARGET_INTENSITY_ALPHA = 1, /* intensity (the red channel) and alpha */
    NEBULA_TARGET_ALPHA = 2,           /* alpha alone, read in every channel */
    NEBULA_TARGET_RED = 3,             /* one colour channel, read in every channel */
    NEBULA_TARGET_GREEN = 4,
    NEBULA_TARGET_BLUE = 5,
    NEBULA_TARGET_RGB = 6              /* colour, alpha read as one */
} NebulaTargetChannels;

/* The channels a clear writes (frame_clear). */
typedef enum NebulaClearChannels {
    NEBULA_CLEAR_COLOR = 1u,
    NEBULA_CLEAR_ALPHA = 2u,
    NEBULA_CLEAR_DEPTH = 4u
} NebulaClearChannels;

/* How frame_read takes its picture. */
typedef enum NebulaCaptureFlags {
    NEBULA_CAPTURE_HALF = 2u,  /* half size: each picture texel averages 2x2 of the frame's */
    NEBULA_CAPTURE_DEPTH = 8u  /* the red channel holds the depth */
} NebulaCaptureFlags;

/* A point of the picture the game asks about (NebulaHostServices::screen_probe). */
#define NEBULA_SCREEN_PROBES_MAX 256u
typedef enum NebulaScreenProbeKind {
    NEBULA_SCREEN_PROBE_ALPHA = 0, /* value: the picture's alpha, 0-1 */
    NEBULA_SCREEN_PROBE_DEPTH = 1  /* value: the depth, 0 near to 1 far */
} NebulaScreenProbeKind;
typedef struct NebulaScreenProbe {
    float x, y;
    float user_x, user_y;
    float value;
} NebulaScreenProbe;

typedef struct NebulaHostServices {
    uint32_t version;
    uint32_t size;
    void* user;

    /* One line of the game's reports (OSReport) or the native layer's log, without a trailing newline. */
    void (*log)(void* user, const char* text);
    /* The game cannot go on (an OSPanic, a JUTException panic, a native-layer error): the host reports `text` with
       its own diagnostics and ends the process. Does not return. */
    void (*fatal)(void* user, const char* text);
    /* The module's own bookkeeping memory, outside its arenas. */
    void* (*heap_alloc)(void* user, size_t size);
    void* (*heap_realloc)(void* user, void* data, size_t size);
    void (*heap_free)(void* user, void* data);
    /* This thread's slots (NEBULA_THREAD_SLOTS of them, null at first) for the module's per-thread values. */
    void** (*thread_slots)(void* user);

    /* A scene bound (NEBULA_SCENE_*) after the records so far; the 3D scene's begin carries 16 floats: the
       camera's world-to-eye matrix (row-major 3x4), then the player's up in the world (against its gravity) and 1, or
       four zeros without a player. The others NULL. */
    void (*scene_marker)(void* user, uint32_t marker, const float* view);
    /* A JPA stripe particle whose two strip vertices are drawn next (galaxy/motion_signals.h): age << 16 |
       lifetime, strip 0 JPADrawStripe, 1-2 the passes of JPADrawStripeX. */
    void (*stripe_particle)(void* user, uint64_t particle, uint32_t age_lifetime, uint8_t strip);

    /* The game's files: the file or folder at `path` ("/StageData/EggStarGalaxy.arc": from the root, '/' at a name's start
       returning to it, "." and ".." steps, letters in either case, a name followed by '/' a folder): 0 when there is
       none. */
    int (*asset_find)(void* user, const char* path, NebulaAssetEntry* entry);
    /* The entry with file-table index `index`: 0 when there is none. */
    int (*asset_entry)(void* user, int32_t index, NebulaAssetEntry* entry);
    /* The `child`-th entry of the folder `folder`, in the table's order: 0 past the last. */
    int (*asset_child)(void* user, int32_t folder, uint32_t child, NebulaAssetEntry* entry);
    /* Reads `size` bytes at `offset` of a file into dst, before it returns: the bytes read, or -1. */
    int32_t (*asset_read)(void* user, int32_t file, uint64_t offset, uint32_t size, void* dst);
    /* The game's memory (NebulaArena): addresses to itself, null if there are none; memory for part of them, zeroed; given back
       with the addresses kept; all of it given back. */
    void* (*memory_reserve)(void* user, uint64_t size);
    int (*memory_commit)(void* user, void* start, uint64_t size);
    void (*memory_decommit)(void* user, void* start, uint64_t size);
    void (*memory_release)(void* user, void* start, uint64_t size);
    /* The game's threads (nebula_threads.h). A new thread sleeps until passed its turn; one that ends sleeps for good: game
       threads end only with the game. */
    NebulaHostThread* (*thread_create)(void* user, void (*entry)(void*), void* arg);
    NebulaHostThread* (*thread_adopt)(void* user);
    void (*thread_pass)(void* user, NebulaHostThread* to);
    void (*thread_wait)(void* user, NebulaHostThread* self);
    void (*thread_exit)(void* user, NebulaHostThread* self);
    void (*thread_cancel)(void* user, NebulaHostThread* thread);
    void (*lock)(void* user, int which);
    void (*unlock)(void* user, int which);
    /* Each thread's stack at its last wait, as offsets in the module holding `in_module`, to the log. */
    void (*threads_dump)(void* user, const void* in_module);
    /* A file's bytes in the mapped store, read-only, for the process's life, on their way into memory when this returns
       (call it where a wait for the drive does no harm); null if there is no such file. */
    const void* (*asset_map)(void* user, int32_t file);
    /* A file's bytes as the game's own memory: writes stay in this process. Null if it cannot be had (read the file then).
       Each ends with asset_unmap. */
    void* (*asset_map_private)(void* user, int32_t file);
    void (*asset_unmap)(void* user, void* data, uint64_t size);
    /* The game's id ("RMGE01", "RMGP01" or "RMGJ01", NUL-terminated), valid for the process's life: the release
       whose few differences the module follows (NebulaRegion.hpp) and whose folder holds its saves. */
    const char* (*game_id)(void* user);

    /* Blocks the game's main loop until its next frame is due: 60 a second, on the display's own refresh where the host
       has one; a frame that ran long starts at once. */
    void (*frame_wait)(void* user);
    /* The next frame has begun: whether the output is black, and the frames so far. The newest picture goes to the screen. */
    void (*frame_begun)(void* user, int black, uint32_t frames);

    /* Starts a save file operation without blocking; the request's name, buffer, length and status stay valid until
       its status leaves NEBULA_SAVE_PENDING (the host writes it from its own thread). */
    void (*save_request)(void* user, const NebulaSaveRequest* request);

    /* A setting of the player's system (NEBULA_SETTING_*). */
    uint32_t (*system_setting)(void* user, uint32_t item);

    /* Input. `device` gets player `player`'s controller; with `max` > 0 `samples` gets its samples since the last
       read, oldest first, one taken now and one every 10 ms before it (at most `max`, the newest kept): the count. */
    uint32_t (*input_read)(void* user, uint32_t player, NebulaInputDevice* device, NebulaInputSample* samples,
                           uint32_t max);
    void (*input_control)(void* user, uint32_t player, uint32_t what, uint32_t value);
    /* Player `player`'s remote speaker: `count` 16-bit samples at 6 kHz, as the game mixes them. */
    void (*speaker_samples)(void* user, uint32_t player, const int16_t* samples, uint32_t count);

    /* Audio, on the game's audio thread. voices_setup hands over the voices and the four effect lines (they stay the
       game's; the mixer reads and advances them in voices_mix).
       voices_mix mixes one subframe at `gain`; voices_frame ends a frame: the subframes mixed since the last call, with
       the movie's sound for them, go to the output. */
    void (*voices_setup)(void* user, NebulaVoice* voices, uint32_t count, const NebulaEffectLine* effects);
    void (*voices_mix)(void* user, float gain);
    void (*voices_frame)(void* user);
    /* The movie player's sound, on the game's threads: movie_audio queues one frame's audio record (its stereo sample count,
       u32, then the samples, PCM16, left and right in turn, of the track played) for the mixer; 0 when the queue is full (null asks only that).
       sound at 32 kHz; 0 when 20 frames already wait (nothing taken). A null record only asks for room.
       movie_audio_control: NEBULA_MOVIE_AUDIO_*. */
    int (*movie_audio)(void* user, const void* record, uint32_t size);
    void (*movie_audio_control)(void* user, uint32_t what, int32_t value, int32_t milliseconds);
    void (*sound_clock)(void* user, uint32_t what, uint32_t value);

    /* The HOME Menu (NEBULA_HBM_*). */
    int (*hbm)(void* user, uint32_t what, uint32_t value);

    /* One movie frame's video (`size` bytes, one JPEG whose entropy data is not byte-stuffed, so only the size bounds
       it) into its Y, U and V planes (`width` x `height` for Y, half each way for U and V, rows of 8-bit texels from the
       top) in game memory, which must hold the result before the renderer reads them; 0 on success. */
    int32_t (*movie_video_decode)(void* user, const void* frame, uint32_t size, uint32_t width, uint32_t height, void* y, void* u, void* v);

    /* The game rewrote [address, address + size): an image handed over there must be handed over again. */
    void (*memory_rewritten)(void* user, const void* address, uint32_t size);

    /* Game state the host's controls follow (NEBULA_CONTEXT_*), reported when it changes. */
    void (*game_context)(void* user, uint32_t what, int32_t value);

    /* The game closes, or starts again from its boot, with pending saves written. Do not return. */
    void (*app_exit)(void* user);
    void (*app_restart)(void* user);

    /* Tests: once per game scene frame (before its 3D), what a hidden run asks of the game (GALAXY_TEST_*): returns 1
       with `action` filled, or 0. */
    int (*test_scene_frame)(void* user, NebulaTestAction* action);

    /* The output's shape latched at this frame, after frame_begun: width << 16 | height (only the ratio counts), or 0
       for 16:9. */
    uint32_t (*display_extent)(void* user);
    /* A Nebula option (NEBULA_OPTION_*), read when the game needs it. */
    uint32_t (*option)(void* user, uint32_t what);
    /* A light of the game (NEBULA_LIGHT_*): `id` names it across frames (its object's address; 0: none), `values` as
       the kind lists. Called on the simulation thread. */
    void (*game_light)(void* user, uint32_t kind, uint64_t id, const float* values);
    /* The stage being played (MR::getCurrentStageName), before each 3D scene's begin marker. */
    void (*game_stage)(void* user, const char* name);
    /* A movie (MovieStream): its disc path and NEBULA_MOVIE_OPENING when it opens, each frame it draws
       (the frame within the movie, its frame rate and its Y, U and V planes, `width` x
       `height` for Y and half each way for U and V), NEBULA_MOVIE_NONE when it closes. The host may show a replacement
       video's frame of the same time in place of the planes. Called on the simulation thread. */
    void (*movie_frame)(void* user, const char* name, int32_t frame, float frame_rate, const void* y, const void* u,
                        const void* v, uint32_t width, uint32_t height);
    /* What the player is doing, for Discord: the galaxy and the star (UTF-16, the game's own text in the played
       language; either may be NULL) and the Power Stars collected (-1: not in a file). Reported when it changes. */
    void (*presence)(void* user, const uint16_t* galaxy, const uint16_t* star, int32_t power_stars);
    /* An achievement the save data meets (galaxy::achievements' id): 1 when it was not unlocked before, with its
       English name (UTF-16, NUL-terminated, at most `capacity` units; `name` may be NULL); else 0. */
    int (*achievement)(void* user, const char* id, uint16_t* name, uint32_t capacity);
    /* The day an achievement was unlocked in the player's time zone (year * 10000 + month * 100 + day); 0 when it is
       locked; 1 when it is unlocked on a day that cannot be read. */
    int32_t (*achievement_date)(void* user, const char* id);
    /* Nebula's all-time counters across save files (galaxy::stats: "jumps", "spins", "deaths"...): `delta` added, or
       the count. */
    void (*stat_add)(void* user, const char* name, uint64_t delta);
    uint64_t (*stat_get)(void* user, const char* name);
    /* The settings menu as it stands (NebulaSettingsView); while the game draws it, the host draws none of its own. */
    void (*settings_view)(void* user, NebulaSettingsView* view);
    /* The pointer's input to the settings menu (NEBULA_SETTINGS_*). */
    void (*settings_input)(void* user, uint32_t action, int32_t arg);
    /* 1 while a Nebula screen with small text shows (the settings menu, the achievement board): the 2D layer at the
       window's resolution whatever the HUD's; 0 after. */
    void (*sharp_2d)(void* user, uint32_t on);
    /* The button icons as the player's controller and the setting have them now (NebulaButtonIcons). */
    void (*button_icons)(void* user, NebulaButtonIcons* icons);
    /* GALAXY_EXPORT_ART=<folder>: Nebula's own pictures written there (NebulaArtExport). NULL asks whether an export
       is on: 1, else 0 (the game then paints nothing for it). */
    uint32_t (*art_export)(void* user, const NebulaArtExport* art);
    /* The lines of the 2D layer as last drawn (the window's while sharp_2d is on); 0 before the first frame. */
    uint32_t (*layer_2d_lines)(void* user);
    /* The mouse wheel's turn since the last call, in 120ths of a notch (positive away from the player), while no screen
       of the host's takes it: the achievement board scrolls by it. */
    int32_t (*pointer_wheel)(void* user);
    /* The settings' galaxy select (ADVANCED page): its galaxy `index` (the game's stage name in `id`, `capacity` bytes,
       and its enterable stars in `stars`): 1, or 0 past the last. */
    int (*galaxy_select_stage)(void* user, uint32_t index, char* id, uint32_t capacity, uint32_t* stars);
    /* 1 while the galaxy select waits for its names (galaxy_select_name), else 0. */
    int (*galaxy_select_names_wanted)(void* user);
    /* A name for the galaxy select in the game's own words in the played language (UTF-16, NUL-terminated): galaxy
       `index`'s (`star` 0) or its star's. */
    void (*galaxy_select_name)(void* user, uint32_t index, uint32_t star, const uint16_t* name);
    /* A model the game loaded (J3D), for its curved surfaces (galaxy/gx/curved_surfaces.h): its position array's
       identity (`key`, as shape_draw names it), its positions and normals (3 floats each), and its triangles, three
       corners each, a corner a position index then a normal index. */
    void (*curved_surface)(void* user, uint64_t key, const float* positions, uint32_t position_count,
                           const float* normals, uint32_t normal_count, const uint16_t* corners,
                           uint32_t triangle_count);
    /* A model's vertex array as values (NebulaShapeArray), registered or replaced; the host copies it. */
    void (*shape_array)(void* user, const struct NebulaShapeArray* array);
    /* A model's shape group (J3DShapeDraw) as typed geometry (NebulaShapeGeometry); the host copies it. */
    void (*shape_geometry)(void* user, const struct NebulaShapeGeometry* geometry);
    /* The game freed the memory from `start` up to `end`: arrays and groups keyed inside it are gone, at this place in the
       records. */
    void (*shape_forget)(void* user, uint64_t start, uint64_t end);
    /* Draws shape group `shape` (NebulaShapeGeometry::key) under the drawing state the game has set so far, its corners reading the position,
       normal and colour 0 arrays named by `position`, `normal` and `color` (shape_array keys; the geometry's own for
       the rest) and the position and normal matrices among `matrices` (`matrix_count` of them, NebulaDrawMatrix),
       after their texture matrices, `texgens` and, when not null, `current` take effect. Records (shapes, materials,
       state, immediates, images, captures, markers) run in the order the game makes them. Shape 0 draws nothing:
       only the texture matrices, current matrices and texgens take effect (state the game set before a material
       record, kept in order with it). */
    void (*shape_draw)(void* user, uint64_t shape, uint64_t position, uint64_t normal,
                       uint64_t color, const NebulaDrawMatrix* matrices, uint32_t matrix_count,
                       const NebulaShapeCurrent* current, const NebulaShapeTexGen* texgens, uint32_t texgen_count, const NebulaPassState* pass);
    /* The projection the draws after this use: `terms` the six of a perspective or orthographic matrix. */
    void (*view_projection)(void* user, int orthographic, const float terms[6]);
    /* The viewport: left, top, width, height (the game's units), near and far depth (0-1). */
    void (*view_viewport)(void* user, const float viewport[6]);
    void (*view_scissor)(void* user, float left, float top, float width, float height);
    /* Light `index` (0-7). */
    void (*light)(void* user, uint32_t index, const NebulaLight* light);
    /* Whether the frame keeps alpha. */
    void (*frame_alpha)(void* user, int keep);
    /* Coordinate `coord` of the draws after this is texture coordinate `from`, through matrix slot `matrix_slot` (0-9
       position matrices, 10-19 texture matrices, 20 identity). */
    void (*texture_coordinate)(void* user, uint32_t coord, uint32_t from, uint32_t matrix_slot);
    /* Vertices drawn as the game sends them (NebulaImmediate). */
    void (*immediate)(void* user, const NebulaImmediate* draw);
    /* A pass into a texture target the renderer owns (E-07): the records until target_end draw into it, not the
       frame; its top left `width` x `height` (in NEBULA_FRAME_UNITS) cleared to `clear_rgba`
       (R << 24 | G << 16 | B << 8 | A) and depth `clear_z` (24 bits). */
    void (*target_begin)(void* user, uint64_t key, uint32_t width, uint32_t height, uint32_t clear_rgba,
                         uint32_t clear_z);
    /* Ends the pass: its picture, at full precision, becomes the texture `key` names (an image or texture load
       naming `key` reads it, whatever format it names), carrying `channels` (NebulaTargetChannels). */
    void (*target_end)(void* user, uint64_t key, uint32_t channels);
    /* Clears channels of the open target pass (`channels`: 1 colour, 2 alpha, 4 depth) to `clear_rgba` / `clear_z`. */
    void (*target_clear)(void* user, uint32_t clear_rgba, uint32_t clear_z, uint32_t channels);
    /* The whole frame drawn so far becomes texture `key` as target_end's pictures do, carrying `channels`
       (NebulaTargetChannels); `flags`: NebulaCaptureFlags. */
    void (*frame_read)(void* user, uint64_t key, uint32_t channels, uint32_t flags);
    /* An image's bytes as the game has them (NebulaTextureImage): the renderer takes a copy and draws the image from
       it from here on. Sent when the image is loaded or rewritten. */
    void (*texture_image)(void* user, const NebulaTextureImage* image);
    /* The Mii in a slot of the Mii database (0-99): 1 when there is one (`mii` filled when not NULL), 0 for
       an empty slot. */
    int32_t (*mii_record)(void* user, uint32_t slot, NebulaMii* mii);
    /* What the picture holds at `count` points where the drawing has reached (at most NEBULA_SCREEN_PROBES_MAX; `what` a
       NebulaScreenProbeKind). x and y the frame's coordinates, negative: no point, its value 0; user_x and user_y come
       back with the answer. */
    void (*screen_probe)(void* user, uint32_t channel, uint32_t what, const NebulaScreenProbe* probes, uint32_t count);
    /* `channel`'s latest finished probes with their values, at most `capacity`; how many (0 before any has finished).
       Never waits for the GPU. */
    uint32_t (*screen_probe_results)(void* user, uint32_t channel, NebulaScreenProbe* probes, uint32_t capacity);
    /* Clears channels (NebulaClearChannels) of the frame's rectangle at `left`, `top` (`width` x `height`, the
       viewport's units) to `clear_rgba` (R << 24 | G << 16 | B << 8 | A) and depth `clear_z` (24 bits, far 0xFFFFFF). */
    void (*frame_clear)(void* user, uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint32_t clear_rgba,
                        uint32_t clear_z, uint32_t channels);
    /* The frame drawn so far is the picture shown. */
    void (*frame_show)(void* user);
    /* The game's frame is complete (its picture was shown earlier): the renderer takes every record made before. */
    void (*frame_end)(void* user);
    /* A host marker (galaxy/motion_signals.h: a draw identity, a bloom phase, a clear quad). A draw identity names its
       objects by address. */
    void (*marker)(void* user, uint32_t kind, uint32_t aux, uint64_t a, uint64_t b);
    /* Nanoseconds on a steady clock from an arbitrary start (never 0): the game's durations and pacing. */
    uint64_t (*clock_ns)(void* user);
    /* The local date and time now: nanoseconds from 2000-01-01 00:00 local time (the game's clock counts from
       there; the game's saves store times in its unit). */
    int64_t (*calendar_ns)(void* user);
    /* A native material for the draws that follow (NebulaNativeMaterial), through state changes (which set its
       fixed-function state and textures), until the next material or one whose shader is NEBULA_SHADER_NONE. */
    void (*native_material)(void* user, const NebulaNativeMaterial* material);
    /* Deterministic test mode only (NULL otherwise; GALAXY_TEST_DETERMINISTIC): the game's time is its frames. No game
       thread can run: the host takes one frame's audio ticks and clock and returns, so frames follow the game's own pace and every run of a build is the same. The game's clock then
       advances one retrace per call and its alarms fire when that clock passes them. */
    void (*test_frame_step)(void* user);
} NebulaHostServices;


enum {
    NEBULA_MOVIE_NONE = -1,
    NEBULA_MOVIE_OPENING = -2,
};

enum {
    NEBULA_CONTEXT_STAR_POINTER_MODE = 1, /* StarPointerOnOffController's mode (-1 none): menus and play */
    NEBULA_CONTEXT_TILT_SECTION,          /* 0 none, 1 Mario on a Star Ball, 2 on a Ray: tilt steers */
    NEBULA_CONTEXT_FRAME_GENERATION,      /* each frame: 1 while the game shows play (no menu, window or full-screen
                                             page): DLSS Frame Generation may add frames (NebulaFrameGen) */
    NEBULA_CONTEXT_CAMERA_WATER,          /* each frame: the camera's depth under water in units, at least 1 (0: out
                                             of water; MR::isCameraInWater): ray tracing's water medium */
};

/* Returns NEBULA_GAME_API_VERSION, or 0 if the host's services are too old. */
NEBULA_GAME_API uint32_t nebula_game_attach(const NebulaHostServices* host);
/* Runs the game on the calling thread, the simulation thread. Does not return. */
NEBULA_GAME_API void nebula_game_run(void);
/* A file read finished: `result` is the bytes read, or -1 on a read error. */
/* The audio clock, while started (NEBULA_AUDIO_START): the game mixes its next frame (voices_mix, voices_frame). */
NEBULA_GAME_API void nebula_game_sound_frame(void);
/* Mii import. prepare: makes a Mii (a 74-byte record as a Mii file stores it) acceptable where the game would
   refuse a detail (birthday, special Mii not kept local, favourite colour, no name), then 1 when the game takes it as
   its database's; pure, any thread. install: places it in slot `slot` (0-99) of the loaded database, free or holding
   the same Mii (its create ID); 0 when the database is not loaded or another Mii holds the slot. On the game thread
   (the host's frame tick). */
/* Whether the game takes a Mii into its database (its parts and values in range); the host keeps only these. */
NEBULA_GAME_API uint32_t nebula_game_mii_valid(const NebulaMii* mii);
/* The host's Mii database changed (Miis imported while the game runs): the game reads its slots again. */
NEBULA_GAME_API void nebula_game_mii_changed(void);
#ifdef __cplusplus
}
#endif

#endif
