/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's audio service (docs/modding/services/audio.md). `audio` 1.0, no permission: a mod plays its cooked
   sound effects (3.7's NSFX: `layers/<l>/assets/<path>.sound.json`, cooked at the game's start) and pushes sound it
   makes itself (a synthesiser, a decoded movie, a voice chat) as a stream. Both go into the game's own mixer, after
   the game's voices, on four buses that follow the game's volume settings: effects and ui (Sound effects), voice
   (Voices), music (Music); the master volume and the output device are the game's. Every call is the game thread's,
   never blocks, and what waits is an event at a frame's start (services-registry 6). No mod code ever runs on the
   audio thread: a stream is a ring the mod fills, never a callback. A mod that stops is silent the same audio frame.
   A voice can be heard from a place in the world (play_at); the game's own sounds start by name (game_play); the
   game's music can be replaced (music); the mix can be listened to (tap_open). Capture is luma_capture.h. */
#ifndef LUMA_AUDIO_H
#define LUMA_AUDIO_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_AUDIO_NAME "audio"
#define LUMA_AUDIO_MAJOR 1u
#define LUMA_AUDIO_MINOR 1u

#define LUMA_AUDIO_VOICES_MOST 32u      /* voices a mod plays at once */
#define LUMA_AUDIO_STREAMS_MOST 4u      /* streams a mod holds at once */
#define LUMA_AUDIO_NAME_MOST 255u       /* bytes of a sound's name */
#define LUMA_AUDIO_RATE_LEAST 8000u     /* a stream's rate, Hz */
#define LUMA_AUDIO_RATE_MOST 192000u
#define LUMA_AUDIO_RING_MS 500u         /* what a stream holds pushed and not yet played */
#define LUMA_AUDIO_NEED_MS 100u         /* under this much, a stream asks for more (NEED) */
#define LUMA_AUDIO_VOLUME_MOST 4.0f     /* a voice's own volume (linear; 1: as cooked) */
#define LUMA_AUDIO_FADE_MOST_MS 10000u  /* voice_stop's, music's and music_restore's longest fade */
#define LUMA_AUDIO_TAPS_MOST 2u         /* taps a mod holds at once */
#define LUMA_AUDIO_TAP_FRAMES 1024u     /* frames in a SAMPLES block (stereo, float) */
#define LUMA_AUDIO_GAME_SOUNDS_MOST 16u /* the game's own sounds a mod holds at once (game_play) */
#define LUMA_AUDIO_PLACE_MOST 1.0e7f    /* a place's coordinates, game units, either way */

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
    LUMA_AUDIO_EVENT_NEED = 2,  /* a stream's: less than LUMA_AUDIO_NEED_MS waits to be played; value: the frames
                                   stream_push takes now. Once a frame at most, and again only after a push. */
    LUMA_AUDIO_EVENT_SAMPLES = 3 /* a tap's: data, LUMA_AUDIO_TAP_FRAMES stereo frames of float (left, right; valid
                                   during the call only); value: the frames; flags: the output's rate (Hz); status
                                   LUMA_E_BUSY when blocks before this one were lost (the mod took too long) */
} LumaAudioEvent;

/* What a tap hears (tap_open). */
typedef enum LumaAudioTapSource {
    LUMA_AUDIO_TAP_OUTPUT = 0, /* the game's output: the game's sound and every mod's, after the player's volumes */
    LUMA_AUDIO_TAP_MODS = 1    /* every mod's voices and streams, before the master volume (the game's own not in it) */
} LumaAudioTapSource;

/* What a place is from (LumaAudioPlace.follow). */
typedef enum LumaAudioFollow {
    LUMA_AUDIO_AT_POINT = 0,  /* pos is a point in the world */
    LUMA_AUDIO_AT_PLAYER = 1, /* pos is from the player's place (world axes) and moves with the player */
    LUMA_AUDIO_AT_CAMERA = 2  /* pos is from the camera's place (world axes) and moves with the camera */
} LumaAudioFollow;

/* Where a sound is heard from: the camera hears it (distance and side), as the game hears its own. */
typedef struct LumaAudioPlace {
    uint32_t size;   /* sizeof(LumaAudioPlace) */
    uint32_t follow; /* LumaAudioFollow */
    float pos[3];    /* game units, each within LUMA_AUDIO_PLACE_MOST */
    uint32_t reserved;
} LumaAudioPlace;

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
       "<id>:<path>", another mod's. LUMA_OK and *out (the voice); ENDED to fn (null: no event) when it ends. A name
       no file has: the placeholder chime (nebula:placeholder/sound, 120 ms) plays in its place with `how`, LUMA_OK
       and *out, and the mod log has Content.MissingSound (624) naming it, once a name, counted on every call (a store
       without the chime: LUMA_E_NOT_FOUND; the start check lets no mod run on one); a file that is not a cooked
       sound: LUMA_E_FORMAT, reported
       (Audio.NotASound); a 33rd voice: the mod's voice of the lowest priority (the new one's included; the oldest
       of equals) ends, ENDED LUMA_E_LIMIT, reported; when that is the new one, LUMA_E_LIMIT, reported. A null name or
       out, a name past LUMA_AUDIO_NAME_MOST, a `how` of the wrong size or out of range: LUMA_E_ARGUMENT, reported.
       `how` (null: the sound as cooked) sets its volume, pitch, bus and loop. */
    /* events: ENDED(socket, status) */
    int32_t (*play)(const LumaMod* mod, const char* name, const LumaAudioPlay* how, LumaEventFn fn, void* user,
                    LumaHandle* out);
    /* A playing voice's volume and pitch (as LumaAudioPlay's), ramped over 10 ms. A handle not the mod's playing
       voice: LUMA_E_HANDLE, reported; out of range: LUMA_E_ARGUMENT, reported. */
    int32_t (*voice_set)(const LumaMod* mod, LumaHandle voice, float volume, float pitch);
    /* Stops a voice over `fade_ms` (0: 10 ms, so nothing clicks; at most LUMA_AUDIO_FADE_MOST_MS). No event after;
       the handle is closed at once. A handle not the mod's voice: LUMA_E_HANDLE, reported. */
    int32_t (*voice_stop)(const LumaMod* mod, LumaHandle voice, uint32_t fade_ms);
    /* The mod's own share of a bus (0 to 1; 1 at its start), on top of the player's setting for it: every voice and
       stream of the mod on that bus, ramped over 10 ms. LUMA_OK; a bus past LUMA_AUDIO_BUS_MUSIC, a volume not a
       number or outside 0 to 1: LUMA_E_ARGUMENT, reported. */
    int32_t (*bus_volume)(const LumaMod* mod, uint32_t bus, float volume);
    /* A stream the mod fills with stream_push. LUMA_OK and *out; NEED follows at once (it is empty). A 5th stream:
       LUMA_E_LIMIT, reported; a desc of the wrong size or out of range, a null desc or out: LUMA_E_ARGUMENT,
       reported. */
    /* events: NEED(socket, value) */
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
    /* Replaces the game's music: the cooked sound `name` (as play's) plays on the music bus, looped by its loop
       points (or whole), fading in over `fade_ms` (0: 10 ms) while the game's own music fades out as long. One music
       for every mod: a later call, this mod's or another's, takes over (the one before fades out as long). Its end is
       the mod's music_restore or its stop. Answers as play does (a name no file has: the placeholder chime, once,
       LUMA_OK, Content.MissingSound; FORMAT; fade_ms past LUMA_AUDIO_FADE_MOST_MS: LUMA_E_ARGUMENT). */
    int32_t (*music)(const LumaMod* mod, const char* name, uint32_t fade_ms);
    /* The game's music back over `fade_ms` (0: 10 ms), the mod's fading out. LUMA_OK also when the mod's music is
       not playing (nothing to do). */
    int32_t (*music_restore)(const LumaMod* mod, uint32_t fade_ms);
    /* Listens to `source` (LumaAudioTapSource): SAMPLES to fn (not null) once every LUMA_AUDIO_TAP_FRAMES frames,
       for visualisers, recorders, beat detection. A 3rd tap: LUMA_E_LIMIT, reported. It hears only the game's
       sound and the mods', nothing of the player's (the microphone is the audio.microphone service). */
    /* events: SAMPLES(socket, status, data, value, flags) */
    int32_t (*tap_open)(const LumaMod* mod, uint32_t source, LumaEventFn fn, void* user, LumaHandle* out);
    /* Ends a tap; no event after. A handle not the mod's tap: LUMA_E_HANDLE, reported. */
    int32_t (*tap_close)(const LumaMod* mod, LumaHandle tap);
    /* As play, heard from `at` (null: as play, everywhere): the sound's near and far (sound.md 3: full volume within
       near, silent beyond far, linear between) and its side from the camera (constant power). A place that is not
       there (no camera, no player) keeps the last one heard (none yet: heard as with no place). Answers as play; a place of the wrong size, a follow
       past LUMA_AUDIO_AT_CAMERA or a pos not finite or past LUMA_AUDIO_PLACE_MOST: LUMA_E_ARGUMENT, reported. */
    /* events: ENDED(socket, status) */
    int32_t (*play_at)(const LumaMod* mod, const char* name, const LumaAudioPlay* how, const LumaAudioPlace* at,
                       LumaEventFn fn, void* user, LumaHandle* out);
    /* A playing voice's place (null: heard everywhere again), from the next audio block, the change ramped over it.
       A handle not the mod's playing voice: LUMA_E_HANDLE, reported; a bad place: LUMA_E_ARGUMENT, reported. */
    int32_t (*voice_place)(const LumaMod* mod, LumaHandle voice, const LumaAudioPlace* at);
    /* Starts the game's own sound `name`, of its sound table (SE_PM_JUMP_M, SE_SY_STAR_GET, as sound.md 6 names
       them), through the game's sound engine: `at` null, heard everywhere (as the game's menu sounds); otherwise
       from that place with the game's own distances and pan. LUMA_OK and *out. Answers: LUMA_E_NOT_FOUND, no sound
       of that name; LUMA_E_BUSY, the game does not start one now (its effects are held, as in the pause menu; its
       voices are full; a place that follows what is not there). A 17th sound of the mod's: LUMA_E_LIMIT, reported
       (a sound that ended is no longer counted). No event. A null name or out, a name past LUMA_AUDIO_NAME_MOST, a
       bad place: LUMA_E_ARGUMENT, reported. */
    int32_t (*game_play)(const LumaMod* mod, const char* name, const LumaAudioPlace* at, LumaHandle* out);
    /* Moves a game sound started with a place. LUMA_OK also when it has ended. A handle not the mod's game sound:
       LUMA_E_HANDLE, reported; one started with no place, a bad place: LUMA_E_ARGUMENT, reported. */
    int32_t (*game_place)(const LumaMod* mod, LumaHandle sound, const LumaAudioPlace* at);
    /* Stops a game sound over `fade_ms` (0: at once; at most LUMA_AUDIO_FADE_MOST_MS), rounded up to the game's
       frames. LUMA_OK also when it has ended; the handle is closed. A handle not the mod's game sound: LUMA_E_HANDLE,
       reported. */
    int32_t (*game_stop)(const LumaMod* mod, LumaHandle sound, uint32_t fade_ms);
    /* 1.1. A stream's channels as its desc gave them (1 or 2): stream_push reads `frames` x that many floats. LUMA_OK
       and *out. A handle not the mod's stream: LUMA_E_HANDLE, reported; a null out: LUMA_E_ARGUMENT, reported. */
    int32_t (*stream_channels)(const LumaMod* mod, LumaHandle stream, uint32_t* out);
} LumaAudioService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_AUDIO_H */
