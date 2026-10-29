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

#define NEBULA_GAME_API_VERSION 37u

/* A matrix a shape draw reads (shape_draw), by GX's matrix id: position and texture matrices 0-63 in threes, a normal
   matrix by its position matrix's id, post-transform texture matrices from 64. `values` holds rows of four (a normal
   matrix rows of three, a 2x4 texture matrix two rows). `model_matrix`: the model's draw matrix it is (its index + 1;
   0 for any other), which names it from frame to frame. */
enum {
    NEBULA_SHAPE_MATRIX_POSITION = 0,
    NEBULA_SHAPE_MATRIX_NORMAL = 1,
    NEBULA_SHAPE_MATRIX_TEXTURE = 2,
    NEBULA_SHAPE_MATRIX_TEXTURE_2X4 = 3,
    NEBULA_SHAPE_MATRIX_POST_TEXTURE = 4,
};

typedef struct NebulaShapeMatrix {
    uint8_t kind;
    uint8_t id;
    uint16_t model_matrix;
    float values[12];
} NebulaShapeMatrix;

/* The matrices a shape draw's corners take when they carry none (shape_draw): the position matrix's id and texture
   coordinates 0-7's. */
typedef struct NebulaShapeCurrent {
    uint8_t position;
    uint8_t texture[8];
} NebulaShapeCurrent;

/* A J3D material as the next shape draws use it (material in NebulaHostServices): every value as GX's functions take
   it, no register words. Colours are RGBA bytes; TEV colours signed 10-bit values in int16_t. */
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

typedef struct NebulaMaterial {
    uint8_t tev_stages, tex_gens, channels, ind_stages, cull;
    uint8_t dither, z_compare_location;
    uint8_t origin; /* where the game captured it: 1 a packet's list, 2 a differed list, 3 a shared list, 0 at the draw */
    uint16_t loads; /* NEBULA_MATERIAL_LOADS_*: the parts the material sets (the others keep what was set before) */
    NebulaMaterialTevStage tev[16];
    int16_t tev_color[4][4];
    uint8_t konst_color[4][4];
    uint8_t swap_table[4][4];
    struct { uint8_t coord, map, scale_s, scale_t; } ind_order[4];
    struct { float m[2][3]; int8_t scale; uint8_t reserved[3]; } ind_matrix[3];
    struct { uint8_t type, source, matrix, normalize, post_matrix, reserved[3]; } tex_gen[8];
    struct { uint16_t scale_s, scale_t; uint8_t bias_s, bias_t, wrap_s, wrap_t; } tex_coord_scale[8];
    NebulaMaterialTexture texture[8];
    struct { uint8_t enable, material_source, ambient_source, light_mask, diffuse, attenuation, reserved[2]; } channel[4];
    uint8_t material_color[2][4], ambient_color[2][4];
    uint8_t light_mask;           /* the lights below that are set */
    NebulaMaterialLight light[8];
    struct { uint8_t type, adjust_enable; uint16_t adjust_center; float start, end, near_z, far_z; uint8_t color[4];
             uint16_t adjust[10]; } fog;
    struct { uint8_t comp0, ref0, op, comp1, ref1, reserved[3]; } alpha_compare;
    struct { uint8_t type, source, destination, logic_op; } blend;
    struct { uint8_t enable, func, update, reserved; } z_mode;
    /* 0: the material as its list sets it. Else an instance's differed list: only the parts these J3D diff flags name
       (J3DDiffFlag_*: counts of textures, stages, texgens and lights, TEV and konst registers, fog, blend, ...). */
    uint32_t diff_flags;
} NebulaMaterial;

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

/* One corner of a shape group: the entry it takes from each array and its matrix slots. */
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

/* Which arrays a shape group's corners read and which matrix slots they carry (NebulaShapeGeometry::attributes). */
enum {
    NEBULA_SHAPE_NORMAL = 1u << 0,
    NEBULA_SHAPE_COLOR0 = 1u << 1,
    NEBULA_SHAPE_COLOR1 = 1u << 2,
    NEBULA_SHAPE_TEXCOORD0 = 1u << 3,         /* to 1u << 10 for texture coordinate 7 */
    NEBULA_SHAPE_NBT = 1u << 11,              /* normals from the NBT array */
    NEBULA_SHAPE_NBT_THREE = 1u << 12,        /* separate normal, tangent and binormal entries */
    NEBULA_SHAPE_POSITION_MATRIX = 1u << 13,  /* corners carry their position matrix slot */
    NEBULA_SHAPE_TEXTURE_MATRIX0 = 1u << 14,  /* to 1u << 21: corners carry texture matrix 0-7's slot */
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

/* A physical range of the game's memory and where it is in the host process: what the GPU's addresses resolve to. */
typedef struct NebulaGameMemoryRegion {
    uint32_t physical;
    uint32_t size;
    uint8_t* host;
} NebulaGameMemoryRegion;

/* A NAND file system operation (nand_native.c), on the console's absolute paths ("/title/00010000/524d4745/data/..."). */
enum {
    NEBULA_FS_OPEN = 1,     /* path, mode = NAND access type -> fd */
    NEBULA_FS_CLOSE,        /* fd */
    NEBULA_FS_READ,         /* fd, buffer, size -> bytes */
    NEBULA_FS_WRITE,        /* fd, buffer, size -> bytes */
    NEBULA_FS_SEEK,         /* fd, offset, whence (0 set, 1 current, 2 end) -> position */
    NEBULA_FS_LENGTH,       /* fd -> length */
    NEBULA_FS_CREATE,       /* path, mode = permission, attribute */
    NEBULA_FS_CREATE_DIR,   /* path, mode = permission, attribute */
    NEBULA_FS_DELETE,       /* path (a file, or a directory with what it holds) */
    NEBULA_FS_RENAME,       /* path -> path2 */
    NEBULA_FS_SAFE_OPEN,    /* path, mode -> fd: writes go to a copy that NEBULA_FS_SAFE_CLOSE puts in place atomically */
    NEBULA_FS_SAFE_CLOSE,   /* fd */
};

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

/* The picture's shape as the game draws it (__NebulaDisplay), from the output's shape the host latches at each retrace
   (display_extent): the camera, the 2D screen and the layouts widen with it. */
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
    NEBULA_OPTION_DRAW_IDENTITIES, /* 1 while the renderer reads host markers (__NebulaGXHostMarker) */
    NEBULA_OPTION_RT_SHADOW_CASTERS, /* 1 while the ray tracer lights the scene: actors are drawn to the camera's far
                                        plane at full detail (LodCtrl), and out-of-view ones whose shadow or light
                                        reaches the view stay unclipped (ClippingActorInfo::isWantedForLight) */
    NEBULA_OPTION_LOD_SCALE,         /* the 3D image's lines over the console's 456 as shown (1 at its resolution, 0
                                        before the first scene): LodCtrl's switch distances grow by it */
    NEBULA_OPTION_TEST_LOD,          /* tests (GALAXY_TEST_LOD): 1 every LodCtrl shows its middle model, 2 its low
                                        model, 3 its high and middle models in turn each frame (the switch itself) */
    NEBULA_OPTION_BLOOM,             /* how BloomEffect and BloomEffectSimple go through the renderer
                                        (__NebulaGXBloomMarker): 0 not at all (their copy chains, as the console's), 1
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

/* gx_scene_marker: the scene bounds in the stream. */
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

typedef struct NebulaFsRequest {
    uint32_t op;
    const char* path;
    const char* path2;
    int32_t fd;
    void* buffer;
    uint32_t size;
    int32_t offset;
    int32_t whence;
    uint32_t mode;
    uint32_t attribute;
} NebulaFsRequest;

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

typedef struct NebulaHostServices {
    uint32_t version;
    uint32_t size;
    void* user;

    /* One line of the game's reports (OSReport) or the native layer's log, without a trailing newline. */
    void (*log)(void* user, const char* text);
    /* The game cannot go on (an OSPanic, a JUTException panic, a native-layer error): the host reports `text` with
       its own diagnostics and ends the process. Does not return. */
    void (*fatal)(void* user, const char* text);

    /* GX: the game's command stream (the console's main FIFO), in order, as spans of the module's command ring. A
       span is contiguous, ends where the game flushed (GXFlush, a draw sync, a draw done: a command boundary) and
       stays valid and unchanged until the host releases it (nebula_game_gx_release), so the renderer parses it in
       place. When the ring is full the game waits for releases, as the console's CPU waits on a full GP FIFO. */
    void (*gx_submit)(void* user, const uint8_t* bytes, size_t size);
    /* A PE event at the end of the stream so far: a draw-sync token (finish = 0) or a draw done (finish = 1). The
       renderer answers it with nebula_game_gx_event when its parse reaches that point. */
    void (*gx_pe_marker)(void* user, int finish, uint16_t token);
    /* An EFB pixel (type 0: colour, 1: depth), as GXPeekARGB/GXPeekZ read it; `token` is the draw-sync token
       whose callback peeks (0: none), the stream position the console's GPU was stopped at. */
    uint32_t (*gx_peek_efb)(void* user, uint16_t x, uint16_t y, uint32_t type, uint16_t token);
    /* A scene bound (NEBULA_SCENE_*) at the end of the stream so far; the 3D scene's begin carries 16 floats: the
       camera's world-to-eye matrix (row-major 3x4), then the player's up in the world (against its gravity) and 1, or
       four zeros without a player. The others NULL. */
    void (*gx_scene_marker)(void* user, uint32_t marker, const float* view);
    /* A JPA stripe particle whose two strip vertices are written next, `span_offset` bytes into the span the game
       has not submitted yet (galaxy/motion_signals.h): age << 16 | lifetime, strip 0 JPADrawStripe, 1-2 the passes
       of JPADrawStripeX. */
    void (*gx_stripe_particle)(void* user, size_t span_offset, uint32_t particle, uint32_t age_lifetime,
                               uint8_t strip);

    /* Disc: the game partition's file table as on the disc (big-endian), valid for the process's life. */
    const uint8_t* (*disc_fst)(void* user, size_t* size);
    /* Reads `size` bytes at byte `offset` of the game partition's data into `dst` (game memory) without blocking;
       the host calls nebula_game_disc_complete(slot, bytes read or -1) from any thread when done. */
    void (*disc_read)(void* user, uint64_t offset, uint32_t size, void* dst, uint32_t slot);
    /* The game partition's main.dol as on the disc, valid for the process's life: the module reads the data tables
       the game keeps in its executable from it at boot (sdk/dol_data.c). */
    const uint8_t* (*disc_dol)(void* user, size_t* size);
    /* The disc's game id ("RMGE01", "RMGP01" or "RMGJ01", NUL-terminated), valid for the process's life: the release
       whose few differences the module follows (sdk/region.c) and whose save folder the NAND holds. */
    const char* (*disc_id)(void* user);

    /* Video: at each retrace, the frame buffer the display shows from now (its physical address; 0 before the first),
       whether the output is black, and the retrace count. Called on the simulation thread; the host only records it. */
    void (*vi_frame)(void* user, uint32_t xfb, int black, uint32_t retrace);

    /* NAND: starts `request` without blocking (its strings and buffer stay valid until completion); the host calls
       nebula_game_fs_complete(slot, result) from any thread with the result above or a NAND_RESULT_* code. */
    void (*fs_request)(void* user, const NebulaFsRequest* request, uint32_t slot);

    /* A system setting (NEBULA_SC_*). */
    uint32_t (*sc_get)(void* user, uint32_t item);

    /* Wii Remotes: a request to remote `chan` (NEBULA_WPAD_*), and speaker data (the game's encoded ADPCM). */
    void (*wpad_control)(void* user, uint32_t chan, uint32_t what, uint32_t value);
    void (*wpad_speaker_data)(void* user, uint32_t chan, const uint8_t* data, uint32_t size);

    /* Audio. The JAudio DSP program: `count` command words as the game mails them to the DSP (count 0: one word,
       the release of a halted subframe), run at once on the simulation thread; returns 1 when the words completed a
       subframe (the DSP's AUDIOMSG_DSP mail). Addresses in the words are physical; the channel records and buffers
       the program reads are in host byte order, the wave data big-endian. */
    int (*audio_dsp)(void* user, const uint32_t* words, uint32_t count);
    /* The AI's next DMA block: `size` bytes of interleaved 16-bit stereo (host order) at physical `start`, latched to
       play after the current block; the host calls nebula_game_ai_interrupt when it takes it. */
    void (*ai_dma)(void* user, uint32_t start, uint32_t size);
    void (*ai_control)(void* user, uint32_t what, uint32_t value);

    /* The HOME Menu (NEBULA_HBM_*). */
    int (*hbm)(void* user, uint32_t what, uint32_t value);

    /* THP: one movie frame's video component (`size` bytes, one JPEG whose entropy data is not byte-stuffed, so only
       the size bounds it) into its Y, U and V planes in game memory, which must hold the result before the renderer
       reads them; 0 on success, as THPVideoDecode returns. */
    int32_t (*thp_video_decode)(void* user, const void* frame, uint32_t size, void* y, void* u, void* v);

    /* The game wrote [physical, physical + size) for the GPU: the console's cache flushes (DCFlushRange,
       DCStoreRange) and DMA copies. The renderer drops what it holds of that memory. Called on the simulation
       thread; the host's own writes to game memory (disc reads) need no call. */
    void (*gpu_coherent)(void* user, uint32_t physical, uint32_t size);

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
    /* A light of the game (NEBULA_LIGHT_*): `id` names it across frames (0: none), `values` as the kind lists. Called on
       the simulation thread. */
    void (*game_light)(void* user, uint32_t kind, uint32_t id, const float* values);
    /* The stage being played (MR::getCurrentStageName), before each 3D scene's begin marker. */
    void (*game_stage)(void* user, const char* name);
    /* A game object's name (NameObj::mName, Shift-JIS as the game spells it) when it is made or renamed; `id` as the
       host markers name the object (__NebulaObjectId). */
    void (*object_name)(void* user, uint32_t id, const char* name);
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
    void (*movie_frame)(void* user, const char* name, int32_t frame, float frame_rate, uint32_t y, uint32_t u,
                        uint32_t v, uint32_t width, uint32_t height);
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
       the rest), after `matrices` (`matrix_count` of them) and, when not null, `current` take effect. `stream` is the
       game's GX command writer when it draws: the draw comes after every command written before it. */
    void (*shape_draw)(void* user, uint64_t shape, const void* stream, uint64_t position, uint64_t normal,
                       uint64_t color, const NebulaShapeMatrix* matrices, uint32_t matrix_count,
                       const NebulaShapeCurrent* current);
    /* The material the next shape draws use (NebulaMaterial), after every command the game wrote before it (`stream`
       as shape_draw takes it). */
    void (*material)(void* user, const void* stream, const NebulaMaterial* material);
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
/* The game's memory regions for the renderer (valid once nebula_game_run has started the game). */
NEBULA_GAME_API int nebula_game_memory_regions(NebulaGameMemoryRegion* out, int max);
/* The renderer reached a PE draw-sync token (finish = 0) or a draw done (finish = 1). */
NEBULA_GAME_API void nebula_game_gx_event(int finish, uint16_t token);
/* The host is done with the next `size` bytes of the command stream (spans are released in order, whole or in part):
   the ring may reuse them. The host must release without waiting on further progress of the game, which may itself be
   waiting for room. */
NEBULA_GAME_API void nebula_game_gx_release(size_t size);
/* A disc read finished: `result` is the bytes read, or -1 on a read error. */
NEBULA_GAME_API void nebula_game_disc_complete(uint32_t slot, int32_t result);
/* A video retrace (the display clock: one per field at 59.94 Hz, or the host's presentation pace). */
NEBULA_GAME_API void nebula_game_vi_retrace(void);
/* A NAND operation finished. */
NEBULA_GAME_API void nebula_game_fs_complete(uint32_t slot, int32_t result);
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
NEBULA_GAME_API uint32_t nebula_game_mii_prepare(uint8_t* record);
NEBULA_GAME_API uint32_t nebula_game_mii_install(uint32_t slot, const uint8_t* record);
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
