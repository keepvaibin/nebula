#include "Game/System/NebulaFrameGen.hpp"

#ifdef TARGET_PC
#include <nebula_game_api.h>
#include <revolution/os.h>

namespace {
    // When a full-screen menu, page or window was last up (OSGetTime; 0 never).
    OSTime sFullscreenUiTime = 0;
    // A menu noted within this many milliseconds holds generation off (its notes come every frame).
    const s32 cFullscreenUiHoldMs = 50;
};  // namespace

namespace NebulaFrameGen {
    void noteFullscreenUi() {
        sFullscreenUiTime = OSGetTime();
    }

    void report(bool playing) {
        const bool menu = sFullscreenUiTime != 0 && OSTicksToMilliseconds(OSGetTime() - sFullscreenUiTime) < cFullscreenUiHoldMs;
        __NebulaGameContext(NEBULA_CONTEXT_FRAME_GENERATION, playing && !menu ? 1 : 0);
    }
};  // namespace NebulaFrameGen
#endif
