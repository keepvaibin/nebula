#include "Game/MapObj/CollectCounter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setPosition_f1, "setPosition.f1", 75.0f);

namespace NrvCollectCounter {
    NEW_NERVE(CollectCounterNrvHide, CollectCounter, Hide);
    NEW_NERVE(CollectCounterNrvShow, CollectCounter, Show);
};  // namespace NrvCollectCounter

CollectCounter::CollectCounter(const char* pName) : LayoutActor(pName, true), mFollowPosition(0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< CollectCounter >);
}

void CollectCounter::init(const TableIter& /*rIter*/) {
    MR::connectToSceneLayout(this);
    initLayoutManager("CollectCounter", 1);
    initNerve(GET_NERVE(CollectCounter, CollectCounterNrvHide));
    MR::setFollowPos(&mFollowPosition, this, 0);
    kill();
}

void CollectCounter::control() {
    setPosition();
}

void CollectCounter::exeHide() {
}

void CollectCounter::exeShow() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Appear", 0);
    }

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE(CollectCounter, CollectCounterNrvHide));
        kill();
    }
}

void CollectCounter::setCount(s32 num) {
    appear();
    setNerve(GET_NERVE(CollectCounter, CollectCounterNrvShow));
    MR::setTextBoxNumberRecursive(this, "CollectCounter", num);
}

void CollectCounter::setPosition() {
    f32 pointRadius = MR::calcPointRadius2D(*MR::getPlayerCenterPos(), cNum_setPosition_f1);
    Vec2f screenPos;
    MR::calcScreenPosition(&screenPos, *MR::getPlayerCenterPos());
    mFollowPosition.x = screenPos.x;
    mFollowPosition.y = screenPos.y - pointRadius;
}
