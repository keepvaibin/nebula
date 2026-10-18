#include "Game/Map/LavaShellTower.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    static const f32 sCamShakeIntensity = 0.1f;
    static const f32 sCamShakeSpeed = 2.2f;
    // static const f32 sCamStartOffsetY = _;
    // static const f32 sCamEndOffsetY = _;
};  // namespace

namespace NrvLavaShellTower {
    NEW_NERVE(HostTypeWait, LavaShellTower, Wait);
    NEW_NERVE(HostTypeDemo, LavaShellTower, Demo);
    NEW_NERVE(HostTypeDone, LavaShellTower, Done);
};  // namespace NrvLavaShellTower

LavaShellTower::LavaShellTower(const char* pName) : MapObjActor(pName) {
}

void LavaShellTower::init(const JMapInfoIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupHioNode("\222\156\214\140\203\111\203\165\203\127\203\106" /* 地形オブジェ */);
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupSound(4);
    info.setupGroupClipping(0x40);
    info.setupNerve(GET_NERVE(LavaShellTower, HostTypeWait));
    initialize(rIter, info);
    MR::registerDemoActionNerve(this, GET_NERVE(LavaShellTower, HostTypeDemo), "\212\112\216\156" /* 開始 */);
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
    MR::tryRumblePadStrong(this, WPAD_CHAN0);

    if (MR::isGreaterStep(this, 1) && MR::isDemoLastStep()) {
        MR::stopShakingCamera(this);
        setNerve(GET_NERVE(LavaShellTower, HostTypeDone));
    }
}

void LavaShellTower::exeDone() {
}
