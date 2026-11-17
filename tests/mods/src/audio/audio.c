/* H's audio conformance mods (services-audio, plan 2.6 layers 1-2), against native/include/luma_audio.h. One source,
   one case each (-DCASE=n). Run steps call MR::getRandom(0, 4800 + k) and the hook gives 1.
   audio-play     (1) 2601: its own cooked sound "beep" (layers/base/assets/beep.sound.json + beep.wav, 0.1 s) played
                       once with a function (ENDED OK when the mix played it to its end), once as
                       "audio-play:beep" with none, once looped and stopped (no event), voice_set on it first; "bad"
                       (bad.nsfx, not a cooked sound) FORMAT, reported; a stopped voice's handle closed.
   audio-limit    (2) 2602: 33 looped plays of "beep": the 33rd is played and the oldest (equal priority) ends with
                       ENDED LIMIT, reported Audio.Dropped; 32 stopped after.
   audio-stream   (3) 2603: a 48 kHz stereo stream: NEED at once with the ring's room (24000), a push of 30000 takes
                       24000, a push into a full ring takes 0; NEED again after the mix played it down; a push with 3
                       NaN and 1 infinity takes them as silence, reported Audio.NotANumber; 4 more streams: the 5th
                       LIMIT; close with drain; a push after close HANDLE.
   audio-mistakes (4) 2604: null and empty names, a LumaAudioPlay of 8 bytes, pitch 5, bus 7, loop 2, a null out,
                       bus_volume bus 4 and volume 2, a stream of 4 kHz and of 3 channels: ARGUMENT, each reported.
   audio-tap      (5) 2605: a tap of the mods' mix and one of the output while its own stream plays a tone: SAMPLES of
                       1024 frames with sound in the mods' tap; a 3rd tap LIMIT, a tap with no fn ARGUMENT; its sound
                       as the game's music, restored; taps closed.
   audio-place    (6) 2608: "beep" played once from a place 10 units right of the camera (ENDED OK: the mix played
                       it placed) and looped from the player's place, heard by its tap of the mods' mix; the looped
                       one moved to a point, then to no place, then stopped; a LumaAudioPlace of 8 bytes, follow 3, a
                       NaN and a 2e7 coordinate ARGUMENT, each reported; voice_place on a stray handle HANDLE.
   audio-game     (7) 2609: the game's SE_PM_JUMP_M heard everywhere and from the player's place; the placed one
                       moved and stopped (stopped again HANDLE); the one with no place cannot be placed (ARGUMENT); a
                       name not in the game's table NOT_FOUND; a null name, a null out, follow 5 ARGUMENT; 17 at once:
                       the 17th LIMIT (or BUSY when the game's own voices ran out first).
   audio-missing  (8) 2611: names no file has: "nothing-here" by play (with a function), by play_at (with a function)
                       and as music, "nothing-else" by play: each LUMA_OK and a voice, the placeholder chime playing in
                       its place (ENDED OK for the two with a function once the 120 ms chime played; the music plays
                       once, then restored). */
#include "../loader/lt.h"
#include <luma_audio.h>

#ifndef CASE
#error CASE
#endif

static const LumaAudioService* audio;

static int32_t ended_ok, ended_limit, ended_other, needs, need_room, others;
__attribute__((unused)) static int32_t blocks_mods, blocks_output, loud_mods, block_frames, block_rate;
__attribute__((unused)) static LumaHandle tap_mods, tap_output;
__attribute__((unused)) static LumaHandle first_voice, looped_voice;

__attribute__((unused)) static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind == LUMA_AUDIO_EVENT_ENDED) {
        if (event->status == LUMA_OK) {
            ended_ok++;
        } else if (event->status == LUMA_E_LIMIT) {
            ended_limit++;
            if (event->handle != first_voice) others++;
        } else {
            ended_other++;
        }
    } else if (event->kind == LUMA_AUDIO_EVENT_NEED) {
        needs++;
        need_room = (int32_t)event->value;
    } else if (event->kind == LUMA_AUDIO_EVENT_SAMPLES) {
        const float* s = (const float*)event->data;
        float most = 0.0f;
        for (uint64_t i = 0; i < event->value * 2u; i++) {
            const float a = s[i] < 0.0f ? -s[i] : s[i];
            if (a > most) most = a;
        }
        block_frames = (int32_t)event->value;
        block_rate = (int32_t)event->flags;
        if (event->handle == tap_mods) {
            blocks_mods++;
            if (most > 0.01f) loud_mods++;
        } else if (event->handle == tap_output) {
            blocks_output++;
        } else {
            others++;
        }
    } else {
        others++;
    }
}

__attribute__((unused)) static LumaAudioPlay how_of(float volume, float pitch, uint32_t bus, int32_t loop) {
    LumaAudioPlay how;
    how.size = sizeof(how);
    how.volume = volume;
    how.pitch = pitch;
    how.bus = bus;
    how.loop = loop;
    how.reserved = 0;
    return how;
}

__attribute__((unused)) static LumaAudioStreamDesc desc_of(uint32_t rate, uint32_t channels) {
    LumaAudioStreamDesc desc;
    desc.size = sizeof(desc);
    desc.rate = rate;
    desc.channels = channels;
    desc.bus = LUMA_AUDIO_BUS_EFFECTS;
    desc.volume = 0.5f;
    desc.reserved = 0;
    return desc;
}

__attribute__((unused)) static LumaAudioPlace place_of(uint32_t follow, float x, float y, float z) {
    LumaAudioPlace at;
    at.size = sizeof(at);
    at.follow = follow;
    at.pos[0] = x;
    at.pos[1] = y;
    at.pos[2] = z;
    at.reserved = 0;
    return at;
}

__attribute__((unused)) static LumaHandle game_flat, game_placed, game_many[16];

#if CASE == 3 || CASE == 5
/* 30000 stereo frames: a 440 Hz triangle (no sine without a C library), then 4 frames with 3 NaN and 1 infinity. */
static float wave[60000];
__attribute__((unused)) static float broken[8];
static LumaHandle stream;
#endif

static void step(int32_t k) {
#if CASE == 1
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("play", audio->play(lt_mod, "beep", 0, on_event, 0, &first_voice));
        lt_fact("play_id_form", audio->play(lt_mod, "audio-play:beep", 0, 0, 0, &h));
        LumaAudioPlay loop = how_of(0.5f, 1.0f, LUMA_AUDIO_BUS_SOUND, 1);
        lt_fact("play_loop", audio->play(lt_mod, "beep", &loop, on_event, 0, &looped_voice));
        lt_fact("voice_set", audio->voice_set(lt_mod, looped_voice, 0.25f, 2.0f));
        lt_fact("not_a_sound", audio->play(lt_mod, "bad", 0, on_event, 0, &h));
        lt_fact("bus_volume", audio->bus_volume(lt_mod, LUMA_AUDIO_BUS_EFFECTS, 0.5f));
    }
    if (k == 2) {
        /* 0.1 s played at least 6 frames later: its ENDED is in; the looped one still plays. */
        lt_fact("ended_ok", ended_ok);
        lt_fact("ended_other", ended_other + ended_limit);
        lt_fact("first_closed", audio->voice_set(lt_mod, first_voice, 1.0f, 1.0f));
        lt_fact("loop_set", audio->voice_set(lt_mod, looped_voice, 1.0f, 1.0f));
        lt_fact("stop", audio->voice_stop(lt_mod, looped_voice, 50));
        lt_fact("stop_again", audio->voice_stop(lt_mod, looped_voice, 0));
    }
    if (k == 3) {
        lt_fact("ended_after_stop", ended_ok);
        lt_fact("others", others);
    }
#elif CASE == 2
    static LumaHandle voices[33];
    if (k == 1) {
        LumaAudioPlay loop = how_of(0.1f, 1.0f, LUMA_AUDIO_BUS_SOUND, 1);
        int32_t played = 0;
        for (int32_t i = 0; i < 32; i++) {
            if (audio->play(lt_mod, "beep", &loop, on_event, 0, &voices[i]) == LUMA_OK) played++;
        }
        first_voice = voices[0];
        lt_fact("played_32", played);
        lt_fact("play_33", audio->play(lt_mod, "beep", &loop, on_event, 0, &voices[32]));
    }
    if (k == 2) {
        lt_fact("ended_limit", ended_limit);
        lt_fact("ended_ok", ended_ok);
        lt_fact("first_closed", audio->voice_stop(lt_mod, voices[0], 0));
        int32_t stopped = 0;
        for (int32_t i = 1; i < 33; i++) {
            if (audio->voice_stop(lt_mod, voices[i], 0) == LUMA_OK) stopped++;
        }
        lt_fact("stopped_32", stopped);
        lt_fact("others", others);
    }
#elif CASE == 3
    if (k == 1) {
        for (int32_t i = 0; i < 30000; i++) {
            const int32_t at = i % 109; /* 48000 / 440 */
            const float v = at < 55 ? (float)at / 27.0f - 1.0f : 1.0f - (float)(at - 55) / 27.0f;
            wave[i * 2] = v * 0.25f;
            wave[i * 2 + 1] = v * 0.25f;
        }
        const LumaAudioStreamDesc desc = desc_of(48000, 2);
        lt_fact("open", audio->stream_open(lt_mod, &desc, on_event, 0, &stream));
    }
    if (k == 2) {
        lt_fact("first_need", needs);
        lt_fact("first_room", need_room);
        lt_fact("push", (long long)audio->stream_push(lt_mod, stream, wave, 30000));
        lt_fact("push_full", (long long)audio->stream_push(lt_mod, stream, wave, 100));
    }
    if (k == 3) {
        /* 0.5 s held, NEED under 0.1 s: 0.4 s (24+ frames) later the mix has played it down. */
        lt_fact("need_again", needs >= 2);
        uint32_t nan_bits = 0x7FC00000u, inf_bits = 0x7F800000u;
        float nan_value, inf_value;
        __builtin_memcpy(&nan_value, &nan_bits, 4);
        __builtin_memcpy(&inf_value, &inf_bits, 4);
        broken[0] = nan_value;
        broken[1] = 0.1f;
        broken[2] = nan_value;
        broken[3] = inf_value;
        broken[4] = nan_value;
        broken[5] = 0.0f;
        broken[6] = 0.2f;
        broken[7] = 0.2f;
        lt_fact("push_broken", (long long)audio->stream_push(lt_mod, stream, broken, 4));
        LumaHandle more[5];
        const LumaAudioStreamDesc mono = desc_of(22050, 1);
        int32_t opened = 0;
        for (int32_t i = 0; i < 3; i++) {
            if (audio->stream_open(lt_mod, &mono, 0, 0, &more[i]) == LUMA_OK) opened++;
        }
        lt_fact("opened_3", opened);
        lt_fact("open_5th", audio->stream_open(lt_mod, &mono, 0, 0, &more[3]));
        lt_fact("close_drain", audio->stream_close(lt_mod, stream, 1));
        lt_fact("push_closed", (long long)audio->stream_push(lt_mod, stream, wave, 10));
        int32_t closed = 0;
        for (int32_t i = 0; i < 3; i++) {
            if (audio->stream_close(lt_mod, more[i], 0) == LUMA_OK) closed++;
        }
        lt_fact("closed_3", closed);
        lt_fact("others", others);
    }
#elif CASE == 5
    if (k == 1) {
        for (int32_t i = 0; i < 30000; i++) {
            const int32_t at = i % 109;
            const float v = at < 55 ? (float)at / 27.0f - 1.0f : 1.0f - (float)(at - 55) / 27.0f;
            wave[i * 2] = v * 0.25f;
            wave[i * 2 + 1] = v * 0.25f;
        }
        LumaHandle h = 0;
        lt_fact("tap_mods", audio->tap_open(lt_mod, LUMA_AUDIO_TAP_MODS, on_event, 0, &tap_mods));
        lt_fact("tap_output", audio->tap_open(lt_mod, LUMA_AUDIO_TAP_OUTPUT, on_event, 0, &tap_output));
        lt_fact("tap_3rd", audio->tap_open(lt_mod, LUMA_AUDIO_TAP_OUTPUT, on_event, 0, &h));
        lt_fact("tap_no_fn", audio->tap_open(lt_mod, LUMA_AUDIO_TAP_MODS, 0, 0, &h));
        const LumaAudioStreamDesc desc = desc_of(48000, 2);
        lt_fact("open", audio->stream_open(lt_mod, &desc, 0, 0, &stream));
        lt_fact("push", (long long)audio->stream_push(lt_mod, stream, wave, 24000));
        lt_fact("music", audio->music(lt_mod, "beep", 100));
    }
    if (k == 2) {
        lt_fact("blocks_mods_some", blocks_mods >= 2);
        lt_fact("loud_mods_some", loud_mods >= 1);
        lt_fact("blocks_output_some", blocks_output >= 2);
        lt_fact("block_frames", block_frames);
        lt_fact("block_rate_some", block_rate >= 8000);
        lt_fact("restore", audio->music_restore(lt_mod, 100));
        lt_fact("restore_again", audio->music_restore(lt_mod, 0));
        lt_fact("close_mods", audio->tap_close(lt_mod, tap_mods));
        lt_fact("close_output", audio->tap_close(lt_mod, tap_output));
        lt_fact("close_again", audio->tap_close(lt_mod, tap_output));
        lt_fact("stream_close", audio->stream_close(lt_mod, stream, 0));
        lt_fact("others", others);
    }
#elif CASE == 4
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("null_name", audio->play(lt_mod, 0, 0, 0, 0, &h));
        lt_fact("empty_name", audio->play(lt_mod, "", 0, 0, 0, &h));
        LumaAudioPlay small = how_of(1.0f, 1.0f, LUMA_AUDIO_BUS_SOUND, -1);
        small.size = 8;
        lt_fact("small_how", audio->play(lt_mod, "beep", &small, 0, 0, &h));
        LumaAudioPlay high = how_of(1.0f, 5.0f, LUMA_AUDIO_BUS_SOUND, -1);
        lt_fact("pitch_5", audio->play(lt_mod, "beep", &high, 0, 0, &h));
        LumaAudioPlay bus = how_of(1.0f, 1.0f, 7, -1);
        lt_fact("bus_7", audio->play(lt_mod, "beep", &bus, 0, 0, &h));
        LumaAudioPlay loop = how_of(1.0f, 1.0f, LUMA_AUDIO_BUS_SOUND, 2);
        lt_fact("loop_2", audio->play(lt_mod, "beep", &loop, 0, 0, &h));
        lt_fact("null_out", audio->play(lt_mod, "beep", 0, 0, 0, 0));
        lt_fact("bus_4", audio->bus_volume(lt_mod, 4, 0.5f));
        lt_fact("volume_2", audio->bus_volume(lt_mod, LUMA_AUDIO_BUS_MUSIC, 2.0f));
        const LumaAudioStreamDesc slow = desc_of(4000, 1);
        lt_fact("rate_4k", audio->stream_open(lt_mod, &slow, 0, 0, &h));
        const LumaAudioStreamDesc wide = desc_of(48000, 3);
        lt_fact("channels_3", audio->stream_open(lt_mod, &wide, 0, 0, &h));
        lt_fact("stray_handle", audio->voice_stop(lt_mod, 12345678, 0));
    }
#elif CASE == 6
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("tap_mods", audio->tap_open(lt_mod, LUMA_AUDIO_TAP_MODS, on_event, 0, &tap_mods));
        const LumaAudioPlace beside = place_of(LUMA_AUDIO_AT_CAMERA, 10.0f, 0.0f, 0.0f);
        lt_fact("play_at", audio->play_at(lt_mod, "beep", 0, &beside, on_event, 0, &first_voice));
        const LumaAudioPlay loop = how_of(1.0f, 1.0f, LUMA_AUDIO_BUS_SOUND, 1);
        const LumaAudioPlace player = place_of(LUMA_AUDIO_AT_PLAYER, 0.0f, 50.0f, 0.0f);
        lt_fact("play_at_loop", audio->play_at(lt_mod, "beep", &loop, &player, on_event, 0, &looped_voice));
        LumaAudioPlace small = beside;
        small.size = 8;
        lt_fact("small_place", audio->play_at(lt_mod, "beep", 0, &small, 0, 0, &h));
        const LumaAudioPlace follow_3 = place_of(3, 0.0f, 0.0f, 0.0f);
        lt_fact("follow_3", audio->play_at(lt_mod, "beep", 0, &follow_3, 0, 0, &h));
        float nan = 0.0f;
        nan = nan / nan;
        const LumaAudioPlace not_number = place_of(LUMA_AUDIO_AT_POINT, nan, 0.0f, 0.0f);
        lt_fact("pos_nan", audio->play_at(lt_mod, "beep", 0, &not_number, 0, 0, &h));
        const LumaAudioPlace too_far = place_of(LUMA_AUDIO_AT_POINT, 0.0f, 2.0e7f, 0.0f);
        lt_fact("pos_2e7", audio->voice_place(lt_mod, looped_voice, &too_far));
        lt_fact("stray_place", audio->voice_place(lt_mod, 12345678, &beside));
    }
    if (k == 2) {
        lt_fact("ended_ok", ended_ok);
        lt_fact("loud_mods_some", loud_mods >= 1);
        const LumaAudioPlace point = place_of(LUMA_AUDIO_AT_POINT, 0.0f, 0.0f, 0.0f);
        lt_fact("place_point", audio->voice_place(lt_mod, looped_voice, &point));
        lt_fact("place_none", audio->voice_place(lt_mod, looped_voice, 0));
        lt_fact("stop", audio->voice_stop(lt_mod, looped_voice, 0));
        lt_fact("place_stopped", audio->voice_place(lt_mod, looped_voice, &point));
        lt_fact("tap_close", audio->tap_close(lt_mod, tap_mods));
        lt_fact("ended_other", ended_other + ended_limit);
        lt_fact("others", others);
    }
#elif CASE == 7
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("everywhere", audio->game_play(lt_mod, "SE_PM_JUMP_M", 0, &game_flat));
        const LumaAudioPlace player = place_of(LUMA_AUDIO_AT_PLAYER, 0.0f, 100.0f, 0.0f);
        lt_fact("at_player", audio->game_play(lt_mod, "SE_PM_JUMP_M", &player, &game_placed));
        const LumaAudioPlace point = place_of(LUMA_AUDIO_AT_POINT, 0.0f, 0.0f, 0.0f);
        lt_fact("place", audio->game_place(lt_mod, game_placed, &point));
        lt_fact("place_flat", audio->game_place(lt_mod, game_flat, &point));
        lt_fact("missing", audio->game_play(lt_mod, "SE_NOT_IN_THE_TABLE_XYZ", 0, &h));
        lt_fact("null_name", audio->game_play(lt_mod, 0, 0, &h));
        lt_fact("null_out", audio->game_play(lt_mod, "SE_PM_JUMP_M", 0, 0));
        const LumaAudioPlace follow_5 = place_of(5, 0.0f, 0.0f, 0.0f);
        lt_fact("follow_5", audio->game_play(lt_mod, "SE_PM_JUMP_M", &follow_5, &h));
        lt_fact("stop", audio->game_stop(lt_mod, game_placed, 100));
        lt_fact("stop_again", audio->game_stop(lt_mod, game_placed, 0));
        lt_fact("stop_flat", audio->game_stop(lt_mod, game_flat, 0));
        lt_fact("stray_stop", audio->game_stop(lt_mod, 12345678, 0));
    }
    if (k == 2) {
        /* 16 held at once (the ones before were stopped), then a 17th. */
        int32_t played = 0;
        int32_t busy = 0;
        for (int32_t i = 0; i < 16; i++) {
            const int32_t r = audio->game_play(lt_mod, "SE_PM_JUMP_M", 0, &game_many[i]);
            if (r == LUMA_OK) played++;
            else if (r == LUMA_E_BUSY) busy++;
        }
        LumaHandle h = 0;
        const int32_t last = audio->game_play(lt_mod, "SE_PM_JUMP_M", 0, &h);
        lt_fact("played_or_busy_16", played + busy);
        lt_fact("seventeenth_held", played == 16 ? last == LUMA_E_LIMIT : last == LUMA_E_BUSY || last == LUMA_OK);
        for (int32_t i = 0; i < 16; i++) {
            (void)audio->game_stop(lt_mod, game_many[i], 0);
        }
    }
#elif CASE == 8
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("play_missing", audio->play(lt_mod, "nothing-here", 0, on_event, 0, &first_voice));
        lt_fact("play_voice", first_voice != 0);
        const LumaAudioPlace beside = place_of(LUMA_AUDIO_AT_CAMERA, 10.0f, 0.0f, 0.0f);
        lt_fact("play_at_missing", audio->play_at(lt_mod, "nothing-here", 0, &beside, on_event, 0, &looped_voice));
        lt_fact("play_at_voice", looped_voice != 0);
        lt_fact("play_other", audio->play(lt_mod, "nothing-else", 0, 0, 0, &h));
        lt_fact("music_missing", audio->music(lt_mod, "nothing-here", 0));
    }
    if (k == 2) {
        /* 0.12 s played at least 30 frames later: both chimes with a function ended. */
        lt_fact("ended_ok", ended_ok);
        lt_fact("ended_other", ended_other + ended_limit);
        lt_fact("restore", audio->music_restore(lt_mod, 0));
        lt_fact("others", others);
    }
#endif
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4800;
    if (k >= 1 && k <= 3) step(k);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(audio, LUMA_AUDIO_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
