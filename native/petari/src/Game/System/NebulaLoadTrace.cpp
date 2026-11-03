// GALAXY_TRACE_LOADS=1: a line at each press and each change of who has the screen, the times the load measurements read
// (tools/verify/loads.py). Not in a release.
#include "Game/System/NebulaLoadTrace.hpp"

#if !defined(NEBULA_RELEASE)
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <cstdlib>

#include "Game/Player/MarioAccess.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"

namespace {
    bool enabled() {
#if defined(NEBULA_RELEASE)
        static const bool sEnabled = false;
#else
        static s32 sEnabled = -1;
        if (sEnabled < 0) {
            sEnabled = nebula_host()->option(nebula_host()->user, NEBULA_OPTION_TEST_TRACE_LOADS) != 0 ? 1 : 0;
        }
#endif
        return sEnabled != 0;
    }

    void change(const char* pName, bool value, bool& rLast) {
        if (value != rLast) {
            rLast = value;
            JUTNativeLog("load: %s %d (%s)\n", pName, value ? 1 : 0, MR::getCurrentStageName());
        }
    }
}  // namespace

namespace NebulaLoadTrace {
    void frame() {
        if (enabled() && (MR::isPressedJump(0) || MR::isPressedFire(0))) {
            JUTNativeLog("load: press\n");
        }
    }

    void gameScene() {
        if (!enabled() || !MR::isInitializeStateEnd() || !MR::isExistSceneObj(SceneObj_DemoDirector)) {
            return;
        }
        static bool sDemo, sWipe, sSystemWipe, sOpening, sControl;
        const bool demo = MR::isDemoActive();
        const bool wipe = MR::isWipeActive();
        const bool systemWipe = MR::isSystemWipeActive();
        const bool opening = MR::isStageStateScenarioOpeningCamera();
        change("demo", demo, sDemo);
        change("wipe", wipe, sWipe);
        change("systemwipe", systemWipe, sSystemWipe);
        change("opening", opening, sOpening);
        change("control", !demo && !wipe && !systemWipe && !opening && !MR::isPlayerDead() && !MR::isPlayerHidden(), sControl);
    }
}  // namespace NebulaLoadTrace
#else
namespace NebulaLoadTrace {
    void frame() {
    }

    void gameScene() {
    }
}  // namespace NebulaLoadTrace
#endif
