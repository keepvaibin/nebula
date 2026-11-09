// GALAXY_TRACE_LOADS=1: a line at each press and each change of who has the screen, the times the load measurements read
// (tools/verify/loads.py). Not in a release.
#include "Game/System/NebulaLoadTrace.hpp"

#if defined(NEBULA_DEV)
#include <nebula_module.h>

#include "Game/Player/MarioAccess.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"

namespace {
    u32 sPlaced = 0;

    bool enabled() {
#if !defined(NEBULA_DEV)
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
            nebula_log("load: %s %d (%s)\n", pName, value ? 1 : 0, MR::getCurrentStageName());
        }
    }
}  // namespace

namespace NebulaLoadTrace {
    Scope::Scope(const char* name) : mName(name), mStart(enabled() ? nebula_host()->clock_ns(nebula_host()->user) : 0) {}
    Scope::~Scope() {
        if (!mStart) return;
        const auto elapsed = nebula_host()->clock_ns(nebula_host()->user) - mStart;
        if (elapsed >= 1000000ull) nebula_log("load-phase: %s %.3f ms\n", mName, elapsed / 1000000.0);
    }
    void frame() {
        if (enabled() && (MR::isPressedJump(0) || MR::isPressedFire(0))) {
            nebula_log("load: press\n");
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

    void sorted(const char* pName, int priority, int count) {
        if (enabled()) {
            nebula_log("place-set: %s priority %d count %d\n", pName, priority, count);
        }
    }

    void placed(const char* pName, int zoneId) {
        if (enabled()) {
            nebula_log("place: %u %s zone %d\n", sPlaced++, pName, zoneId);
        }
    }

    void placementDone() {
        if (enabled()) {
            nebula_log("place-end: %s %u objects random %08x\n", MR::getCurrentStageName(), sPlaced,
                         SingletonHolder< GameSystem >::get()->mObjHolder->mRandom.mSeed);
        }
        sPlaced = 0;
    }
}  // namespace NebulaLoadTrace
#else
namespace NebulaLoadTrace {
    void frame() {
    }

    void gameScene() {
    }

    void sorted(const char*, int, int) {
    }

    void placed(const char*, int) {
    }

    void placementDone() {
    }
}  // namespace NebulaLoadTrace
#endif
