/* 2613-bad-pointers (M-029; services-capture): capture-badptr (permissions audio.microphone and capture.camera) gives
   both open calls every pointer they read, write or call wrong (lt_bad.h) at k 1: each LUMA_E_ARGUMENT, reported,
   nothing opened; "none_written" 1 when no out was written. Run steps call MR::getRandom(0, 4800 + k); the hook
   gives 1. */
#include "../loader/lt.h"
#include "../loader/lt_bad.h"
#include <luma_capture.h>

static const LumaAudioMicrophoneService* mic;
static const LumaCaptureCameraService* camera;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4800;
    if (k != 1) return 0;
    LumaHandle none = 7;
    const LumaAudioMicrophoneDesc voice = {.size = sizeof(LumaAudioMicrophoneDesc), .rate = 48000, .channels = 1};
    const LumaCaptureCameraDesc picture = {.size = sizeof(LumaCaptureCameraDesc)};
    lt_fact("mic_desc_unmapped", mic->open(lt_mod, (const LumaAudioMicrophoneDesc*)LT_UNMAPPED, on_event, 0, &none));
    lt_fact("mic_fn_data", mic->open(lt_mod, &voice, (LumaEventFn)(uintptr_t)LT_DATA_FN, 0, &none));
    lt_fact("mic_out_read_only", mic->open(lt_mod, &voice, on_event, 0, (LumaHandle*)LT_READ_ONLY));
    lt_fact("camera_desc_unmapped", camera->open(lt_mod, (const LumaCaptureCameraDesc*)LT_UNMAPPED, on_event, 0, &none));
    lt_fact("camera_fn_game", camera->open(lt_mod, &picture, (LumaEventFn)(uintptr_t)camera->close, 0, &none));
    lt_fact("camera_out_unmapped", camera->open(lt_mod, &picture, on_event, 0, (LumaHandle*)LT_UNMAPPED));
    lt_fact("none_written", none == 7);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mic, LUMA_AUDIO_MICROPHONE_NAME, "1.0"),
    LUMA_SERVICE(camera, LUMA_CAPTURE_CAMERA_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
