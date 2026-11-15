#include "Game/Screen/PlayerMissLeft.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/CountUpPaneRumbler.hpp"
#include "Game/Screen/CounterLayoutAppearer.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 hCountDownTime = 15;
    const s32 hEndTime = NebulaFrameClock::stepsOfMilliseconds(1500);
};  // namespace

namespace NrvPlayerMissLeft {
    NEW_NERVE(HostTypeNrvAppearWait, PlayerMissLeft, AppearWait);
    NEW_NERVE(HostTypeNrvAppear, PlayerMissLeft, Appear);
    NEW_NERVE(HostTypeNrvWait, PlayerMissLeft, Wait);
    NEW_NERVE(HostTypeNrvEnd, PlayerMissLeft, End);
};  // namespace NrvPlayerMissLeft

PlayerMissLeft::PlayerMissLeft(const char* pName) : LayoutActor(pName, true), mLayoutAppearer(), mPaneRumbler() {
    lumacore::setClass(this, lumacore::kClassId< PlayerMissLeft >);
}

void PlayerMissLeft::init(const TableIter& /*rIter*/) {
    initLayoutManager("PlayerLeft", 3);
    MR::createAndAddPaneCtrl(this, "CenterPlayerLeft", 1);

    mLayoutAppearer = new CounterLayoutAppearer(this, Vec2f(-50.0f, 0.0f));
    mPaneRumbler = new CountUpPaneRumbler(this, "CenterPlayerLeft");

    initNerve(GET_NERVE(PlayerMissLeft, HostTypeNrvAppearWait));
    kill();
}

void PlayerMissLeft::appear() {
    LayoutActor::appear();
    mLayoutAppearer->reset();
    mPaneRumbler->reset();
    MR::hidePaneRecursive(this, "PlayerLeft");
    setNerve(GET_NERVE(PlayerMissLeft, HostTypeNrvAppearWait));
    MR::hideLayout(this);
}

void PlayerMissLeft::control() {
    mLayoutAppearer->update();
    mPaneRumbler->update();
}

void PlayerMissLeft::exeAppearWait() {
    if (MR::isGreaterStep(this, 0)) {
        setNerve(GET_NERVE(PlayerMissLeft, HostTypeNrvAppear));
    }
}

void PlayerMissLeft::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::showLayout(this);
        MR::setTextBoxNumberRecursive(this, "CenterPlayerLeft", MR::getPlayerLeft() + 1);
        mLayoutAppearer->appear(Vec2f(0.0f, 0.0f));
        MR::startAnim(this, "Wait", 1);

        if (MR::isPlayerLuigi()) {
            MR::startAnim(this, "Luigi", 2);
            MR::setAnimFrameAndStop(this, 1.0f, 2);
        } else {
            MR::startAnim(this, "Luigi", 2);
            MR::setAnimFrameAndStop(this, 0.0f, 2);
        }
    }

    if (mLayoutAppearer->isAppeared()) {
        setNerve(GET_NERVE(PlayerMissLeft, HostTypeNrvWait));
    }
}

void PlayerMissLeft::exeWait() {
    if (MR::isStep(this, ::hCountDownTime)) {
        MR::setTextBoxNumberRecursive(this, "CenterPlayerLeft", MR::getPlayerLeft());
        mPaneRumbler->start();
        MR::startSystemSE("SE_SY_PLAYER_LEFT_DEC");
    }

    if (MR::isGreaterStep(this, ::hEndTime)) {
        setNerve(GET_NERVE(PlayerMissLeft, HostTypeNrvEnd));
    }
}

void PlayerMissLeft::exeEnd() {
    if (MR::isFirstStep(this)) {
        mLayoutAppearer->disappear();
    }

    if (mLayoutAppearer->isDisappeared()) {
        kill();
    }
}
