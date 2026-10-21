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

#define NEBULA_GAME_API_VERSION 13u

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
    uint8_t battery;
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

/* The HOME Menu (hbm). NEBULA_HBM_POLL returns the choice so far: -1 none yet, 0 close, 1 leave the game, 2 reset. */
enum {
    NEBULA_HBM_OPEN = 1, /* value: 1 on a 16:9 screen */
    NEBULA_HBM_POLL,
    NEBULA_HBM_CLOSE,    /* closed at once (a reset or power event) */
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
};

/* The picture's shape as the game draws it (__NebulaDisplay), from the output's shape the host latches at each retrace
   (display_extent), as v1.0.1's dynamic aspect: the camera, the 2D screen and the layouts widen with it. */
typedef struct NebulaDisplay {
    int32_t wide;          /* 0: the Wii's 16:9, nothing changes */
    float content_aspect;  /* width / height, 4:3 to 32:9 */
    int32_t screen_width;  /* MR::getScreenWidth in the 16:9 mode (832 at 16:9) */
    float canvas_width;    /* the NW4R layout canvas (608 at 16:9 and narrower) */
    float vertical_fit;    /* the layouts' height scale under 16:9 (1 at 16:9 and wider) */
} NebulaDisplay;

/* Nebula options the game follows (option). */
enum {
    NEBULA_OPTION_TEXT_SPEED = 1, /* 1 to 3: text steps revealed per frame */
    NEBULA_OPTION_TEST_DUMP_LAYOUTS, /* tests: 1 logs each layout's pane tree once (GALAXY_TEST_DUMP_LAYOUTS) */
    NEBULA_OPTION_DRAW_IDENTITIES, /* 1 while the renderer reads host markers (__NebulaGXHostMarker) */
    NEBULA_OPTION_RT_SHADOW_CASTERS, /* 1 while the ray tracer lights the scene: clipped actors whose shadows can
                                        reach the view stay drawn (ClippingActorInfo::judgeShadowDraw) */
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
} NebulaHostServices;

enum {
    NEBULA_POWER_SHUTDOWN = 1, /* the console turns off: the game closes */
    NEBULA_POWER_RESTART,      /* the game starts again from its boot */
    NEBULA_POWER_MENU,         /* back to the Wii Menu: the game closes */
    NEBULA_POWER_REBOOT,       /* the console restarts: the game closes */
};

enum {
    NEBULA_CONTEXT_STAR_POINTER_MODE = 1, /* StarPointerOnOffController's mode (-1 none): menus and play */
    NEBULA_CONTEXT_TILT_SECTION,          /* 0 none, 1 Mario on a Star Ball, 2 on a Ray: tilt steers */
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

#ifdef __cplusplus
}
#endif

#endif
