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

#define NEBULA_GAME_API_VERSION 4u

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
};

/* A Wii Remote's connection: its EEPROM's first 0x2A bytes (DPD and accelerometer calibration), the attached extension
   (0 none, 1 Nunchuk: the SDK's WPAD_DEV_* values) with its 32-byte calibration block, and the battery level. */
typedef struct NebulaWiimoteConnection {
    uint8_t eeprom[0x2A];
    uint8_t extension_calibration[32];
    uint32_t extension;
    uint8_t battery;
} NebulaWiimoteConnection;

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
    /* An EFB pixel (type 0: colour, 1: depth), as GXPeekARGB/GXPeekZ read it. */
    uint32_t (*gx_peek_efb)(void* user, uint16_t x, uint16_t y, uint32_t type);

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

    /* THP: one movie frame's video data (the game's THPVideoDecode) into its Y, U and V planes in game memory, which
       must hold the result before the renderer reads them; 0 on success, as THPVideoDecode returns. */
    int32_t (*thp_video_decode)(void* user, const void* frame, void* y, void* u, void* v);

    /* The game wrote [physical, physical + size) for the GPU: the console's cache flushes (DCFlushRange,
       DCStoreRange) and DMA copies. The renderer drops what it holds of that memory. Called on the simulation
       thread; the host's own writes to game memory (disc reads) need no call. */
    void (*gpu_coherent)(void* user, uint32_t physical, uint32_t size);
} NebulaHostServices;

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
/* A Wii Remote connected (with its calibration) or disconnected. */
NEBULA_GAME_API void nebula_game_wpad_connect(uint32_t chan, int connected, const NebulaWiimoteConnection* connection);
/* A Wii Remote input report, from its id (0x30, 0x31, 0x33 or 0x37 as the data format asks), extension bytes decrypted;
   one per report the remote sends (100 per second). */
NEBULA_GAME_API void nebula_game_wpad_report(uint32_t chan, const uint8_t* report, size_t size);

#ifdef __cplusplus
}
#endif

#endif
