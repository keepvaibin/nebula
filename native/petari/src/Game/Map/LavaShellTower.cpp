#include "Game/Map/LavaShellTower.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeDemo_1, "exeDemo.1", 1);

namespace {
    NEBULA_TUNABLE(f32, sCamShakeIntensity, 0.1f);
    NEBULA_TUNABLE(f32, sCamShakeSpeed, 2.2f);
};  // namespace

namespace NrvLavaShellTower {
    NEW_NERVE(HostTypeWait, LavaShellTower, Wait);
    NEW_NERVE(HostTypeDemo, LavaShellTower, Demo);
    NEW_NERVE(HostTypeDone, LavaShellTower, Done);
};  // namespace NrvLavaShellTower

LavaShellTower::LavaShellTower(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< LavaShellTower >);
}

void LavaShellTower::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupSound(4);
    info.setupGroupClipping(0x40);
    info.setupNerve(GET_NERVE(LavaShellTower, HostTypeWait));
    initialize(rIter, info);
    MR::registerDemoActionNerve(this, GET_NERVE(LavaShellTower, HostTypeDemo), "開始");
    makeActorAppeared();
}

void LavaShellTower::exeWait() {
}

void LavaShellTower::exeDemo() {
    if (MR::isFirstStep(this)) {
        MR::shakeCameraInfinity(this, ::sCamShakeIntensity, ::sCamShakeSpeed);
        MR::overlayWithPreviousScreen(2);
    }

    MR::startSystemLevelSE("SE_DM_LV_SHELL_TOWER");
    MR::tryRumblePadStrong(this, PLAYER_1);

    if (MR::isGreaterStep(this, cStep_exeDemo_1) && MR::isDemoLastStep()) {
        MR::stopShakingCamera(this);
        setNerve(GET_NERVE(LavaShellTower, HostTypeDone));
    }
}

void LavaShellTower::exeDone() {
}
