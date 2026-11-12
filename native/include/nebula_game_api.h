/* The game module's interface to Nebula's host (docs/NATIVE_PORT.md, "Module layout"): plain C, so the game module
   (built against the game's own C++ library) and the host (the host's C++ library) share nothing else. The host fills
   NebulaHostServices and calls nebula_game_attach, then runs the game on its game thread with nebula_game_run. Game code
   runs on that thread only, but for nebula_game_sound_frame (the host's audio clock) and nebula_game_mii_valid (any
   thread, pure). */
#ifndef NEBULA_GAME_API_H
#define NEBULA_GAME_API_H

#include <stddef.h>
#include <stdint.h>
#include "nebula_frame_list.h"

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

#define NEBULA_GAME_API_VERSION 152u

/* The game's fixed step: nebula_game_run's loop runs this many steps a second (frame_wait paces them), whatever the
   display's rate; the host presents each or interpolates between them. */
#define NEBULA_STEPS_PER_SECOND 60

/* Viewports, scissors, clears, target sizes and picture points count this many units across a frame on each axis. */
#define NEBULA_FRAME_UNITS 65536.0f

/* The height the game's 2D coordinates, effects and mip chains were authored for: the frame's height over it scales an
   effect's reach and a texture's mip bias. The one definition (the game's NebulaFrameClock and the renderer use it). */
#define NEBULA_AUTHORED_LINES 456

/* A colour word, R << 24 | G << 16 | B << 8 | A. */
#define NEBULA_RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

/* Audio (the host's mixer): the game's voices mix at the output device's rate, one tick's worth per voices_mix call
   (the clock's rate, NEBULA_AUDIO_START). A voice plays one source (PCM16 or an oscillator) through
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

/* The stencil a pass tests and changes: every pixel it reaches (always), only where the value is zero, or only where it is
   not. The operation runs where the test and the depth test pass; add and take stop at the ends. */
enum {
    NEBULA_STENCIL_OFF = 0,
    NEBULA_STENCIL_ALWAYS,
    NEBULA_STENCIL_IF_ZERO,
    NEBULA_STENCIL_IF_SET,
};
enum {
    NEBULA_STENCIL_KEEP = 0,
    NEBULA_STENCIL_ADD,
    NEBULA_STENCIL_TAKE,
    NEBULA_STENCIL_ZERO,
};
/* A light: colour, angle and distance terms, position and direction in view space. */
typedef struct NebulaLight {
    uint8_t color[4];
    float angle[3];
    float distance[3];
    float position[3];
    float direction[3];
} NebulaLight;

/* An image: its identity (the game's address of its bytes), format (the image file's format number), size, mipmap levels
   and its bytes (`size` of them, in the format's layout). */
typedef struct NebulaTextureImage {
    const void* bytes;
    uint32_t size;
    uint32_t width, height;
    uint8_t format;
    uint32_t levels;
} NebulaTextureImage;

/* A written shader (runtime/src/render/materials/<name>.hlsl). */
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
    NEBULA_SHADER_FONT_TEXT = 27,            /* Ui::CharWriter: glyphs mapped between two colours, by the vertex colour */
    NEBULA_SHADER_J2D_PICTURE = 28,          /* J2DPicture: up to four textures blended by constant ratios, remapped */
    NEBULA_SHADER_FUR = 29,                  /* FurDrawer: a fur shell over the body texture, lit */
    NEBULA_SHADER_PARTICLE = 30,             /* particles: the texture by the emitter's colours, its colour mode */
    NEBULA_SHADER_OCEAN_BOWL_BLOOM = 31,     /* Beach Bowl's sea from under water: the water at two scrolls over grey */
    NEBULA_SHADER_MII_FACE = 32,             /* Mii faces: a part's form, Mii's light or the Mii NPCs' stages */
    NEBULA_SHADER_MII_TEXTURE = 33,          /* a Mii's face texture parts: eyes, eyebrows, mouth, mustache, mole */
    NEBULA_SHADER_IMAGE_EFFECT = 34,         /* the game's screen texture passes: modulate, threshold, masks, edge */
    NEBULA_SHADER_GAUSSIAN_BLUR = 35,        /* ImageEffectLocalUtil::blurTexture: one axis of a Gaussian */
    NEBULA_SHADER_DOF_FINAL = 36,            /* the game's depth of field: blurred over sharp by the depth's ramp */
    NEBULA_SHADER_NORMAL_MAP = 37,           /* NormalMapBase: a normal-mapped shape lit through a light gradient */
    NEBULA_SHADER_MOVIE = 38,      /* MovieDraw (THPDraw): a movie frame's Y, U and V planes to RGB */
    NEBULA_SHADER_WATER_CAMERA = 39, /* WaterCameraFilter: the screen warped and darkened under water */
    NEBULA_SHADER_FRAME_RIPPLE = 40, /* MarioActor's heat and cold ripples: the frame, each row moved by a wave */
    NEBULA_SHADER_FRAME_DEPTH = 41, /* the frame's depth as the game reads it, averaged over the pass's pixel */
    NEBULA_SHADER_PARTICLE_INSTANCES = 42, /* the particle material's vertex stage for NEBULA_ITEM_INSTANCES draws */
    NEBULA_SHADER_GLYPH_INSTANCES = 43, /* the font material's vertex stage for CharWriter's instanced glyphs */
    NEBULA_SHADER_COUNT
} NebulaShaderId;

enum {
    NEBULA_BLEND_REPLACE = 0,  /* blend_op: the source replaces */
    NEBULA_BLEND_ADD = 1,      /* source * blend_src + destination * blend_dst */
    NEBULA_BLEND_SUBTRACT = 2, /* destination - source */
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

/* A file or folder of the asset store. */
typedef struct NebulaAssetEntry {
    int32_t index;      /* in the file table; 0 is the root folder */
    uint32_t length;    /* a file's length in bytes; a folder's, from its first file to its last one's end */
    uint64_t position;  /* the first byte in the store */
    uint32_t children;  /* a folder's entries */
    uint8_t folder;
    uint8_t kind;       /* a file's NEBULA_ASSET_KIND_*, from the importer (cook/store.rs resource_kind) */
    uint8_t reserved[2];
    const char* name;   /* valid for the process's life */
} NebulaAssetEntry;

/* What the game makes of a file of an archive (ResourceHolder, LayoutHolder): its name's extension, at import. */
enum {
    NEBULA_ASSET_KIND_OTHER = 0,
    NEBULA_ASSET_KIND_MODEL,             /* bdl, bmd */
    NEBULA_ASSET_KIND_MOTION,            /* bck, bca */
    NEBULA_ASSET_KIND_TEXTURE_SRT,       /* btk */
    NEBULA_ASSET_KIND_COLOR_KEY,         /* bpk */
    NEBULA_ASSET_KIND_TEXTURE_PATTERN,   /* btp */
    NEBULA_ASSET_KIND_CLUSTER_KEY,       /* blk */
    NEBULA_ASSET_KIND_REGISTER_COLOR,    /* brk */
    NEBULA_ASSET_KIND_SOUND_ANIMATION,   /* bas */
    NEBULA_ASSET_KIND_MATERIALS,         /* bmt */
    NEBULA_ASSET_KIND_VISIBILITY,        /* bva */
    NEBULA_ASSET_KIND_MATERIAL_TABLE,    /* banmt */
    NEBULA_ASSET_KIND_TEXTURE,           /* bti */
    NEBULA_ASSET_KIND_LAYOUT,            /* brlyt */
    NEBULA_ASSET_KIND_LAYOUT_ANIMATION,  /* brlan */
    NEBULA_ASSET_KIND_COUNT
};

/* The settings the game reads from the player's system (system_setting). */
enum {
    NEBULA_SETTING_LANGUAGE = 1,
    NEBULA_SETTING_SOUND_MODE,
    NEBULA_SETTING_WIDESCREEN,
};

/* A player's controller this frame, whatever the device (input_read). */
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
typedef struct NebulaInputSample {
    uint32_t buttons;       /* NEBULA_BUTTON_* held */
    float stick[2];         /* the move stick, -1..1: right, up */
    float pointer[2];       /* where the player points, -1..1 across the picture: right, down (pointer_on) */
    float roll[2];          /* the pointer's roll as the screen sees it, a unit vector: (1, 0) level */
    float distance;         /* from the screen (pointer_on) */
    float tilt[3];          /* the main hand's tilt in g (1 at rest): x out of its left side, y toward its far end, z up */
    float sub_tilt[3];      /* the sub hand's, the same axes */
    uint8_t pointer_on;     /* 1: the player points at the picture */
    uint8_t spin_presses;   /* spins since the last read: the main hand's, then the sub hand's */
    uint8_t sub_spin_presses;
    uint8_t reserved;
} NebulaInputSample;
typedef struct NebulaInputDevice {
    uint8_t connected;
    uint8_t nunchuk;        /* 1: a Nunchuk is attached */
    uint8_t battery;        /* 0 empty .. 4 full */
    uint8_t reserved;
} NebulaInputDevice;
/* What the game asks of a player's controller (input_control). */
enum {
    NEBULA_INPUT_RUMBLE = 1, /* value: how strongly, 0 off to 255 */
    NEBULA_INPUT_RELEASE,    /* the game lets the controller go (players 3 and 4 in play) */
    NEBULA_INPUT_RESET,      /* a scene change: from rest */
};

/* The HOME Menu (services.home_menu). NEBULA_HOME_MENU_POLL returns the choice so far: -1 none yet, 0 close, 1 leave the game, 2 reset.
   The host keeps the menu (its keys and controllers' buttons) and the game draws it: NEBULA_HOME_MENU_VIEW returns -1 while
   it is closed, else the selected answer in bits 0-7 and in bits 8-15 the choice it asks about (0 none: the list,
   whose rows are close, reset and leave; 1 leave or 2 reset: "are you sure?", whose answers are no and yes). */
enum {
    NEBULA_HOME_MENU_OPEN = 1, /* value: 1 on a 16:9 screen */
    NEBULA_HOME_MENU_POLL,
    NEBULA_HOME_MENU_CLOSE,    /* closed at once (a reset or power event) */
    NEBULA_HOME_MENU_VIEW,
    NEBULA_HOME_MENU_POINT,    /* value: the answer under the pointer, selected */
    NEBULA_HOME_MENU_CHOOSE,   /* value: the answer clicked */
};

/* What the settings' galaxy select asks of the scene (galaxy_travel). */
enum {
    NEBULA_GALAXY_TRAVEL_NONE = 0,
    NEBULA_GALAXY_TRAVEL_STAGE,       /* to `stage`, its star `star` */
    NEBULA_GALAXY_TRAVEL_GRAND_STARS, /* the save's Grand Stars 1 to `star`, then to `stage` (the Observatory) */
};

/* The picture's shape as the game draws it (NebulaFrameClock::display), from the output's shape the host latches at
   each frame (display_extent): the camera, the 2D screen and the layouts widen with it. */
typedef struct NebulaDisplay {
    int32_t wide;          /* 0: 16:9, nothing changes */
    float content_aspect;  /* width / height, 4:3 to 32:9 */
    int32_t screen_width;  /* MR::getScreenWidth in the 16:9 mode (832 at 16:9) */
    float layout_width;    /* the layout canvas (608 at 16:9 and narrower) */
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
    NEBULA_OPTION_PIXEL_LIT,         /* 1: materials with light terms light at the pixel */
    NEBULA_OPTION_INPUT_MODE,        /* NebulaInputMode: the input mode the player chose */
    NEBULA_OPTION_BLOOM,             /* 1 when the renderer's bloom is built: BloomEffect and BloomEffectSimple give it
                                        their values (NebulaRender::bloomMarker); 0 no bloom */
    NEBULA_OPTION_MENU_TEXT_SIZE,    /* the menus' text in percent; 0: as the screen's size calls for */
    NEBULA_OPTION_WINDOW_WIDTH_DP,   /* the window's width in density-independent units; 0: not known */
    NEBULA_OPTION_MODS_CHANGED,      /* the mods chosen differ from those this run started with: a reset starts the game again */
    NEBULA_OPTION_PREPARE_KIND,      /* what the preparation screen is for: 0 ray tracing, 1 mods' files, 2 the pipeline list */
    NEBULA_OPTION_PREPARATION,       /* a preparation the title waits for (NebulaPrepare): 0 none, 1 it opens, 2 it runs
                                        (the preparation screen shows) */
    NEBULA_OPTION_PREPARE_PROGRESS,  /* while NEBULA_OPTION_PREPARATION is 2: its progress, 0 to 10000, never going back */
    NEBULA_OPTION_DIAGNOSE_SCENE_LOADS, /* --diagnostics scene-loads: 1 logs what each scene change loads */
    NEBULA_OPTION_TOUCH_POINTER,       /* 0 no touch screen, 1 one, 2 the finger has the pointer, 3 and the game is on a
                                          menu: no pointer shows under a finger there */
    NEBULA_OPTION_GRAPHICS_BACKEND,    /* the renderer's backend, the same all session: 1 D3D12, 2 Vulkan (draw.native) */
};

/* post_register's result (draw.post, contract draw 15). */
enum {
    NEBULA_POST_OK = 0,        /* *program is the compiled program (never 0) */
    NEBULA_POST_COMPILE = 1,   /* the device's compiler refused the text: its first error in `error` */
    NEBULA_POST_NO_MOTION = 2, /* it reads motion and the frame has no motion vectors (temporal upscaling off) */
};

/* The movie player's sound (movie_audio_control). */
enum {
    NEBULA_MOVIE_AUDIO_OPEN = 1, /* value 1: mono output (both sides their mean at -3 dB) */
    NEBULA_MOVIE_AUDIO_CLOSE,
    NEBULA_MOVIE_AUDIO_RESET,    /* the waiting frames dropped, the volume at its target */
    NEBULA_MOVIE_AUDIO_VOLUME,   /* value 0-127 (the gain is its square), reached over `milliseconds` */
    NEBULA_MOVIE_AUDIO_RESUME,   /* after a pause: once two frames wait, a 5 ms fade in */
};

/* The sound clock (sound_clock). */
enum {
    NEBULA_AUDIO_START = 1, /* value: ticks a second; the host calls nebula_game_sound_frame once a tick */
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
    const char* name;          /* a plain file name in the save folder, or "luma/<plain name>": a mod's global file
                                  (services-save 7) in its luma folder, made at the first write, kept without backups */
    void* buffer;
    uint32_t size;
    uint32_t* length;          /* read: the file's length (may be NULL) */
    int32_t* status;  /* the host sets NEBULA_SAVE_PENDING at once and the result when done (a release store; the game loads it acquire) */
} NebulaSaveRequest;

/* Nebula's settings menu as the host has it now (settings_view), for the game to draw in its own windows and words.
   The host keeps the settings, their pages, the draft and its apply, and takes the keyboard and controllers while the
   menu is open; the game shows it (and takes the pointer, settings_input). Texts are the host's own, in English
   (UTF-8): the game names each in the played language (NebulaSettingsMenu).
   The view has no fixed sizes: the page names and every row of the open page are sent whole. The strings, the page names
   and the rows are the host's, valid until the next settings_view call; the game copies what it keeps. */

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
/* A model whose buttons are not its family's (galaxy::input::ButtonIconModel). */
enum {
    NEBULA_ICON_MODEL_STANDARD = 0,
    NEBULA_ICON_MODEL_SIXAXIS,  /* PLAYSTATION: Select and Start */
    NEBULA_ICON_MODEL_GAMECUBE, /* NINTENDO: face buttons, analog L and R, ZL and Z, Start */
    NEBULA_ICON_MODEL_STEAM,    /* XBOX: Steam button (HOME), quick access (CAPTURE), back buttons, trackpads */
    NEBULA_ICON_MODEL_DUALSENSE_EDGE, /* PLAYSTATION: back paddles and Fn buttons (GRIP) */
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
    NEBULA_ICON_DPAD,     /* place: 0 up, 1 down, 2 left, 3 right; KEYBOARD: the four keys of `pad_keys` */
    NEBULA_ICON_KEY,      /* place: NEBULA_KEY_* */
    NEBULA_ICON_CAPTURE,  /* Capture, the touchpad's click, Steam's quick access */
    NEBULA_ICON_CHAT,     /* Pro Controller 2 C, DualSense mute */
    NEBULA_ICON_GRIP,     /* place: 0 left, 1 right, 2 and 3 the Steam Controller's second pair, 4 and 5 the Edge's Fn buttons */
    NEBULA_ICON_TRACKPAD, /* place: 0 left, 1 right */
    NEBULA_ICON_STICK_AXIS, /* place: 0 up, 1 down, 2 left, 3 right; 4 on the right stick's */
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
    uint8_t model; /* NEBULA_ICON_MODEL_* */
    uint8_t glyphs[NEBULA_BUTTON_ICONS]; /* NEBULA_ICON_KEEP... */
    uint8_t places[NEBULA_BUTTON_ICONS];
    uint8_t wii_remote; /* a real Wii Remote is in use: the game's pictures of one show */
    uint8_t pad_keys[4]; /* KEYBOARD: the Control Pad's up, down, left and right keys (NEBULA_KEY_*) */
    /* KEYBOARD: the capital each layout key prints (from NEBULA_KEY_LAYOUT_FIRST; UCS-2), 0 where the US label shows */
    uint16_t layout[NEBULA_KEY_LAYOUT_KEYS];
    /* KEYBOARD: each icon on NEBULA_KEY_OTHER, its key's name in capitals (ASCII, at most 6), else empty */
    char other[NEBULA_BUTTON_ICONS][NEBULA_KEY_NAME_CHARS];
} NebulaButtonIcons;

/* What the line under the page says (with `status_number` or `status_text` where it has one). */
enum {
    NEBULA_SETTINGS_STATUS_HELP = 0,         /* the selected row's description (`help`) */
    NEBULA_SETTINGS_STATUS_CAPTURE_KEY,      /* a binding waits for a key or mouse button */
    NEBULA_SETTINGS_STATUS_CAPTURE_PAD,      /* a binding waits for a button, stick or trigger */
    NEBULA_SETTINGS_STATUS_MESSAGE,          /* a message for a moment (`status_text`) */
    NEBULA_SETTINGS_STATUS_DLSS_ASK,         /* download DLSS? yes: NEBULA_SETTINGS_APPLY, its license: NEBULA_SETTINGS_REVERT,
                                                no: NEBULA_SETTINGS_CLOSE */
    NEBULA_SETTINGS_STATUS_DLSS_INSTALLING,  /* DLSS downloads (`status_number` percent); NEBULA_SETTINGS_CLOSE stops it */
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
    NEBULA_SETTINGS_STATUS_MODS_CODE_ASK,    /* a mod turned on brings code (`status_text`: its name): allow it? yes:
                                                APPLY, no: CLOSE */
    NEBULA_SETTINGS_STATUS_XESS_ASK,         /* download XeSS? yes: APPLY, its license: REVERT, no: CLOSE */
    NEBULA_SETTINGS_STATUS_XESS_INSTALLING,  /* XeSS downloads (`status_number` percent); CLOSE stops it */
    NEBULA_SETTINGS_STATUS_XESS_RESTART,     /* XeSS installed: restart now? yes: APPLY, later: CLOSE */
    NEBULA_SETTINGS_STATUS_XESS_FAILED,      /* XeSS was not installed */
    NEBULA_SETTINGS_STATUS_LICENSE,          /* the maker's license: `long_text` one page, `status_number` its number from 1
                                                (-1 loading, -2 not reachable); next: APPLY, before: REVERT, back: CLOSE */
    NEBULA_SETTINGS_STATUS_PREPARING,        /* the settings in use want pipelines still being made (`status_number` percent) */
    NEBULA_SETTINGS_STATUS_ARRANGE,          /* the phone's on-screen controls are dragged where they go (the page under it
                                                hidden): save: APPLY, every control where the game places it: REVERT,
                                                cancel: CLOSE */
    NEBULA_SETTINGS_STATUS_MOD_ABOUT,        /* a mod's description and pictures, a page at a time: `long_text` the page's
                                                text, `picture` its picture (either may be empty), `status_number` its
                                                number from 1, or for a picture page whose picture is not shown
                                                -(number * 4 + 1 it loads, 2 it can't be read, 3 it is missing);
                                                next: APPLY, before: REVERT, back: CLOSE */
    NEBULA_SETTINGS_STATUS_MOD_REMOVE_ASK,   /* remove the mod whose page is open? `status_number` 1: it goes to the
                                                recycle bin, 0: it is deleted. yes: APPLY, no: CLOSE */
    NEBULA_SETTINGS_STATUS_MOD_FORGET_ASK,   /* forget what a removed mod kept (its numbers and saved data)?
                                                `status_text` its id. yes: APPLY, no: CLOSE */
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
    NEBULA_SETTINGS_INPUT_TOUCH,       /* the finger: no key or button hints */
};

typedef struct NebulaSettingsRow {
    const char* label;
    const char* value;
    uint32_t flags; /* NEBULA_SETTINGS_ROW_* */
    uint16_t slider_steps;
    uint16_t slider_at;
} NebulaSettingsRow;

typedef struct NebulaSettingsView {
    uint32_t open;
    uint32_t page;
    uint32_t page_count;
    const char* const* pages; /* `page_count` names */
    uint32_t controls_page;   /* the page whose rows are bindings (its hints differ) */
    uint32_t input;           /* what the player last used, for the hints: NEBULA_SETTINGS_INPUT_* */
    /* Changed by the host whenever anything else in the view changes: views with equal revisions are identical. */
    uint32_t revision;
    uint32_t total_rows;      /* the page's rows: `rows` holds all of them */
    const NebulaSettingsRow* rows;
    uint32_t selected;        /* counted from the page's first row */
    uint32_t selected_button; /* the selected row's, when it has buttons */
    /* The MODS page's lists (off, on): their lengths. */
    uint32_t mod_list_count[2];
    const char* help;
    uint32_t status; /* NEBULA_SETTINGS_STATUS_* */
    int32_t status_number;
    const char* status_text;
    const char* long_text; /* a license's page */
    /* A picture over the page (NEBULA_SETTINGS_STATUS_MOD_ABOUT): RGBA8 rows of picture_width texels, tightly packed,
       straight alpha; NULL when there is none. The same pointer holds the same picture; it stays until the revision
       changes. */
    const uint8_t* picture;
    uint32_t picture_width;
    uint32_t picture_height;
    uint32_t help_as_is; /* 1: `help` is already in the played language (a mod's refusal, as the validator words it):
                            shown as is, not translated */
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

/* The channels a target pass's picture carries for the textures that sample it (NEBULA_PASS_DRAW's target). */
typedef enum NebulaTargetChannels {
    NEBULA_TARGET_RGBA = 0,  /* colour and alpha */
    NEBULA_TARGET_ALPHA = 1, /* alpha, read in every channel */
    NEBULA_TARGET_RED = 2,   /* red, read in every channel */
    NEBULA_TARGET_RGB = 3    /* colour, alpha read as one */
} NebulaTargetChannels;

/* The channels a clear writes (NEBULA_PASS_CLEAR). */
typedef enum NebulaClearChannels {
    NEBULA_CLEAR_COLOR = 1u,
    NEBULA_CLEAR_ALPHA = 2u,
    NEBULA_CLEAR_DEPTH = 4u
} NebulaClearChannels;

/* A point of the picture the game asks about (NEBULA_PASS_PROBE). */
typedef struct NebulaScreenProbe {
    float x, y;
    float user_x, user_y, user_z;
    float value; /* the depth, 0 near to 1 far */
} NebulaScreenProbe;

typedef struct NebulaHostServices {
    uint32_t version;
    uint32_t size;
    void* user;

    /* One line of the game's log, without a trailing newline. */
    void (*log)(void* user, const char* text);
    /* The game cannot go on (a failed load or check, a native-layer error): the host reports `text` with
       its own diagnostics and ends the process. Does not return. */
    void (*fatal)(void* user, const char* text);
    /* The module's own bookkeeping memory, outside its arenas. */
    void* (*heap_alloc)(void* user, size_t size);
    void* (*heap_realloc)(void* user, void* data, size_t size);
    void (*heap_free)(void* user, void* data);

    /* The game's files: the file or folder at `path` ("/StageData/EggStarGalaxy", an archive's folder: from the root, '/' at a name's start
       returning to it, "." and ".." steps, letters in either case, a name followed by '/' a folder): 0 when there is
       none. */
    int (*asset_find)(void* user, const char* path, NebulaAssetEntry* entry);
    /* As asset_find, the game's own file or folder under any mod's: 0 when the game has none. */
    int (*asset_find_game)(void* user, const char* path, NebulaAssetEntry* entry);
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
    uint32_t (*input_read)(void* user, uint32_t player, NebulaInputDevice* device, NebulaInputSample* sample);
    void (*input_control)(void* user, uint32_t player, uint32_t what, uint32_t value);
    /* Player `player`'s remote speaker: `count` 16-bit samples at 6 kHz, as the game mixes them. */
    void (*speaker_samples)(void* user, uint32_t player, const int16_t* samples, uint32_t count);

    /* Audio, on the host's audio clock (nebula_game_sound_frame). voices_setup hands over the voices and the four effect lines (they stay the
       game's; the mixer reads and advances them in voices_mix).
       voices_mix mixes one subframe at `gain`; voices_frame ends a frame: the subframes mixed since the last call, with
       the movie's sound for them, go to the output. */
    void (*voices_setup)(void* user, NebulaVoice* voices, uint32_t count, const NebulaEffectLine* effects);
    void (*voices_mix)(void* user, float gain, uint32_t used); /* `used`: voices past this have never played */
    void (*voices_frame)(void* user);
    /* The movie player's sound, on the game thread: movie_audio queues one frame's audio record (its stereo sample count,
       u32, then the samples, PCM16, left and right in turn, of the track played) for the mixer; 0 when the queue is full (null asks only that).
       sound at 32 kHz; 0 when NEBULA_MOVIE_AUDIO_QUEUE_FRAMES already wait (nothing taken). A null record asks for the frames there is room for.
       movie_audio_control: NEBULA_MOVIE_AUDIO_*. movie_audio_position: where the output has reached in the sound since the movie opened or was
       reset, in frames and the fraction of the one being heard. */
    int (*movie_audio)(void* user, const void* record, uint32_t size);
    void (*movie_audio_control)(void* user, uint32_t what, int32_t value, int32_t milliseconds);
    double (*movie_audio_position)(void* user);
    void (*sound_clock)(void* user, uint32_t what, uint32_t value);

    /* The HOME Menu (NEBULA_HOME_MENU_*). */
    int (*home_menu)(void* user, uint32_t what, uint32_t value);



    /* Game state the host's controls follow (NEBULA_CONTEXT_*), reported when it changes. */
    void (*game_context)(void* user, uint32_t what, int32_t value);

    /* The game closes, or starts again from its boot, with pending saves written. Do not return. */
    void (*app_exit)(void* user);
    void (*app_restart)(void* user);
    /* The game's memory is ready, and nothing of it has run: the mods' code starts. */
    void (*mods_start)(void* user);
    /* What the mods add to a folder the game has: asset_child doesn't list these. */
    int (*asset_added_child)(void* user, const char* path, uint32_t child, NebulaAssetEntry* out);

    /* Once per game scene frame (before its 3D): a travel the player asked for in the settings' galaxy select
       (NEBULA_GALAXY_TRAVEL_*), with `stage` (`capacity` bytes) and `star` filled, or NEBULA_GALAXY_TRAVEL_NONE. */
    int (*galaxy_travel)(void* user, char* stage, uint32_t capacity, int32_t* star);

    /* The output's shape latched at this frame, after frame_begun: width << 16 | height (only the ratio counts), or 0
       for 16:9. */
    uint32_t (*display_extent)(void* user);
    /* A Nebula option (NEBULA_OPTION_*), read when the game needs it. */
    uint32_t (*option)(void* user, uint32_t what);
    /* The stage being played (MR::getCurrentStageName), before each 3D scene's begin marker. */
    void (*game_stage)(void* user, const char* name);
    /* A movie (MovieStream): its disc path and NEBULA_MOVIE_OPENING when it opens (with its cooked file, NMOV, as
       mapped, once the game has it), each frame it draws (the frame within the movie, its frame rate, the file, the
       picture's `width` x `height`), NEBULA_MOVIE_NONE when it closes. The host decodes the frame (or a replacement
       video's frame of the same time) into the planes NEBULA_TEXTURE_MOVIE_Y, _U and _V the movie quad samples. */
    void (*movie_frame)(void* user, const char* name, int32_t frame, float frame_rate, const void* data, uint64_t size,
                        uint32_t width, uint32_t height);
    /* What the player is doing, for Discord: the galaxy and the star (UTF-16, the game's own text in the played
       language; either may be NULL) and the Power Stars collected (-1: not in a file). Reported when it changes. */
    void (*presence)(void* user, const uint16_t* galaxy, const uint16_t* star, int32_t power_stars);
    /* An achievement the save data meets (galaxy::achievements' id): 1 when it was not unlocked before, with its
       English name (UTF-16, NUL-terminated, at most `capacity` units; `name` may be NULL); else 0. */
    int (*achievement)(void* user, const char* id, uint16_t* name, uint32_t capacity);
    /* The day an achievement was unlocked in the player's time zone (year * 10000 + month * 100 + day); 0 when it is
       locked; 1 when it is unlocked on a day that cannot be read. */
    int32_t (*achievement_date)(void* user, const char* id);
    /* The achievements the running mods add (their luma.json "achievements"): how many, for this run. */
    int32_t (*mod_achievement_count)(void* user);
    /* One of them by its place: its key for achievement_date ("mod:<mod>/<id>"), its name, what earns it and its mod's
       name (UTF-8, in the played language, kept for the run), and in `hidden` 1 when the board lists it only once
       unlocked. 0 past the last. */
    int32_t (*mod_achievement)(void* user, int32_t index, const char** key, const char** name, const char** description,
                               const char** mod, int32_t* hidden);
    /* A mod achievement unlocked since the last call (a plugin's unlock_achievement, or the event it waits for), for
       its pop-up: its place; -1 when none. */
    int32_t (*mod_achievement_unlocked)(void* user);
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
    void (*galaxy_select_name)(void* user, uint32_t index, uint32_t star, const uint16_t* name);    /* `hd`: a texture record, the image handed over at `image` as the game painted it larger; the renderer draws it in
       that image's place while the image's texels are what they are now (a texture pack's name for them wins). NULL:
       what was handed over for `texture` goes. The renderer takes a copy. */
    void (*texture_hd)(void* user, uint32_t texture, const void* hd);
    /* The Miis in the player's Mii library (any number; nebula_game_mii_changed says when it changed). */
    uint32_t (*mii_count)(void* user);
    /* The library's Mii at `place` (0 to mii_count - 1): 1 when there is one (`mii` filled when not NULL), 0 past
       its Miis. */
    int32_t (*mii_record)(void* user, uint32_t place, NebulaMii* mii);
    /* Nanoseconds on a steady clock from an arbitrary start (never 0): the game's durations and pacing. */
    uint64_t (*clock_ns)(void* user);
    /* The local date and time now: nanoseconds from 2000-01-01 00:00 local time (the game's clock counts from
       there; the game's saves store times in its unit). */
    int64_t (*calendar_ns)(void* user);
    /* Deterministic test mode only (NULL otherwise; GALAXY_TEST_DETERMINISTIC): the game's time is its frames. The host
       takes one frame's audio ticks and clock and returns, so frames follow the game's own pace and every run of a build is the same. The
       game's clock advances one frame per call. */
    void (*test_frame_step)(void* user);
    /* A model file (NMDL) the game loaded: its meshes get ids `*first_mesh` on, in file order. The file stays mapped
       until model_free. Returns the model's id. */
    uint32_t (*model_register)(void* user, const void* file, uint32_t* first_mesh);
    void (*model_free)(void* user, uint32_t model);
    /* Vertices and indices a site built once. The renderer takes a copy. */
    uint32_t (*mesh_register)(void* user, const NebulaFrameMesh* mesh);
    void (*mesh_free)(void* user, uint32_t mesh);
    /* An image as the game has it. The renderer takes a copy before this returns. */
    uint32_t (*texture_register)(void* user, const NebulaTextureImage* image);
    /* The image's bytes changed: the renderer takes a copy before this returns. */
    void (*texture_update)(void* user, uint32_t texture, const void* bytes, uint32_t size);
    void (*texture_free)(void* user, uint32_t texture);
    /* A texture a pass draws into: empty until then. */
    uint32_t (*texture_target)(void* user);
    /* The textures a draw reads and how, by what they are. */
    uint32_t (*texture_table)(void* user, const NebulaFrameTexture* list, uint32_t count);
    /* A material's constants that do not change; the renderer takes a copy. */
    uint32_t (*material_register)(void* user, const float (*rows)[4], uint32_t vertex_rows, uint32_t pixel_rows);
    /* `desc->input_count` NebulaLayoutInput at `inputs` (any count); the renderer takes copies of both. */
    uint32_t (*pipeline_register)(void* user, const NebulaPipelineDesc* desc, const NebulaLayoutInput* inputs);
    /* The frame's list. The renderer owns it until it sets `released`; nothing waits. */
    void (*submit_frame)(void* user, NebulaFrameList* list);
    /* `channel`'s latest finished probes with their values, at most `capacity`; how many (0 before any has finished).
       Never waits for the GPU. */
    uint32_t (*probe_results)(void* user, uint32_t channel, NebulaScreenProbe* probes, uint32_t capacity);
    /* The players input_read serves (0 to this - 1), fixed for the session. */
    uint32_t (*input_players)(void* user);
    /* The player chose to pause the game when its window loses focus (runtime settings, focus_loss) and the window has: 1 once
       per loss, then 0. The game asks only while it could open its pause menu, and opens it as the pause button does. A
       request the game did not take (a cutscene, a wipe) lapses when the window is in front again. */
    int (*take_pause_request)(void* user);
    /* draw.post (contract draw 15): a mod's post program compiled for this device. `text` is the cooked program's text
       (LumaLink's part: its params over post_params, the mod's `float4 post(PostIn i)`), `size` bytes, not NUL-ended;
       the host adds its prelude (PostIn, scene, scene_texel, depth, motion, post_params, the bindings) and its entry.
       `reads`: LUMA_DRAW_POST_READS_*. NEBULA_POST_* and *program; `error` holds the compiler's first error, NUL-ended
       within `error_capacity`. Game thread. */
    int32_t (*post_register)(void* user, const void* text, uint32_t size, uint32_t reads, uint32_t* program, char* error,
                             uint32_t error_capacity);
    /* The program freed, after the lists that name it are released. Game thread. */
    void (*post_free)(void* user, uint32_t program);
} NebulaHostServices;


enum {
    NEBULA_MOVIE_NONE = -1,
    NEBULA_MOVIE_OPENING = -2,
};

/* The movie quad's planes (movie_frame), ids no image has: images number from 1 up, render targets have the top bit. */
#define NEBULA_TEXTURE_MOVIE_Y 0x7FFFFF00u
#define NEBULA_TEXTURE_MOVIE_U 0x7FFFFF01u
#define NEBULA_TEXTURE_MOVIE_V 0x7FFFFF02u

enum {
    NEBULA_CONTEXT_STAR_POINTER_MODE = 1, /* StarPointerOnOffController's mode (-1 none): menus and play */
    NEBULA_CONTEXT_TILT_SECTION,          /* 0 none, 1 Mario on a Star Ball, 2 on a Ray: tilt steers */
    NEBULA_CONTEXT_FRAME_GENERATION,      /* each frame: 1 while the game shows play (no menu, window or full-screen
                                             page): DLSS Frame Generation may add frames (NebulaFrameGen) */
    NEBULA_CONTEXT_CAMERA_WATER,          /* each frame: the camera's depth under water in units, at least 1 (0: out
                                             of water; MR::isCameraInWater): ray tracing's water medium */
    NEBULA_CONTEXT_TOUCH_TARGET,          /* each frame, on a touch screen, for each thing in reach the pointer grabs (a pull
                                             star): its circle on the screen, bits 0-11 x and 12-23 y (0..4095 across), bits
                                             24-31 the radius (255: the screen's height) */
    NEBULA_CONTEXT_SCREEN_COVERED,        /* each frame: 1 while a system or scene wipe is fully closed (MR::isScreenCoveredByWipe):
                                             the host holds the controls' context through what changes under it */
};

#define NEBULA_MOVIE_AUDIO_QUEUE_FRAMES 20

/* Returns NEBULA_GAME_API_VERSION, or 0 if the host's services are too old. */
NEBULA_GAME_API uint32_t nebula_game_attach(const NebulaHostServices* host);
/* Runs the game on the calling thread, the game thread. Does not return. */
NEBULA_GAME_API void nebula_game_run(void);
/* The game's half of the plugin interface. */
NEBULA_GAME_API const struct NebulaModApi* nebula_game_mod_api(void);
/* The audio clock, while started (NEBULA_AUDIO_START): the game mixes its next frame (voices_mix, voices_frame). */
NEBULA_GAME_API void nebula_game_sound_frame(void);
/* Whether the game takes a Mii into its database (its parts and values in range); the host keeps only these. Pure, any
   thread. */
NEBULA_GAME_API uint32_t nebula_game_mii_valid(const NebulaMii* mii);
/* The host's Mii database changed (Miis imported while the game runs): the game reads its slots again. */
NEBULA_GAME_API void nebula_game_mii_changed(void);
/* The module's type rows as JSON (contract types; the host's --luma-describe), given to `write` in pieces in order.
   Needs no attach; reads only the rows. */
typedef void (*NebulaDescribeWrite)(void* user, const char* text, uint32_t length);
NEBULA_GAME_API void nebula_game_luma_describe(NebulaDescribeWrite write, void* user);
#ifdef __cplusplus
}
#endif

#endif
