/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's capture services (contract services-audio 7; plan 2.6 layers 5 and 6): the player's microphone
   (`audio.microphone` 1.0) and camera (`capture.camera` 1.0), each its own permission of the same name, granted on
   the consent screen and checked when the mod binds (services-registry 5.2). The game's own picture and mix need no
   permission: they are draw's and audio.tap's. Every call is the game thread's and never blocks: the device opens
   on a thread of its own and what it hears or sees comes as events at a frame's start. A mod that stops closes them
   the same frame. Where the platform or the device has none: open answers LUMA_E_ABSENT, or (the device found
   missing or refused on its own thread) ENDED with LUMA_E_ABSENT follows. */
#ifndef LUMA_CAPTURE_H
#define LUMA_CAPTURE_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_MICROPHONE_NAME "audio.microphone"
#define LUMA_MICROPHONE_MAJOR 1u
#define LUMA_MICROPHONE_MINOR 0u
#define LUMA_CAMERA_NAME "capture.camera"
#define LUMA_CAMERA_MAJOR 1u
#define LUMA_CAMERA_MINOR 0u

#define LUMA_MICROPHONE_BLOCK_FRAMES 1024u /* frames in a SAMPLES block */
#define LUMA_CAMERA_WIDTH_MOST 3840u
#define LUMA_CAMERA_HEIGHT_MOST 2160u
#define LUMA_CAMERA_FPS_MOST 60u

/* Events (LumaEvent.kind) of both; `handle` is the open's. */
typedef enum LumaCaptureEvent {
    LUMA_CAPTURE_EVENT_SAMPLES = 1, /* microphone: data, LUMA_MICROPHONE_BLOCK_FRAMES frames of float (the desc's
                                       channels, interleaved; valid during the call); value: the frames; flags: the
                                       rate; status LUMA_E_BUSY when blocks before it were lost */
    LUMA_CAPTURE_EVENT_FRAME = 2,   /* camera: data, a LumaCameraFrame (valid during the call), the newest picture
                                       (pictures the mod had no frame for are skipped: status LUMA_E_BUSY) */
    LUMA_CAPTURE_EVENT_ENDED = 3    /* the device stopped on its own: LUMA_E_ABSENT (none, or the player's system
                                       refused it), LUMA_E_IO (unplugged, taken by another program). The handle is
                                       closed after it. */
} LumaCaptureEvent;

typedef struct LumaMicrophoneDesc {
    uint32_t size;     /* sizeof(LumaMicrophoneDesc) */
    uint32_t rate;     /* 8000 to 192000 Hz (resampled from the device's); 0: 48000 */
    uint32_t channels; /* 1 or 2 */
    uint32_t reserved;
} LumaMicrophoneDesc;

typedef struct LumaMicrophoneService {
    LumaServiceTable base;
    /* Opens the player's default microphone: SAMPLES to fn (not null) from a frame after. One a mod (a 2nd:
       LUMA_E_LIMIT, reported). A desc of the wrong size or out of range, a null fn or out: LUMA_E_ARGUMENT, reported;
       no microphone on this platform: LUMA_E_ABSENT. */
    int32_t (*open)(const LumaMod* mod, const LumaMicrophoneDesc* desc, LumaEventFn fn, void* user, LumaHandle* out);
    /* Closes it; no event after. A handle not the mod's: LUMA_E_HANDLE, reported. */
    int32_t (*close)(const LumaMod* mod, LumaHandle microphone);
} LumaMicrophoneService;

typedef struct LumaCameraDesc {
    uint32_t size;   /* sizeof(LumaCameraDesc) */
    uint32_t width;  /* wanted, to LUMA_CAMERA_WIDTH_MOST; 0: the camera's own (FRAME says what it gives) */
    uint32_t height; /* to LUMA_CAMERA_HEIGHT_MOST; 0 with width 0 */
    uint32_t fps;    /* wanted, to LUMA_CAMERA_FPS_MOST; 0: the camera's own */
} LumaCameraDesc;

/* A FRAME's data: RGBA, 8 bits each, rows top first (draw's texture from pixels takes it as is). */
typedef struct LumaCameraFrame {
    uint32_t size; /* sizeof(LumaCameraFrame) */
    uint32_t width;
    uint32_t height;
    uint32_t stride;  /* bytes from a row to the next: width × 4 */
    uint64_t time_ns; /* when the camera took it, steady clock */
    const uint8_t* pixels;
} LumaCameraFrame;

typedef struct LumaCameraService {
    LumaServiceTable base;
    /* Opens the player's first camera: FRAME to fn (not null), at most one a game frame. One a mod (a 2nd:
       LUMA_E_LIMIT, reported). A desc of the wrong size or out of range, a null fn or out: LUMA_E_ARGUMENT,
       reported; no camera on this platform: LUMA_E_ABSENT. */
    int32_t (*open)(const LumaMod* mod, const LumaCameraDesc* desc, LumaEventFn fn, void* user, LumaHandle* out);
    /* Closes it; no event after. A handle not the mod's: LUMA_E_HANDLE, reported. */
    int32_t (*close)(const LumaMod* mod, LumaHandle camera);
} LumaCameraService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_CAPTURE_H */
