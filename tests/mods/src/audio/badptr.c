/* 2612-bad-pointers (M-029; services-audio): audio-badptr gives audio every pointer it reads, writes or calls wrong
   (lt_bad.h) at k 1: each LUMA_E_ARGUMENT, reported; "none_written" 1 when no out was written; a good stream opened
   ("open_good"), pushed from an unmapped address (refused) and from its own samples ("push_good", the frames taken),
   then closed. Run steps call MR::getRandom(0, 4800 + k) and the hook gives 1. */
#include "../loader/lt.h"
#include "../loader/lt_bad.h"
#include <luma_audio.h>

static const LumaAudioService* audio;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4800;
    if (k != 1) return 0;
    LumaHandle none = 7;
    uint32_t channels = 7;
    LumaAudioStreamDesc desc = {.size = sizeof(LumaAudioStreamDesc), .rate = 48000, .channels = 2,
                                .bus = LUMA_AUDIO_BUS_EFFECTS, .volume = 0.5f};
    lt_fact("play_name_unmapped", audio->play(lt_mod, (const char*)LT_UNMAPPED, 0, on_event, 0, &none));
    lt_fact("play_how_unmapped", audio->play(lt_mod, "beep", (const LumaAudioPlay*)LT_UNMAPPED, on_event, 0, &none));
    lt_fact("play_fn_data", audio->play(lt_mod, "beep", 0, (LumaEventFn)(uintptr_t)LT_DATA_FN, 0, &none));
    lt_fact("play_out_read_only", audio->play(lt_mod, "beep", 0, on_event, 0, (LumaHandle*)LT_READ_ONLY));
    lt_fact("play_at_unmapped", audio->play_at(lt_mod, "beep", 0, (const LumaAudioPlace*)LT_UNMAPPED, on_event, 0, &none));
    lt_fact("voice_place_unmapped", audio->voice_place(lt_mod, 12345u, (const LumaAudioPlace*)LT_UNMAPPED));
    lt_fact("game_play_name_unmapped", audio->game_play(lt_mod, (const char*)LT_UNMAPPED, 0, &none));
    lt_fact("game_play_out_read_only", audio->game_play(lt_mod, "SE_PM_JUMP_M", 0, (LumaHandle*)LT_READ_ONLY));
    lt_fact("game_place_unmapped", audio->game_place(lt_mod, 12345u, (const LumaAudioPlace*)LT_UNMAPPED));
    lt_fact("music_name_unmapped", audio->music(lt_mod, (const char*)LT_UNMAPPED, 0));
    lt_fact("stream_desc_unmapped", audio->stream_open(lt_mod, (const LumaAudioStreamDesc*)LT_UNMAPPED, 0, 0, &none));
    lt_fact("stream_fn_game", audio->stream_open(lt_mod, &desc, (LumaEventFn)(uintptr_t)audio->voice_stop, 0, &none));
    lt_fact("stream_out_read_only", audio->stream_open(lt_mod, &desc, 0, 0, (LumaHandle*)LT_READ_ONLY));
    lt_fact("tap_fn_data", audio->tap_open(lt_mod, LUMA_AUDIO_TAP_MODS, (LumaEventFn)(uintptr_t)LT_DATA_FN, 0, &none));
    lt_fact("none_written", none == 7);
    LumaHandle stream = 0;
    lt_fact("open_good", audio->stream_open(lt_mod, &desc, 0, 0, &stream));
    lt_fact("push_unmapped", audio->stream_push(lt_mod, stream, (const float*)LT_UNMAPPED, 100));
    lt_fact("channels_out_read_only", audio->stream_channels(lt_mod, stream, (uint32_t*)LT_READ_ONLY));
    lt_fact("channels_unwritten", channels == 7);
    static float quiet[2 * 100];
    lt_fact("push_good", audio->stream_push(lt_mod, stream, quiet, 100));
    lt_fact("close_good", audio->stream_close(lt_mod, stream, 0));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(audio, LUMA_AUDIO_NAME, "1.1"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
