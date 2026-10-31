#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaKernel.hpp"

#include <math.h>
#include <revolution/os.h>

#include <nebula_game_api.h>
#include <nebula_kernel.h>
#include "Game/System/NebulaSystemSettings.hpp"

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    volatile u32 sFrames;
    NebulaThreadQueue sSleepers;
    NebulaFrameClock::Hook sPreHook;
    NebulaFrameClock::Hook sPostHook;
    // show() sets the next state; it is what the screen shows from the next frame on.
    bool sNextPicture;
    bool sPicture;

    // VI_DM_DEFAULT: 5 minutes of NTSC fields.
    const u32 cTimeToDimming = 18000;
    bool sDimming;
    u32 sIdleFrames;

    // The Wii's picture: 16:9 at the game's 832 screen units.
    const f64 cWiiAspect = 16.0 / 9.0;
    const NebulaDisplay cWiiDisplay = {0, 16.0f / 9.0f, 832, 608.0f, 1.0f};
    NebulaDisplay sDisplay = cWiiDisplay;
    u32 sDisplayExtent;

    // Every shape is held to 9:21 .. 32:9 (the presentation fits the rest). The screen width runs in even units from
    // the game's 832 at 16:9 (608 is its own 4:3 mode); below 4:3 it stays 4:3's 624 and the picture grows taller
    // (Vert+: CameraContext::getRenderFovy, the layouts' vertical_fit).
    void refreshDisplay() {
        const NebulaHostServices* const host = nebula_host();
        const u32 extent = host->display_extent != nullptr ? host->display_extent(host->user) : 0;
        if (extent == sDisplayExtent) {
            return;
        }

        sDisplayExtent = extent;
        sDisplay = cWiiDisplay;
        const u32 w = extent >> 16;
        const u32 h = extent & 0xFFFF;
        if (w <= 1 || h <= 1) {
            return;
        }

        const f64 aspect = fmin(fmax(static_cast< f64 >(w) / h, 9.0 / 21.0), 32.0 / 9.0);
        const long width = lround(832.0 * (fmax(aspect, 4.0 / 3.0) / cWiiAspect) * 0.5) * 2;
        if (width <= 608 || width > 1664) {
            return;
        }

        sDisplay.wide = 1;
        sDisplay.content_aspect = static_cast< f32 >(aspect);
        sDisplay.screen_width = static_cast< s32 >(width);
        sDisplay.canvas_width = static_cast< f32 >(608.0 * fmax(1.0, aspect / cWiiAspect));
        sDisplay.vertical_fit = static_cast< f32 >(fmax(1.0, cWiiAspect / aspect));
    }

    void onFrame() {
        sFrames++;
        if (!sDimming) {
            sIdleFrames = 0;
        } else if (sIdleFrames < cTimeToDimming) {
            sIdleFrames++;
        }
        if (sPreHook != nullptr) {
            sPreHook(sFrames);
        }
        sPicture = sNextPicture;
        nebula_host()->vi_frame(nebula_host()->user, 0, sPicture ? 0 : 1, sFrames);
        refreshDisplay();
        if (sPostHook != nullptr) {
            sPostHook(sFrames);
        }
        sSleepers.wakeup();
    }
}  // namespace

extern "C" NEBULA_GAME_API void nebula_game_vi_retrace(void) {
    nebula_raise_event(NEBULA_EVENT_FRAME);
}

namespace NebulaFrameClock {
    void init() {
        static bool sStarted;
        if (sStarted) {
            return;
        }
        sStarted = true;
        sSleepers.init();
        NebulaKernel::setEventHandler(NEBULA_EVENT_FRAME, onFrame);
        NebulaKernel::enableEvent(NEBULA_EVENT_FRAME);
    }

    u32 count() {
        return sFrames;
    }

    // Interrupts off from reading the count to sleeping: a frame delivered in between would wake no one.
    void waitFrame() {
        const BOOL enabled = NebulaKernel::disableInterrupts();
        const u32 frames = sFrames;
        do {
            sSleepers.sleep();
        } while (frames == sFrames);
        NebulaKernel::restoreInterrupts(enabled);
    }

    void show(bool picture) {
        sNextPicture = picture;
    }

    // The frame arrives as a host event, which the kernel delivers only at its scheduling points: a plain store cannot
    // be interrupted halfway.
    Hook setPreHook(Hook hook) {
        const Hook previous = sPreHook;
        sPreHook = hook;
        return previous;
    }

    Hook setPostHook(Hook hook) {
        const Hook previous = sPostHook;
        sPostHook = hook;
        return previous;
    }

    const NebulaDisplay& display() {
        return sDisplay;
    }

    bool enableDimming(bool enable) {
        const bool previous = sDimming;
        sDimming = enable && NebulaSystemSettings::screenSaver();
        return previous;
    }

    u32 framesUntilDimming() {
        return cTimeToDimming - sIdleFrames;
    }

    void resetIdle() {
        sIdleFrames = 0;
    }
}  // namespace NebulaFrameClock
