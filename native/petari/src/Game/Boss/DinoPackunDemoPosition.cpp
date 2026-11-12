#include "Game/Boss/DinoPackunDemoPosition.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sLandMotionStartTime = 28;
    const s32 sJumpBackTime = 56;
};  // namespace

namespace NrvDinoPackunDemoPosition {
    NEW_NERVE(DinoPackunDemoPositionNrvOpeningDemo, DinoPackunDemoPosition, OpeningDemo);
};  // namespace NrvDinoPackunDemoPosition

DinoPackunDemoPosition::DinoPackunDemoPosition(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< DinoPackunDemoPosition >);
}

void DinoPackunDemoPosition::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initNerve(GET_NERVE(DinoPackunDemoPosition, DinoPackunDemoPositionNrvOpeningDemo));
    initModelManagerWithAnm("DinoPackunDemoPosition", nullptr, false);
    MR::invalidateClipping(this);
    makeActorDead();
}

void DinoPackunDemoPosition::startOpeningDemo() {
    setNerve(GET_NERVE(DinoPackunDemoPosition, DinoPackunDemoPositionNrvOpeningDemo));
    makeActorAppeared();
}

void DinoPackunDemoPosition::endDemo() {
    makeActorDead();
}

void DinoPackunDemoPosition::exeOpeningDemo() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "OpeningDemo");
        MR::startBckPlayer("CocoonFly");
    }

    if (MR::isStep(this, ::sLandMotionStartTime)) {
        MR::startBckPlayer("LandStiffen");
    }

    if (MR::isStep(this, ::sJumpBackTime)) {
        MR::startBckPlayer("JumpBack");
    }

    calcAnim();
    MR::setPlayerBaseMtx(MR::getJointMtx(this, "MarioPosition"));
}
