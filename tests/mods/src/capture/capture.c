/* H's capture conformance mods (services-audio 7, plan 2.6 layers 5-6), against native/include/luma_capture.h. One
   source, one case each (-DCASE=n). Run steps call MR::getRandom(0, 4800 + k) and the hook gives 1. A test machine
   may have no microphone or camera: the device's answer is either its blocks (or pictures) or ENDED with ABSENT,
   and the facts say which; both are the contract.
   capture-mic    (1) 2606: a 48 kHz mono microphone: SAMPLES of 1024 frames at 48000, or ENDED ABSENT; a 2nd open
                       LIMIT; 4 kHz, 3 channels, a small desc, a null fn: ARGUMENT; closed, closed again HANDLE.
   capture-camera (2) 2607: a 640×480 camera: FRAME (RGBA, stride width × 4) or ENDED ABSENT; a 2nd open LIMIT;
                       5000 wide, a width without a height, 120 fps, a null fn: ARGUMENT; closed, closed again HANDLE. */
#include "../loader/lt.h"
#include <luma_capture.h>

#ifndef CASE
#error CASE
#endif

#if CASE == 1
static const LumaAudioMicrophoneService* device;
#else
static const LumaCaptureCameraService* device;
#endif

static int32_t blocks, frames_ok, ended_absent, open_absent, ended_other, others, shape_ok, told;
static LumaHandle opened;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    if (event->handle != opened) {
        others++;
        return;
    }
    if (!told) {
        told = 1;
        lt_fact("first_event", event->kind); /* the test's next step waits for it: the device's time, not frames */
    }
    if (event->kind == LUMA_AUDIO_MICROPHONE_EVENT_SAMPLES) {
        blocks++;
        if (event->value == LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES && event->flags == 48000u &&
            event->data_size == LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES * sizeof(float)) {
            shape_ok++;
        }
    } else if (event->kind == LUMA_CAPTURE_CAMERA_EVENT_FRAME) {
        const LumaCaptureCameraFrame* frame = (const LumaCaptureCameraFrame*)event->data;
        frames_ok++;
        if (frame->size == sizeof(LumaCaptureCameraFrame) && frame->stride == frame->width * 4u && frame->width > 0u &&
            frame->height > 0u && frame->pixels != 0) {
            shape_ok++;
        }
    } else if (event->kind == LUMA_AUDIO_MICROPHONE_EVENT_ENDED || event->kind == LUMA_CAPTURE_CAMERA_EVENT_ENDED) {
        if (event->status == LUMA_E_ABSENT) {
            ended_absent++;
        } else {
            ended_other++;
        }
    } else {
        others++;
    }
}

static void step(int32_t k) {
    LumaHandle h = 0;
#if CASE == 1
    if (k == 1) {
        LumaAudioMicrophoneDesc desc = {sizeof(LumaAudioMicrophoneDesc), 48000u, 1u, 0u};
        const int32_t status = device->open(lt_mod, &desc, on_event, 0, &opened);
        lt_fact("open_answer", status == LUMA_OK || status == LUMA_E_ABSENT);
        open_absent = status == LUMA_E_ABSENT; /* no device on this platform (luma_capture.h): no event follows */
        if (status != LUMA_OK && !told) {
            told = 1;
            lt_fact("first_event", 0); /* no event will come */
        }
        lt_fact("open_twice", status == LUMA_OK ? device->open(lt_mod, &desc, on_event, 0, &h) == LUMA_E_LIMIT : 1);
        LumaAudioMicrophoneDesc slow = {sizeof(LumaAudioMicrophoneDesc), 4000u, 1u, 0u};
        lt_fact("rate_4k", device->open(lt_mod, &slow, on_event, 0, &h));
        LumaAudioMicrophoneDesc wide = {sizeof(LumaAudioMicrophoneDesc), 48000u, 3u, 0u};
        lt_fact("channels_3", device->open(lt_mod, &wide, on_event, 0, &h));
        LumaAudioMicrophoneDesc small = {8u, 48000u, 1u, 0u};
        lt_fact("small_desc", device->open(lt_mod, &small, on_event, 0, &h));
        lt_fact("no_fn", device->open(lt_mod, &desc, 0, 0, &h));
    }
#else
    if (k == 1) {
        LumaCaptureCameraDesc desc = {sizeof(LumaCaptureCameraDesc), 640u, 480u, 30u};
        const int32_t status = device->open(lt_mod, &desc, on_event, 0, &opened);
        lt_fact("open_answer", status == LUMA_OK || status == LUMA_E_ABSENT);
        open_absent = status == LUMA_E_ABSENT; /* no device on this platform (luma_capture.h): no event follows */
        if (status != LUMA_OK && !told) {
            told = 1;
            lt_fact("first_event", 0); /* no event will come */
        }
        lt_fact("open_twice", status == LUMA_OK ? device->open(lt_mod, &desc, on_event, 0, &h) == LUMA_E_LIMIT : 1);
        LumaCaptureCameraDesc huge = {sizeof(LumaCaptureCameraDesc), 5000u, 480u, 30u};
        lt_fact("width_5000", device->open(lt_mod, &huge, on_event, 0, &h));
        LumaCaptureCameraDesc half = {sizeof(LumaCaptureCameraDesc), 640u, 0u, 30u};
        lt_fact("no_height", device->open(lt_mod, &half, on_event, 0, &h));
        LumaCaptureCameraDesc fast = {sizeof(LumaCaptureCameraDesc), 640u, 480u, 120u};
        lt_fact("fps_120", device->open(lt_mod, &fast, on_event, 0, &h));
        lt_fact("no_fn", device->open(lt_mod, &desc, 0, 0, &h));
    }
#endif
    if (k == 2) {
        /* After the first event (first_event): blocks or pictures came, or the device was found missing (ENDED
           ABSENT, or open answered ABSENT). */
        lt_fact("heard_or_absent", (blocks + frames_ok) > 0 || ended_absent == 1 || open_absent);
        lt_fact("shapes_right", shape_ok == blocks + frames_ok);
        lt_fact("ended_other", ended_other);
        const int32_t closed = device->close(lt_mod, opened);
        lt_fact("close_answer", closed == LUMA_OK || ((ended_absent == 1 || open_absent) && closed == LUMA_E_HANDLE));
        lt_fact("close_again", device->close(lt_mod, opened));
        lt_fact("others", others);
    }
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4800;
    if (k >= 1 && k <= 2) step(k);
    return 0;
}

static const LumaImport imports[] = {
#if CASE == 1
    LUMA_SERVICE(device, LUMA_AUDIO_MICROPHONE_NAME, "1.0"),
#else
    LUMA_SERVICE(device, LUMA_CAPTURE_CAMERA_NAME, "1.0"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
