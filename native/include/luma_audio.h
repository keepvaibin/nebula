/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's audio service (contract services-audio; plan 2.6). `audio` 1.0, no permission: a mod plays its cooked
   sound effects (3.7's NSFX: `layers/<l>/assets/<path>.sound.json`, cooked at the game's start) and pushes sound it
   makes itself (a synthesiser, a decoded movie, a voice chat) as a stream. Both go into the game's own mixer, after
   the game's voices, on four buses that follow the game's volume settings: effects and ui (Sound effects), voice
   (Voices), music (Music); the master volume and the output device are the game's. Every call is the game thread's,
   never blocks, and what waits is an event at a frame's start (services-registry 6). No mod code ever runs on the
   audio thread: a stream is a ring the mod fills, never a callback. A mod that stops is silent the same audio frame.
   Later layers (plan 2.6): the game's own sounds by name, positioned voices, music replaced, the mix tapped, capture. */
#ifndef LUMA_AUDIO_H
#define LUMA_AUDIO_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_AUDIO_NAME "audio"
#define LUMA_AUDIO_MAJOR 1u
#define LUMA_AUDIO_MINOR 0u

#define LUMA_AUDIO_VOICES_MOST 32u      /* voices a mod plays at once */
#define LUMA_AUDIO_STREAMS_MOST 4u      /* streams a mod holds at once */
#define LUMA_AUDIO_NAME_MOST 255u       /* bytes of a sound's name */
#define LUMA_AUDIO_RATE_LEAST 8000u     /* a stream's rate, Hz */
#define LUMA_AUDIO_RATE_MOST 192000u
#define LUMA_AUDIO_RING_MS 500u         /* what a stream holds pushed and not yet played */
#define LUMA_AUDIO_NEED_MS 100u         /* under this much, a stream asks for more (NEED) */
#define LUMA_AUDIO_VOLUME_MOST 4.0f     /* a voice's own volume (linear; 1: as cooked) */
#define LUMA_AUDIO_FADE_MOST_MS 10000u  /* voice_stop's longest fade */

/* The buses a voice or stream plays on (an NSFX names its own; LUMA_AUDIO_BUS_SOUND keeps it). */
typedef enum LumaAudioBus {
    LUMA_AUDIO_BUS_EFFECTS = 0, /* the game's Sound effects volume */
    LUMA_AUDIO_BUS_UI = 1,      /* Sound effects too; kept apart for the mod's own bus_volume */
    LUMA_AUDIO_BUS_VOICE = 2,   /* Voices */
    LUMA_AUDIO_BUS_MUSIC = 3,   /* Music */
    LUMA_AUDIO_BUS_SOUND = 255  /* play: the sound's own bus */
} LumaAudioBus;

/* Events (LumaEvent.kind) to the fn given at play or stream_open; `handle` is the voice's or the stream's. */
typedef enum LumaAudioEvent {
    LUMA_AUDIO_EVENT_ENDED = 1, /* a voice's, once: it played to its end (status LUMA_OK), or it was dropped for a
                                   voice of the mod's of higher priority (status LUMA_E_LIMIT, reported: Audio.Dropped).
                                   The handle is closed after it. A voice stopped by voice_stop has no event. */
    LUMA_AUDIO_EVENT_NEED = 2   /* a stream's: less than LUMA_AUDIO_NEED_MS waits to be played; value: the frames
                                   stream_push takes now. Once a frame at most, and again only after a push. */
} LumaAudioEvent;

/* How play plays a sound (null: as cooked). */
typedef struct LumaAudioPlay {
    uint32_t size;    /* sizeof(LumaAudioPlay) */
    float volume;     /* times the sound's own (0 to LUMA_AUDIO_VOLUME_MOST) */
    float pitch;      /* times the sound's own pick in its pitch range (0.25 to 4) */
    uint32_t bus;     /* LumaAudioBus; LUMA_AUDIO_BUS_SOUND: the sound's */
    int32_t loop;     /* -1: as the sound (its loop points); 0: once; 1: its loop points, or the whole sound */
    uint32_t reserved;
} LumaAudioPlay;

/* What stream_open makes. */
typedef struct LumaAudioStreamDesc {
    uint32_t size;      /* sizeof(LumaAudioStreamDesc) */
    uint32_t rate;      /* frames a second, LUMA_AUDIO_RATE_LEAST to LUMA_AUDIO_RATE_MOST (resampled to the output's) */
    uint32_t channels;  /* 1 or 2 (interleaved: left, right) */
    uint32_t bus;       /* LumaAudioBus, not LUMA_AUDIO_BUS_SOUND */
    float volume;       /* 0 to LUMA_AUDIO_VOLUME_MOST */
    uint32_t reserved;
} LumaAudioStreamDesc;

typedef struct LumaAudioService {
    LumaServiceTable base;
    /* Plays the cooked sound `name`: "<path>", the mod's own (`layers/<l>/assets/<path>.sound.json`), or
       "<id>:<path>", another mod's. LUMA_OK and *out (the voice); ENDED to fn (null: no event) when it ends. A sound
       not there: LUMA_E_NOT_FOUND (an answer); a file that is not a cooked sound: LUMA_E_FORMAT, reported
       (Audio.NotASound); a 33rd voice: the mod's voice of the lowest priority (the new one's included; the oldest
       of equals) ends, ENDED LUMA_E_LIMIT, reported; when that is the new one, LUMA_E_LIMIT, reported. A null name or
       out, a name past LUMA_AUDIO_NAME_MOST, a `how` of the wrong size or out of range: LUMA_E_ARGUMENT, reported. */
    int32_t (*play)(const LumaMod* mod, const char* name, const LumaAudioPlay* how, LumaEventFn fn, void* user,
                    LumaHandle* out);
    /* A playing voice's volume and pitch (as LumaAudioPlay's), ramped over 10 ms. A handle not the mod's playing
       voice: LUMA_E_HANDLE, reported; out of range: LUMA_E_ARGUMENT, reported. */
    int32_t (*voice_set)(const LumaMod* mod, LumaHandle voice, float volume, float pitch);
    /* Stops a voice over `fade_ms` (0: 10 ms, so nothing clicks; at most LUMA_AUDIO_FADE_MOST_MS). No event after;
       the handle is closed at once. A handle not the mod's voice: LUMA_E_HANDLE, reported. */
    int32_t (*voice_stop)(const LumaMod* mod, LumaHandle voice, uint32_t fade_ms);
    /* The mod's own share of a bus (0 to 1; 1 at its start), on top of the player's setting for it: every voice and
       stream of the mod on that bus, ramped over 10 ms. */
    int32_t (*bus_volume)(const LumaMod* mod, uint32_t bus, float volume);
    /* A stream the mod fills with stream_push. LUMA_OK and *out; NEED follows at once (it is empty). A 5th stream:
       LUMA_E_LIMIT, reported; a desc of the wrong size or out of range, a null desc or out: LUMA_E_ARGUMENT,
       reported. */
    int32_t (*stream_open)(const LumaMod* mod, const LumaAudioStreamDesc* desc, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* Queues `frames` frames of float samples (channels each, interleaved; -1 to 1, louder is clipped at the
       output). Returns the frames taken: all, or as many as the ring has room for (LUMA_AUDIO_RING_MS in all). A
       sample that is not a number is silence, reported once a push (Audio.NotANumber). A null samples with frames:
       LUMA_E_ARGUMENT, reported; a handle not the mod's stream: LUMA_E_HANDLE, reported. A stream that runs dry
       plays silence and goes on when pushed again. */
    int64_t (*stream_push)(const LumaMod* mod, LumaHandle stream, const float* samples, uint64_t frames);
    /* Ends a stream: `drain` 1 plays what it holds first, 0 fades it out over 10 ms. No event after; the handle is
       closed at once. A handle not the mod's stream: LUMA_E_HANDLE, reported. */
    int32_t (*stream_close)(const LumaMod* mod, LumaHandle stream, uint32_t drain);
} LumaAudioService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_AUDIO_H */
