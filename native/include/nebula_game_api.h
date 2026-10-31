/* The game module's interface to Nebula's host (docs/NATIVE_PORT.md, "Module layout"): plain C, so the game module
   (built against the game's own C++ library) and the host (the host's C++ library) share nothing else. The host fills
   NebulaHostServices and calls nebula_game_attach, then runs the game on its simulation thread with nebula_game_run;
   events from host threads (the renderer's PE tokens and stream consumption, the video retrace, audio, input, disc)
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

#define NEBULA_GAME_API_VERSION 59u

/* One of JAudio's 64 channels (JASDsp::TChannel, 0x180 bytes, host byte order): the game sets its source, pitch,
   volumes and filters; the host's mixer plays it and advances its playback state. */
typedef struct NebulaJAudioBusSend {
    uint16_t connect;  /* the mix bus fed (0: none) */
    int16_t target;    /* volume ramped to over a subframe */
    int16_t current;   /* volume now */
    uint16_t delay;    /* samples of delay: target in the high byte, now in the low */
} NebulaJAudioBusSend;

typedef struct NebulaJAudioChannel {
    uint16_t active;                  /* 0x000 */
    uint16_t finished;                /* 0x002 */
    uint16_t pitch;                   /* 0x004 source samples per output sample, 4.12 */
    uint16_t reserved_006;
    uint16_t starting;                /* 0x008 set at play start: the next render restarts the source */
    uint16_t loop_pending;            /* 0x00A PCM: the position reached the end (loop or finish next) */
    uint16_t paused;                  /* 0x00C */
    uint16_t delay_history;           /* 0x00E samples of the previous subframe the bus delays read */
    NebulaJAudioBusSend buses[6];     /* 0x010 */
    uint8_t reserved_040[0x10];
    uint16_t auto_pan;                /* 0x050 auto mixer: pan (high byte), front/surround balance (low byte) */
    int16_t auto_send;                /* 0x052 effect send */
    int16_t auto_volume;              /* 0x054 volume now */
    int16_t auto_target;              /* 0x056 volume ramped to */
    uint16_t auto_mixer;              /* 0x058 nonzero: the auto mixer plays the channel, else its bus sends */
    uint8_t reserved_05A[6];
    uint16_t phase;                   /* 0x060 position between source samples (12 bits) */
    uint16_t reserved_062;
    uint16_t leftover;                /* 0x064 decoded samples of `block` still to play (the game starts it at the
                                         format's block size) */
    int16_t last_sample;              /* 0x066 the last output, held while paused */
    uint32_t restart;                 /* 0x068 PCM: the sample its source restarts at */
    uint32_t reserved_06C;
    uint32_t address;                 /* 0x070 next block or sample, from the wave data's base */
    uint32_t left;                    /* 0x074 samples before the end (or loop end) */
    int16_t resample_history[4];      /* 0x078 the previous subframe's last source samples */
    int16_t fir_history[20];          /* 0x080 */
    int16_t iir_state[4];             /* 0x0A8 x1, x2, y2, y3 */
    int16_t block[16];                /* 0x0B0 the decoded ADPCM block; [14] and [15] the predictor's history */
    int16_t delay_samples[16];        /* 0x0D0 the previous subframe's last samples, newest at [15] */
    uint8_t reserved_0F0[6];
    uint16_t play_count;              /* 0x0F6 */
    uint8_t reserved_0F8[8];
    uint16_t format;                  /* 0x100 ADPCM bytes per block (9, 5), 8 PCM8, 16 PCM16, else an oscillator */
    uint16_t loop;                    /* 0x102 */
    int16_t loop_history1;            /* 0x104 ADPCM predictor history at the loop start */
    int16_t loop_history2;            /* 0x106 */
    uint16_t filter_mode;             /* 0x108 FIR taps (bits 0-4), IIR on (bit 5) */
    uint16_t forced_stop;             /* 0x10A */
    uint32_t reserved_10C;
    uint32_t loop_start;              /* 0x110 */
    uint32_t end;                     /* 0x114 */
    uint32_t wave;                    /* 0x118 the wave data's start */
    uint32_t reserved_11C;
    int16_t fir_coefficients[8];      /* 0x120 */
    uint8_t reserved_130[0x18];
    int16_t iir_coefficients[8];      /* 0x148 (four used) */
    uint8_t reserved_158[0x28];
} NebulaJAudioChannel;

/* One of JAudio's four FX lines (JASDsp::FxBuf): a delay of `frames` subframes in `buffer` (frames * 80 samples)
   fed by its input bus, an 8-tap FIR before (flags bit 0) or after (bit 1) the sends to buses `dest_a` and `dest_b`. */
typedef struct NebulaJAudioFxLine {
    uint16_t flags;
    uint16_t frames;
    int16_t* buffer;
    uint16_t dest_a;
    int16_t volume_a;
    uint16_t dest_b;
    int16_t volume_b;
    int16_t coefficients[8];
} NebulaJAudioFxLine;

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

/* One drawing-state change the game makes outside a material (state): `kind` names the GX function whose
   arguments `a` holds in its order (enumerations and bytes), `size` its sizes, `f` its float arguments and `image`
   a texture's identity (the game's address of its image). */
enum {
    NEBULA_STATE_BLEND_ETC = 1,     /* type, source, destination, logic op, colour update, alpha update, dither */
    NEBULA_STATE_BLEND,             /* type, source, destination, logic op (updates and dither kept) */
    NEBULA_STATE_Z_MODE,            /* compare, function, update */
    NEBULA_STATE_ALPHA_COMPARE,     /* comparison 0, reference 0, op, comparison 1, reference 1 */
    NEBULA_STATE_DST_ALPHA,         /* enable, alpha */
    NEBULA_STATE_CULL,              /* mode */
    NEBULA_STATE_GEN_MODE,          /* texgens, channels, TEV stages */
    NEBULA_STATE_GEN_MODE2,         /* texgens, channels, TEV stages, indirect stages, cull mode */
    NEBULA_STATE_CHANNEL,           /* channel, enable, ambient source, material source, light mask, diffuse, attenuation */
    NEBULA_STATE_TEV_COLOR_CALC,    /* stage, a, b, c, d, op, bias, scale, clamp, output */
    NEBULA_STATE_TEV_ALPHA_CALC,    /* stage, a, b, c, d, op, bias, scale, clamp, output, raster swap, texture swap */
    NEBULA_STATE_TEV_DIRECT,        /* stage */
    NEBULA_STATE_TEV_ORDER,         /* even stage, coord 0, map 0, channel 0, coord 1, map 1, channel 1 */
    NEBULA_STATE_TEV_COLOR,         /* register, r, g, b, a */
    NEBULA_STATE_MATERIAL_COLOR,    /* channel, r, g, b, a, GXSetChan*Color's form (1: a colour channel sets only
                                       r, g, b, an alpha channel only a; 0: GDSetChan*Color's, the whole word) */
    NEBULA_STATE_AMBIENT_COLOR,     /* as NEBULA_STATE_MATERIAL_COLOR */
    NEBULA_STATE_TEX_COORD_GEN,     /* coord, type, source, normalize, post-transform matrix id, GXSetTexCoordGen2's
                                       form (1: also the coordinate's matrix index, a[6]; 0: GDSetTexCoordGen's) */
    NEBULA_STATE_TEX_LOOKUP,        /* map, wrap s, wrap t, min filter, mag filter, bias clamp, edge LOD, anisotropy;
                                       f: min LOD, max LOD, LOD bias */
    NEBULA_STATE_TEX_IMAGE_ATTR,    /* map, format; size: width, height */
    NEBULA_STATE_TEX_IMAGE,         /* map; a[1] 1: a texture object's load (coordinate scales follow it); image */
    NEBULA_STATE_FOG,               /* type, r, g, b, a, GXSetFog's form (1: a projected type writes its second and
                                       third words with register id 0, as GXSetFog does; 0: GDSetFog's); f: start,
                                       end, near, far */
    /* Changes of some fields of a register, the others as they are (GX's partial setters): */
    NEBULA_STATE_TEV_COLOR_IN,      /* stage, a, b, c, d */
    NEBULA_STATE_TEV_ALPHA_IN,      /* stage, a, b, c, d */
    NEBULA_STATE_TEV_COLOR_OP,      /* stage, op, bias, scale, clamp, output */
    NEBULA_STATE_TEV_ALPHA_OP,      /* stage, op, bias, scale, clamp, output */
    NEBULA_STATE_TEV_KCOLOR_SEL,    /* stage, selection */
    NEBULA_STATE_TEV_KALPHA_SEL,    /* stage, selection */
    NEBULA_STATE_TEV_SWAP_MODE,     /* stage, raster swap, texture swap */
    NEBULA_STATE_TEV_STAGE_ORDER,   /* stage, coord, map, channel */
    NEBULA_STATE_COLOR_UPDATE,      /* enable */
    NEBULA_STATE_ALPHA_UPDATE,      /* enable */
    NEBULA_STATE_DITHER,            /* enable */
    NEBULA_STATE_PROJECTION,        /* type (0 perspective, 1 orthographic); f: GXSetProjectionv's six terms */
    NEBULA_STATE_VIEWPORT,          /* f: x scale, y scale, z scale, x origin, y origin, z origin (GX's terms) */
    NEBULA_STATE_SCISSOR,           /* f: left, top, width, height (pixels) */
    NEBULA_STATE_SCISSOR_OFFSET,    /* f: x, y (pixels) */
    NEBULA_STATE_TEV_KCOLOR,        /* konst register, r, g, b, a */
    NEBULA_STATE_TEV_COLOR_S10,     /* register; size: r, g; f: b, a (signed 10-bit values) */
    NEBULA_STATE_CLIP_MODE,         /* mode (GXClipMode) */
    NEBULA_STATE_IND_TEX_MATRIX,    /* matrix id (GXIndTexMtxID), scale exponent (s8); f: offset[0][0], [1][0],
                                       [0][1], [1][1], [0][2], [1][2] */
    NEBULA_STATE_TEV_SWAP_TABLE,    /* table (GXTevSwapSel), red, green, blue, alpha (GXTevColorChan) */
    NEBULA_STATE_NUM_TEX_GENS,      /* count */
    NEBULA_STATE_NUM_CHANS,         /* count */
    NEBULA_STATE_NUM_TEV_STAGES,    /* count */
    NEBULA_STATE_NUM_IND_STAGES,    /* count */
    NEBULA_STATE_CO_PLANAR,         /* enable */
    NEBULA_STATE_Z_COMP_LOC,        /* before texturing */
    NEBULA_STATE_CURRENT_MATRIX,    /* position/normal matrix id (GXSetCurrentMtx) */
    NEBULA_STATE_TEV_INDIRECT,      /* TEV stage, indirect stage, format, bias, matrix, wrap s, wrap t, add previous,
                                       unmodified LOD, alpha (GXSetTevIndirect's arguments) */
    NEBULA_STATE_IND_TEX_SCALE,     /* indirect stage, scale s, scale t */
    NEBULA_STATE_LINE_WIDTH,        /* width, texture offset */
    NEBULA_STATE_POINT_SIZE,        /* size, texture offset */
    NEBULA_STATE_LIGHT_ATTENUATION, /* light (0-7), r, g, b, a; f: angle attenuation a0-a2, distance k0-k2 */
    NEBULA_STATE_LIGHT_POSITION,    /* light (0-7); f: position x, y, z, direction x, y, z */
    NEBULA_STATE_PIXEL_FORMAT,      /* changed (the format and depth format are new), pixel type (the hardware's:
                                       GXSetPixelFmt's table), depth format, antialiasing, YUV (a Y8/U8/V8 format),
                                       YUV component */
    NEBULA_STATE_INVALIDATE_TEXTURES, /* none: textures in memory changed (GXInvalidateTexAll) */
    NEBULA_STATE_Z_TEXTURE,         /* operation (GXZTexOp), format (GXTexFmt), bias (24 bits, a[2..4] low first) */
    NEBULA_STATE_COPY_CLEAR,        /* r, g, b, a, depth (24 bits, a[4..6] low first) */
    NEBULA_STATE_COPY_SAMPLES,      /* antialiasing, then (when set) the 12 sample points' x, y (4 bits each, two a
                                       byte from a[1], x low) */
    NEBULA_STATE_COPY_VFILTER,      /* vertical filter on, its 7 coefficients */
    NEBULA_STATE_COPY_YSCALE,       /* size[0]: the display copy's vertical scale (256 / scale, 9 bits) */
    NEBULA_STATE_COPY,              /* an EFB copy, run with the current state: display (1) or texture (0), clear,
                                       texture format (GXTexFmt), mipmap, clamp top, clamp bottom, interlaced (GXCopyMode),
                                       vertical scale on, gamma (GXGamma); f: source left, top, width, height; size: the
                                       destination's width and height (a display copy: its width); image: destination */
    NEBULA_STATE_FOG_RANGE,         /* enable; size[0]: centre (screen x); a[1]: with its table, then f: none and the
                                       table's ten 12-bit entries in NEBULA_STATE_FOG_RANGE_TABLE ops before it */
    NEBULA_STATE_FOG_RANGE_TABLE,   /* pair index (0-4); size: the pair's two entries */
    NEBULA_STATE_TEX_TLUT,          /* map, palette format (GXTlutFmt), palette place (GX's 20 TLUT names) */
    NEBULA_STATE_TLUT_LOAD,         /* palette place; image: the palette */
    NEBULA_STATE_IND_TEX_ORDER,     /* indirect stage, texture coordinate, map (GXSetIndTexOrder's) */
    NEBULA_STATE_TEX_OFFSETS,       /* texture coordinate, lines offset it, points offset it */
};

typedef struct NebulaStateOp {
    uint8_t kind;
    uint8_t a[13];
    uint16_t size[2];
    float f[6];
    uint64_t image;
} NebulaStateOp;

/* An image (texture_image): its identity (the game's address of its bytes), format (GXTexFmt), size, mipmap levels
   and its bytes (`size` of them, in the format's layout). */
typedef struct NebulaTextureImage {
    const void* bytes;
    uint32_t size;
    uint16_t width, height;
    uint8_t format, levels;
} NebulaTextureImage;

/* Vertices the game sends as it draws (immediate): `count` of them grouped as `primitive` (GXBegin's: quads,
   triangles, strips, fans, lines, line strips, points), each with the values `attributes` names (NEBULA_SHAPE_NORMAL,
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
    uint32_t reserved3;
} NebulaImmediate;

/* A texture coordinate a shape draw generates differently from its material (shape_draw): GXSetTexCoordGen's values
   (type, source, normalize, post-transform matrix id). */
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

/* A J3D material in two parts, every value as GX's functions take it, no register words. Its program
   (NebulaMaterialProgram: what its shaders and fixed-function state follow from) is the same for every draw of the
   display list it belongs to: the game sends it when it builds or patches the list (material_program in
   NebulaHostServices), and the host compiles its pipelines then. Each draw names the program and carries the
   material's values (NebulaMaterialValues: colours, matrices, lights, textures, fog range; material). Colours are
   RGBA bytes; TEV colours signed 10-bit values in int16_t. */
enum {
    NEBULA_MATERIAL_LOADS_FOG = 1u << 0,
    NEBULA_MATERIAL_LOADS_DITHER = 1u << 1,
    NEBULA_MATERIAL_LOADS_TEV_COLOR = 1u << 2,
    NEBULA_MATERIAL_LOADS_KONST_COLOR = 1u << 3,
    NEBULA_MATERIAL_LOADS_KONST_SELECT = 1u << 4,
    NEBULA_MATERIAL_LOADS_SWAP_TABLE = 1u << 5,
    NEBULA_MATERIAL_LOADS_AMBIENT = 1u << 6,
    NEBULA_MATERIAL_LOADS_LIGHTS = 1u << 7,
    NEBULA_MATERIAL_LOADS_TEV = 1u << 8,       /* textures, stage orders, stages, indirect stages */
    NEBULA_MATERIAL_LOADS_CHANNELS = 1u << 9,  /* channel controls and material colours */
    NEBULA_MATERIAL_LOADS_INDIRECT = 1u << 10, /* indirect matrices, scales and orders */
};

typedef struct NebulaMaterialTevStage {
    uint8_t tex_coord, tex_map, channel;          /* GXSetTevOrder */
    uint8_t color_in[4], color_op, color_bias, color_scale, color_clamp, color_reg;
    uint8_t alpha_in[4], alpha_op, alpha_bias, alpha_scale, alpha_clamp, alpha_reg;
    uint8_t ras_swap, tex_swap, konst_color, konst_alpha;
    uint8_t ind_stage, ind_format, ind_bias, ind_matrix, ind_wrap_s, ind_wrap_t, ind_prev, ind_lod, ind_alpha;
} NebulaMaterialTevStage;

typedef struct NebulaMaterialTexture {
    uint64_t image;               /* the texture's identity: the game's address of its image (0: no texture) */
    uint16_t width, height;
    uint8_t format, wrap_s, wrap_t, min_filter, mag_filter, mipmaps, edge_lod, anisotropy;
    float min_lod, max_lod, lod_bias;
    uint64_t palette;             /* colour-indexed formats: the palette's identity, else 0 */
    uint16_t palette_entries;
    uint8_t palette_format, bias_clamp;
} NebulaMaterialTexture;

typedef struct NebulaMaterialLight {
    float position[3], direction[3];
    float angle[3], distance[3];  /* attenuation coefficients */
    uint8_t color[4];
} NebulaMaterialLight;

typedef struct NebulaMaterialProgram {
    uint8_t tev_stages, tex_gens, channels, ind_stages, cull;
    uint8_t dither, z_compare_location;
    uint8_t origin; /* where the game captured it: 1 a packet's list, 2 a differed list, 3 a shared list, 0 at the draw */
    uint16_t loads; /* NEBULA_MATERIAL_LOADS_*: the parts the material sets (the others keep what was set before) */
    uint8_t reserved[2];
    /* 0: the material as its list sets it. Else an instance's differed list: only the parts these J3D diff flags name
       (J3DDiffFlag_*: counts of textures, stages, texgens and lights, TEV and konst registers, fog, blend, ...). */
    uint32_t diff_flags;
    NebulaMaterialTevStage tev[16];
    uint8_t swap_table[4][4];
    struct { uint8_t coord, map, scale_s, scale_t; } ind_order[4];
    struct { uint8_t type, source, matrix, normalize, post_matrix, reserved[3]; } tex_gen[8];
    struct { uint8_t enable, material_source, ambient_source, light_mask, diffuse, attenuation, reserved[2]; } channel[4];
    struct { uint8_t type, adjust_enable; uint16_t adjust_center; uint16_t adjust[10]; } fog;
    struct { uint8_t comp0, op, comp1, reserved; } alpha_compare;
    struct { uint8_t type, source, destination, logic_op; } blend;
    struct { uint8_t enable, func, update, reserved; } z_mode;
    /* Texgen i's texture matrix (J3DTexMtx), when the material has one: `id` GX's matrix id (a post-transform one from
       64), `rows` 2 or 3, `listed` whether the material's list loads it (its texgen's matrix is not the identity; a
       differed list loads every one). Its values are NebulaMaterialValues::texture_matrix[i]. */
    struct { uint8_t present, listed, id, rows; } texture_matrix[8];
} NebulaMaterialProgram;

typedef struct NebulaMaterialValues {
    int16_t tev_color[4][4];
    uint8_t konst_color[4][4];
    uint8_t material_color[2][4], ambient_color[2][4];
    uint8_t light_mask; /* the lights below that are set */
    uint8_t alpha_ref[2];
    uint8_t reserved;
    struct { float m[2][3]; int8_t scale; uint8_t reserved[3]; } ind_matrix[3];
    struct { uint16_t scale_s, scale_t; uint8_t bias_s, bias_t, wrap_s, wrap_t; } tex_coord_scale[8];
    NebulaMaterialTexture texture[8];
    NebulaMaterialLight light[8];
    struct { float start, end, near_z, far_z; uint8_t color[4]; } fog;
    float texture_matrix[8][12];
} NebulaMaterialValues;

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

/* A file or folder of the game's disc (the game partition's file table, or the mods' disc built over it). */
typedef struct NebulaDiscEntry {
    int32_t index;     /* in the file table; 0 is the root folder */
    uint32_t length;   /* a file's length in bytes; 0 for a folder */
    uint8_t folder;
    uint8_t reserved[3];
    const char* name;  /* valid for the process's life */
} NebulaDiscEntry;

/* System settings (SCGet*), with the SDK's values (SC_LANGUAGE_*, SC_SOUND_MODE_*, SC_ASPECT_RATIO_*...). */
enum {
    NEBULA_SC_LANGUAGE = 1,
    NEBULA_SC_SOUND_MODE,
    NEBULA_SC_ASPECT_RATIO,
    NEBULA_SC_PROGRESSIVE_MODE,
    NEBULA_SC_EURGB60_MODE,
    NEBULA_SC_SENSOR_BAR_POSITION,  /* 0 below the screen, 1 above */
    NEBULA_SC_DPD_SENSITIVITY,      /* 1..5 */
    NEBULA_SC_SPEAKER_VOLUME,       /* 0..127 */
    NEBULA_SC_MOTOR_MODE,           /* 0 rumble off, 1 on */
    NEBULA_SC_SCREEN_SAVER_MODE,    /* 0 off, 1 on: the screen dims after 5 idle minutes (the game lowers its volume) */
};

/* A Wii Remote's connection: its EEPROM's first 0x2A bytes (DPD and accelerometer calibration), the attached extension
   (0 none, 1 Nunchuk: the SDK's WPAD_DEV_* values) with its 32-byte calibration block, and the battery level. */
typedef struct NebulaWiimoteConnection {
    uint8_t eeprom[0x2A];
    uint8_t extension_calibration[32];
    uint32_t extension;
    uint8_t battery; /* WPADInfo's level, 0 empty to 4 */
} NebulaWiimoteConnection;

/* The pointer a report's IR data stands for, in KPAD's units (-1..1 across the screen, x right, y down), when the
   host points directly (mouse, gyro): KPAD takes it as the position instead of solving it from the IR dots. */
typedef struct NebulaWpadPointer {
    float x;
    float y;
} NebulaWpadPointer;

/* What the game asks of a remote (wpad_control). */
enum {
    NEBULA_WPAD_INIT = 1,   /* the game started WPAD */
    NEBULA_WPAD_FORMAT,     /* value: WPAD_FMT_*, which selects the input report the host sends */
    NEBULA_WPAD_DPD,        /* value: 0 IR camera off, else on */
    NEBULA_WPAD_MOTOR,      /* value: 0 stop, 1 rumble */
    NEBULA_WPAD_SPEAKER,    /* value: the WPADControlSpeaker command (0 off, 1 on, 2 mute, 3 unmute, 4 play) */
    NEBULA_WPAD_DISCONNECT,
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

/* A test request (test_scene_frame). */
typedef struct {
    uint32_t kind;     /* NEBULA_TEST_* */
    int32_t scenario;  /* NEBULA_TEST_CHANGE_STAGE */
    float position[3]; /* NEBULA_TEST_PLACE_PLAYER */
    char stage[64];    /* NEBULA_TEST_CHANGE_STAGE */
} NebulaTestAction;

enum {
    NEBULA_TEST_CHANGE_STAGE = 1, /* MR::requestChangeStageInGameMoving(stage, scenario) */
    NEBULA_TEST_PLACE_PLAYER,     /* MR::setPlayerPos(position) */
    NEBULA_TEST_KILL_PLAYER,      /* MR::forceKillPlayerByAbyss: a miss, then retry or game over */
    NEBULA_TEST_START_MOVIE,      /* MR::startMovie(scenario): where the stage has a movie sequence */
    NEBULA_TEST_GET_STAR,         /* the star `scenario` is collected (its result sent, the get demo requested) */
    NEBULA_TEST_GET_GRAND_STAR,   /* the same with the Grand Star's demo */
    NEBULA_TEST_SET_PLAYER,       /* the file's player from the next scene (`scenario` 0 Mario, 1 Luigi) */
    NEBULA_TEST_SET_GRAND_STARS,  /* the save holds Grand Stars 1-`scenario` (their galaxies' first star) */
    NEBULA_TEST_LIST_ACTORS,      /* the scene's live actors with a model, their model name and position, logged */
    NEBULA_TEST_SET_EVENT_FLAG,   /* the save's event flag named `stage` on (GameDataFunction::onGameEventFlag) */
    NEBULA_TEST_SHAKE_CAMERA,     /* the screen shakes until the scene ends (MR::shakeCameraInfinity, Dodoryu's quake) */
    NEBULA_TEST_KILL_ACTOR,       /* the live actors whose model is `stage` killed (their kill(): what they leave behind) */
};

/* The picture's shape as the game draws it (NebulaFrameClock::display), from the output's shape the host latches at
   each retrace (display_extent): the camera, the 2D screen and the layouts widen with it. */
typedef struct NebulaDisplay {
    int32_t wide;          /* 0: the Wii's 16:9, nothing changes */
    float content_aspect;  /* width / height, 4:3 to 32:9 */
    int32_t screen_width;  /* MR::getScreenWidth in the 16:9 mode (832 at 16:9) */
    float canvas_width;    /* the NW4R layout canvas (608 at 16:9 and narrower) */
    float vertical_fit;    /* the layouts' height scale under 16:9 (1 at 16:9 and wider) */
} NebulaDisplay;

/* Nebula options the game follows (option). */
enum {
    NEBULA_OPTION_TEXT_SPEED = 1, /* 1 to 3: text steps revealed per frame; 4: each page at once */
    NEBULA_OPTION_TEST_DUMP_LAYOUTS, /* tests: 1 logs each layout's pane tree once (GALAXY_TEST_DUMP_LAYOUTS) */
    NEBULA_OPTION_DRAW_IDENTITIES, /* 1 while the renderer reads host markers (NebulaRender::hostMarker) */
    NEBULA_OPTION_RT_SHADOW_CASTERS, /* 1 while the ray tracer lights the scene: actors are drawn to the camera's far
                                        plane at full detail (LodCtrl), and out-of-view ones whose shadow or light
                                        reaches the view stay unclipped (ClippingActorInfo::isWantedForLight) */
    NEBULA_OPTION_LOD_SCALE,         /* the 3D image's lines over the console's 456 as shown (1 at its resolution, 0
                                        before the first scene): LodCtrl's switch distances grow by it */
    NEBULA_OPTION_TEST_LOD,          /* tests (GALAXY_TEST_LOD): 1 every LodCtrl shows its middle model, 2 its low
                                        model, 3 its high and middle models in turn each frame (the switch itself) */
    NEBULA_OPTION_BLOOM,             /* how BloomEffect and BloomEffectSimple go through the renderer
                                        (NebulaRender::bloomMarker): 0 not at all (their copy chains, as the console's), 1
                                        their own chains with the scene they work over kept and restored by it (in the
                                        EFB's own format), 2 it draws the bloom from their values */
    NEBULA_OPTION_RT_PREPARATION,    /* ray tracing's shader preparation, which the title waits for (NebulaRtPrepare):
                                        0 none, 1 the renderer opens its pipeline library, 2 it compiles the pipelines
                                        (the preparation screen shows) */
    NEBULA_OPTION_RT_PREPARE_PROGRESS, /* while NEBULA_OPTION_RT_PREPARATION is 2: its progress, 0 to 10000, never
                                          going back */
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

/* Audio interface requests (ai_control). */
enum {
    NEBULA_AI_START = 1, /* the DMA engine runs the latched blocks */
    NEBULA_AI_STOP,
    NEBULA_AI_RATE,      /* value: the output rate in Hz (32000 or 48000) */
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
    NEBULA_ICON_KEEP = 0, /* the Wii's own icon */
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
   apply as an Xbox controller's A, B, X and Y; a Switch controller's by their labels, as on the console. */
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
   names UTF-16 (host byte order, 10 characters, unused ones 0), the create ID as the console stores it. */
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

typedef struct NebulaHostServices {
    uint32_t version;
    uint32_t size;
    void* user;

    /* One line of the game's reports (OSReport) or the native layer's log, without a trailing newline. */
    void (*log)(void* user, const char* text);
    /* The game cannot go on (an OSPanic, a JUTException panic, a native-layer error): the host reports `text` with
       its own diagnostics and ends the process. Does not return. */
    void (*fatal)(void* user, const char* text);

    /* A scene bound (NEBULA_SCENE_*) after the records so far; the 3D scene's begin carries 16 floats: the
       camera's world-to-eye matrix (row-major 3x4), then the player's up in the world (against its gravity) and 1, or
       four zeros without a player. The others NULL. */
    void (*scene_marker)(void* user, uint32_t marker, const float* view);
    /* A JPA stripe particle whose two strip vertices are drawn next (galaxy/motion_signals.h): age << 16 |
       lifetime, strip 0 JPADrawStripe, 1-2 the passes of JPADrawStripeX. */
    void (*stripe_particle)(void* user, uint64_t particle, uint32_t age_lifetime, uint8_t strip);

    /* Disc: the file or folder at `path` ("/StageData/EggStarGalaxy.arc": from the root, '/' at a name's start
       returning to it, "." and ".." steps, letters in either case, a name followed by '/' a folder): 0 when there is
       none. */
    int (*disc_find)(void* user, const char* path, NebulaDiscEntry* entry);
    /* The entry with file-table index `index`: 0 when there is none. */
    int (*disc_entry)(void* user, int32_t index, NebulaDiscEntry* entry);
    /* The `child`-th entry of the folder `folder`, in the table's order: 0 past the last. */
    int (*disc_child)(void* user, int32_t folder, uint32_t child, NebulaDiscEntry* entry);
    /* Reads `size` bytes at `offset` of the file `file` into `dst` without blocking (offset at most the file's length,
       the end before its length plus 32: the bytes after a file are the disc's); the host calls
       nebula_game_disc_complete(slot, bytes read or -1) from any thread when done. */
    void (*disc_read)(void* user, int32_t file, uint32_t offset, uint32_t size, void* dst, uint32_t slot);
    /* The game partition's main.dol as on the disc, valid for the process's life: the module reads the data tables
       the game keeps in its executable from it at boot (sdk/dol_data.c). */
    const uint8_t* (*disc_dol)(void* user, size_t* size);
    /* The disc's game id ("RMGE01", "RMGP01" or "RMGJ01", NUL-terminated), valid for the process's life: the release
       whose few differences the module follows (sdk/region.c) and whose save folder the NAND holds. */
    const char* (*disc_id)(void* user);

    /* Video: at each retrace, the frame buffer the display shows from now (its physical address; 0 before the first),
       whether the output is black, and the retrace count. Called on the simulation thread; the host only records it. */
    void (*vi_frame)(void* user, uint32_t xfb, int black, uint32_t retrace);

    /* Starts a save file operation without blocking; the request's name, buffer, length and status stay valid until
       its status leaves NEBULA_SAVE_PENDING (the host writes it from its own thread). */
    void (*save_request)(void* user, const NebulaSaveRequest* request);

    /* A system setting (NEBULA_SC_*). */
    uint32_t (*sc_get)(void* user, uint32_t item);

    /* Wii Remotes: a request to remote `chan` (NEBULA_WPAD_*), and speaker data (the game's encoded ADPCM). */
    void (*wpad_control)(void* user, uint32_t chan, uint32_t what, uint32_t value);
    void (*wpad_speaker_data)(void* user, uint32_t chan, const uint8_t* data, uint32_t size);

    /* JAudio's mixer (on the console a DSP program; the host runs it). Pointers are into the game's memory.
       jaudio_setup hands over the channel records (`channels` of them, in the program's layout, host byte order), the
       resampling filter (640 words) and ADPCM coefficients (32 words) and the four FX lines, once. */
    void (*jaudio_setup)(void* user, uint32_t channels, NebulaJAudioChannel* channel_records,
                         const int16_t* resample_filter,
                         const int16_t* adpcm_filter, const NebulaJAudioFxLine* fx_lines);
    /* The game's sound data in its auxiliary memory: the channels' wave offsets count from `base`, below `size`. */
    void (*jaudio_aux)(void* user, const void* base, uint32_t size);
    /* An audio frame: `subframes` subframes of 80 samples mix into the left and right output planes at
       `mixer_level` (4096 = 1). */
    void (*jaudio_frame)(void* user, uint32_t subframes, uint32_t mixer_level, int16_t* left, int16_t* right);
    /* Channel group `group` (0-3) is ready; `active` holds its 16 channels' active bits, the first channel in bit 15.
       Group 3 mixes the frame's next subframe: returns 1 when it did. */
    int (*jaudio_mix)(void* user, uint32_t group, uint32_t active);
    /* The AI's next DMA block: `size` bytes of 16-bit (right, left) pairs in host byte order, latched to play after
       the current block; the host calls nebula_game_ai_interrupt when it takes it. The block stays the game's: the
       host reads it while it plays. */
    void (*ai_dma)(void* user, const void* samples, uint32_t size);
    void (*ai_control)(void* user, uint32_t what, uint32_t value);

    /* The HOME Menu (NEBULA_HBM_*). */
    int (*hbm)(void* user, uint32_t what, uint32_t value);

    /* THP: one movie frame's video component (`size` bytes, one JPEG whose entropy data is not byte-stuffed, so only
       the size bounds it) into its Y, U and V planes in game memory, which must hold the result before the renderer
       reads them; 0 on success, as THPVideoDecode returns. */
    int32_t (*thp_video_decode)(void* user, const void* frame, uint32_t size, void* y, void* u, void* v);

    /* The game wrote [address, address + size) for the GPU: the console's cache flushes (DCFlushRange,
       DCStoreRange) and DMA copies. An image handed over there must be handed over again. Called on the simulation
       thread; the host's own writes to game memory (disc reads) need no call. */
    void (*gpu_coherent)(void* user, const void* address, uint32_t size);

    /* Game state the host's controls follow (NEBULA_CONTEXT_*), reported when it changes. */
    void (*game_context)(void* user, uint32_t what, int32_t value);

    /* The game ends (NEBULA_POWER_*): OSShutdownSystem, OSRestart, OSReturnToMenu, OSRebootSystem. Does not return. */
    void (*power)(void* user, uint32_t what);

    /* Tests: once per game scene frame (before its 3D), what a hidden run asks of the game (GALAXY_TEST_*): returns 1
       with `action` filled, or 0. */
    int (*test_scene_frame)(void* user, NebulaTestAction* action);

    /* The output's shape latched at this retrace, after vi_frame: width << 16 | height (only the ratio counts), or 0
       for the Wii's 16:9. */
    uint32_t (*display_extent)(void* user);
    /* The borders around a fitted picture take this grey (0 black to 1 white) for the scene being drawn: the Wii Remote
       strap screen's white backing, as it fades. */
    void (*scene_surround)(void* user, float intensity);
    /* A Nebula option (NEBULA_OPTION_*), read when the game needs it. */
    uint32_t (*option)(void* user, uint32_t what);
    /* A light of the game (NEBULA_LIGHT_*): `id` names it across frames (its object's address; 0: none), `values` as
       the kind lists. Called on the simulation thread. */
    void (*game_light)(void* user, uint32_t kind, uint64_t id, const float* values);
    /* The stage being played (MR::getCurrentStageName), before each 3D scene's begin marker. */
    void (*game_stage)(void* user, const char* name);
    /* A game object's name (NameObj::mName, Shift-JIS as the game spells it) when it is made or renamed; `id` is the
       object's address, as the host markers name it. */
    void (*object_name)(void* user, uint64_t id, const char* name);
    /* While NEBULA_OPTION_RT_SHADOW_CASTERS is 1: how far (world units) the traced light of a model (its resource name,
       MR::getModelResName) reaches from a ball of `radius` around it; 0 when the model is no light source. */
    float (*light_reach)(void* user, const char* model, float radius);
    /* While NEBULA_OPTION_RT_SHADOW_CASTERS is 1: the world direction towards the sun the ray tracer lights the scene
       from (3 floats, unit length): 1, or 0 without one. */
    int (*traced_sun)(void* user, float* direction);
    /* A THP movie (THPSimplePlayerWrapper): its disc path and NEBULA_MOVIE_OPENING when it opens, each frame it draws
       (the frame within the movie, its frame rate and the physical addresses of its I8 Y, U and V planes, `width` x
       `height` for Y and half each way for U and V), NEBULA_MOVIE_NONE when it closes. The host may show a replacement
       video's frame of the same time in place of the planes. Called on the simulation thread. */
    void (*movie_frame)(void* user, const char* name, int32_t frame, float frame_rate, const void* y, const void* u,
                        const void* v, uint32_t width, uint32_t height);
    /* A native equivalent of a mod's code patch (NEBULA_PATCH_*): 1 with `value` set when an enabled mod carries it,
       else 0. Fixed for the session. */
    int (*code_patch)(void* user, uint32_t id, uint32_t* value);
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
    /* The game reads remote `chan` now (each frame, before KPADRead): the host's newest input in a report at once
       (nebula_game_wpad_report, from this thread), rather than at its next one. */
    void (*wpad_sample)(void* user, uint32_t chan);
    /* GALAXY_EXPORT_ART=<folder>: Nebula's own pictures written there (NebulaArtExport). NULL asks whether an export
       is on: 1, else 0 (the game then paints nothing for it). */
    uint32_t (*art_export)(void* user, const NebulaArtExport* art);
    /* The scale the 2D layer was last drawn at: its 640 x 456 picture times this (the window's while sharp_2d is on);
       0 before the first frame. */
    uint32_t (*layer_2d_scale)(void* user);
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
    /* Draws shape group `shape` (NebulaShapeGeometry::key) under the drawing state the game has set so far, its corners reading the position,
       normal and colour 0 arrays named by `position`, `normal` and `color` (shape_array keys; the geometry's own for
       the rest) and the position and normal matrices among `matrices` (`matrix_count` of them, NebulaDrawMatrix),
       after their texture matrices, `texgens` and, when not null, `current` take effect. Records (shapes, materials,
       state, immediates, images, captures, markers) run in the order the game makes them. Shape 0 draws nothing:
       only the texture matrices, current matrices and texgens take effect (state the game set before a material
       record, kept in order with it). */
    void (*shape_draw)(void* user, uint64_t shape, uint64_t position, uint64_t normal,
                       uint64_t color, const NebulaDrawMatrix* matrices, uint32_t matrix_count,
                       const NebulaShapeCurrent* current, const NebulaShapeTexGen* texgens, uint32_t texgen_count);
    /* A material program (NebulaMaterialProgram), registered or replaced under `program` (the display list's
       identity); the host copies it and prepares its pipelines. */
    void (*material_program)(void* user, uint64_t program, const NebulaMaterialProgram* material);
    /* The material the next shape draws use: program `program` (material_program) with `values`; the host copies
       them. */
    void (*material)(void* user, uint64_t program, const NebulaMaterialValues* values);
    /* Drawing-state changes the game makes outside materials (NebulaStateOp, `count` of them, in order). */
    void (*state)(void* user, const NebulaStateOp* ops, uint32_t count);
    /* Vertices drawn as the game sends them (NebulaImmediate). */
    void (*immediate)(void* user, const NebulaImmediate* draw);
    /* An image's bytes as the game has them (NebulaTextureImage): the renderer takes a copy and draws the image from
       it from here on. Sent when the image is loaded or rewritten. */
    void (*texture_image)(void* user, const NebulaTextureImage* image);
    /* The Mii in a slot of the console's Mii database (0-99): 1 when there is one (`mii` filled when not NULL), 0 for
       an empty slot. */
    int32_t (*mii_record)(void* user, uint32_t slot, NebulaMii* mii);
    /* The picture's colour and depth where the drawing has reached, kept as `channel`'s. */
    void (*readback_capture)(void* user, uint32_t channel);
    /* A pixel of `channel`'s latest finished capture, in frame-buffer coordinates (640x528): `depth` 0 its colour
       (0xAARRGGBB), 1 its depth (0x00ZZZZZZ); 0 before one has finished. Never waits for the GPU. */
    uint32_t (*readback_pixel)(void* user, uint32_t channel, uint32_t depth, uint32_t x, uint32_t y);
    /* The game's frame is complete (its picture was shown earlier): the renderer takes every record made before. */
    void (*frame_end)(void* user);
    /* A host marker (galaxy/motion_signals.h: a draw identity, a bloom phase, a clear quad). A draw identity names its
       objects by address. */
    void (*marker)(void* user, uint32_t kind, uint32_t aux, uint64_t a, uint64_t b);
    /* How many captures of `channel`'s colour have finished since the game first read it (0: none): a capture made
       when the count was n is readable once it passes n. */
    uint32_t (*readback_serial)(void* user, uint32_t channel);
    /* Nanoseconds on a steady clock from an arbitrary start (never 0): the game's durations and pacing. */
    uint64_t (*clock_ns)(void* user);
    /* The local date and time now: nanoseconds from 2000-01-01 00:00 local time (the console's clock counted from
       there; the game's saves store times in its unit). */
    int64_t (*calendar_ns)(void* user);
} NebulaHostServices;


enum {
    NEBULA_MOVIE_NONE = -1,
    NEBULA_MOVIE_OPENING = -2,
};

/* Native equivalents of mods' patches to the console's code (memory patches over main.dol's text): the host's mods
   layer recognises each by its exact bytes for this disc and the game asks code_patch for it. */
enum {
    NEBULA_PATCH_FLIP_PANEL_GROUP = 1,       /* value: a FlipPanel group's size (joinToGroupArray's 0x20) */
    NEBULA_PATCH_FLIP_PANEL_OBSERVER_GROUP,  /* value: the same for FlipPanelObserver */
    NEBULA_PATCH_WATER_BAZOOKA_SHOT_FLAG,    /* WaterBazooka::tryShotBullet: shotWaterBullet's last flag set */
    NEBULA_PATCH_KAMECK_BARRIER_OFFSET_Y,    /* value: float bits added to BossKameckBarrier's height at init */
    NEBULA_PATCH_SKELETAL_FISH_BABY_NO_BUBBLE, /* SkeletalFishBaby::damage releases no air bubble */
    NEBULA_PATCH_DODORYU_SHELL_ONLY,         /* Dodoryu (Lv1, Lv2) is hurt by a shell alone (isMsgJetTurtleAttack) */
    NEBULA_PATCH_MISS_STAR_PIECES,           /* value: star bits added after a miss (s32; -999: all lost) */
    NEBULA_PATCH_NO_HIDDEN_STAR_TABLE,       /* getPlacedHiddenStarScenarioNo skips its table */
    NEBULA_PATCH_NO_SUDDEN_DEATH_DODORYU,    /* isStageSuddenDeathDodoryu is false */
    NEBULA_PATCH_BEGOMAN_FIRE_PROOF,         /* Begoman Baby, Spike and Spring survive fire ground */
    NEBULA_PATCH_MESSENGER_ALWAYS_EX,        /* KinopioAstro (the 1-up messenger) gives its larger supply */
    NEBULA_PATCH_MESSENGER_EX_ONE_UPS,       /* value: that supply's 1-ups (20) */
    NEBULA_PATCH_COUNT
};

/* __NebulaDolTable kinds: 0-5 the Grand galaxies' comet schedules, then: */
enum { NEBULA_DOL_EVENT_FLAGS = 6 };

enum {
    NEBULA_POWER_SHUTDOWN = 1, /* the console turns off: the game closes */
    NEBULA_POWER_RESTART,      /* the game starts again from its boot */
    NEBULA_POWER_MENU,         /* back to the Wii Menu: the game closes */
    NEBULA_POWER_REBOOT,       /* the console restarts: the game closes */
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
/* A disc read finished: `result` is the bytes read, or -1 on a read error. */
NEBULA_GAME_API void nebula_game_disc_complete(uint32_t slot, int32_t result);
/* A video retrace (the display clock: one per field at 59.94 Hz, or the host's presentation pace). */
NEBULA_GAME_API void nebula_game_vi_retrace(void);
/* A NAND operation finished. */
/* The AI took the latched DMA block (the console's AI DMA interrupt): the game prepares the next. */
NEBULA_GAME_API void nebula_game_ai_interrupt(void);
/* The sound playing on DSP channel `channel` (0-63) for the host's volume categories: its JAudio sound ID, with *route
   1 (an effect), 2 (a sequence) or 3 (a stream); 0 with *route 0 when no sound the game can name plays there. The host
   calls it from the DSP command it is running (the game's audio thread). */
NEBULA_GAME_API uint32_t nebula_game_audio_channel_owner(uint32_t channel, uint32_t* route);
/* Mii import. prepare: makes a Mii (a 74-byte record as the console stores it) acceptable where the console would
   refuse a detail (birthday, special Mii not kept local, favourite colour, no name), then 1 when the game takes it as
   its database's; pure, any thread. install: places it in slot `slot` (0-99) of the loaded database, free or holding
   the same Mii (its create ID); 0 when the database is not loaded or another Mii holds the slot. On the game thread
   (the host's retrace). */
/* Whether the game takes a Mii into its database (its parts and values in range); the host keeps only these. */
NEBULA_GAME_API uint32_t nebula_game_mii_valid(const NebulaMii* mii);
/* The host's Mii database changed (Miis imported while the game runs): the game reads its slots again. */
NEBULA_GAME_API void nebula_game_mii_changed(void);
/* A Wii Remote connected (with its calibration) or disconnected. */
NEBULA_GAME_API void nebula_game_wpad_connect(uint32_t chan, int connected, const NebulaWiimoteConnection* connection);
/* A Wii Remote input report, from its id (0x30, 0x31, 0x33 or 0x37 as the data format asks), extension bytes decrypted;
   one per report the remote sends (100 per second). `pointer`: the exact position its IR dots stand for, or NULL (a
   real remote). */
NEBULA_GAME_API void nebula_game_wpad_report(uint32_t chan, const uint8_t* report, size_t size,
                                             const NebulaWpadPointer* pointer);
/* The shake buttons held on a controller standing in for a remote (bit 0: the remote's, bit 1: the Nunchuk's): each
   press is one swing for the game's swing detector, the frame it is pressed. Any thread; called on every change. */
NEBULA_GAME_API void nebula_game_wpad_shake(uint32_t chan, uint32_t held);
/* A connected remote's battery, as WPADInfo's level (0 empty to 4), when it changes. */
NEBULA_GAME_API void nebula_game_wpad_battery(uint32_t chan, uint32_t level);

#ifdef __cplusplus
}
#endif

#endif
