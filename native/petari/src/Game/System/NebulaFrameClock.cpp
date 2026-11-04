#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/GameThreads.hpp"

#include <math.h>

#include <nebula_game_api.h>
#include <nebula_threads.h>
#include "Game/System/NebulaSystemSettings.hpp"

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    u32 sFrames;
    // What the screen shows from the next frame on (show()).
    bool sPicture;

    // The design's picture: 16:9 at NebulaFrameClock::cWideScreenWidth screen units.
    const f64 cDesignAspect = 16.0 / 9.0;
    const NebulaDisplay cDesignDisplay = {0, 16.0f / 9.0f, NebulaFrameClock::cWideScreenWidth, NebulaFrameClock::cLayoutWidth, 1.0f};
    NebulaDisplay sDisplay = cDesignDisplay;
    u32 sDisplayExtent;

    // Every shape is held to 9:21 .. 32:9 (the presentation fits the rest). The screen width runs in even units from
    // NebulaFrameClock::cWideScreenWidth at 16:9 (NebulaFrameClock::cLayoutWidth is its own 4:3 mode); below 4:3 it stays 4:3's 624 and the picture grows taller
    // (Vert+: CameraContext::getRenderFovy, the layouts' vertical_fit).
    void refreshDisplay() {
        const NebulaHostServices* const host = nebula_host();
        const u32 extent = host->display_extent != nullptr ? host->display_extent(host->user) : 0;
        if (extent == sDisplayExtent) {
            return;
        }

        sDisplayExtent = extent;
        sDisplay = cDesignDisplay;
        const u32 w = extent >> 16;
        const u32 h = extent & 0xFFFF;
        if (w <= 1 || h <= 1) {
            return;
        }

        const f64 aspect = fmin(fmax(static_cast< f64 >(w) / h, 9.0 / 21.0), 32.0 / 9.0);
        const long width = lround(NebulaFrameClock::cWideScreenWidth * (fmax(aspect, 4.0 / 3.0) / cDesignAspect) * 0.5) * 2;
        if (width <= static_cast< long >(NebulaFrameClock::cLayoutWidth) || width > NebulaFrameClock::cWideScreenWidth * 2) {
            return;
        }

        sDisplay.wide = 1;
        sDisplay.content_aspect = static_cast< f32 >(aspect);
        sDisplay.screen_width = static_cast< s32 >(width);
        sDisplay.layout_width = static_cast< f32 >(NebulaFrameClock::cLayoutWidth * fmax(1.0, aspect / cDesignAspect));
        sDisplay.vertical_fit = static_cast< f32 >(fmax(1.0, cDesignAspect / aspect));
    }

    void onFrame() {
        sFrames++;
        nebula_host()->frame_begun(nebula_host()->user, sPicture ? 0 : 1, sFrames);
        refreshDisplay();
    }

}  // namespace

namespace NebulaFrameClock {
    u32 count() {
        return sFrames;
    }

    void waitFrame() {
        sleepUntilFrame();
        beginFrame();
    }

    void sleepUntilFrame() {
        GameThreads::waitFrame();
    }

    void beginFrame() {
        onFrame();
    }

    void show(bool picture) {
        sPicture = picture;
    }

    const NebulaDisplay& display() {
        return sDisplay;
    }

}  // namespace NebulaFrameClock
