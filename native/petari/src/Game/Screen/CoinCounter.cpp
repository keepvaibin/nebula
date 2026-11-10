#include "Game/Screen/CoinCounter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/CountUpPaneRumbler.hpp"
#include "Game/Screen/CounterLayoutAppearer.hpp"
#include "Game/Screen/CounterLayoutController.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 50.0f);

namespace {
    NEBULA_TUNABLE(s32, cInvalidCountUpInterval, 3);
    NEBULA_TUNABLE(f32, cAstroLocationOffsetY, 40.0f);
};  // namespace

namespace NrvCoinCounter {
    NEW_NERVE(CoinCounterNrvHide, CoinCounter, Hide);
    NEW_NERVE(CoinCounterNrvAppear, CoinCounter, Appear);
    NEW_NERVE(CoinCounterNrvWait, CoinCounter, Wait);
    NEW_NERVE(CoinCounterNrvDisappear, CoinCounter, Disappear);
};  // namespace NrvCoinCounter

CoinCounter::CoinCounter(const char* pName)
    : LayoutActor(pName, true), mCoinNum(0), mCoinDisplayNum(0), mInvalidCountUpFrame(0), mLayoutAppearer(nullptr), mPaneRumbler(nullptr),
      mIsForceAppear(false), mFollowPos(0.0f, 0.0f) {
}

void CoinCounter::init(const TableIter& /*rIter*/) {
    initLayoutManager("CoinCounter", 2);
    MR::createAndAddPaneCtrl(this, "Counter", 1);
    MR::createAndAddPaneCtrl(this, "CoinCounter", 1);
    MR::setFollowPos(&mFollowPos, this, "CoinCounter");
    initEffectKeeper(0, nullptr, nullptr);

    mLayoutAppearer = new CounterLayoutAppearer(this, Vec2f(cNum_init_f1, 0.0f));
    mPaneRumbler = new CountUpPaneRumbler(this, "Counter");

    initNerve(GET_NERVE(CoinCounter, CoinCounterNrvHide));
    MR::connectToSceneLayout(this);
    appear();
}

void CoinCounter::appear() {
    mLayoutAppearer->reset();
    mPaneRumbler->reset();

    mInvalidCountUpFrame = 0;
    mIsForceAppear = false;

    MR::hideLayout(this);
    setNerve(GET_NERVE(CoinCounter, CoinCounterNrvHide));
    LayoutActor::appear();
}

void CoinCounter::forceAppear() {
    if (!isNerve(GET_NERVE(CoinCounter, CoinCounterNrvWait))) {
        appear();
        setNerve(GET_NERVE(CoinCounter, CoinCounterNrvAppear));
    }

    mIsForceAppear = true;
}

void CoinCounter::disappear() {
    mIsForceAppear = false;

    setNerve(GET_NERVE(CoinCounter, CoinCounterNrvDisappear));
}

bool CoinCounter::isWait() const {
    return isNerve(GET_NERVE(CoinCounter, CoinCounterNrvWait));
}

void CoinCounter::control() {
    updateCounter();
    mLayoutAppearer->update();
    mPaneRumbler->update();
}

void CoinCounter::updateCounter() {
    mCoinNum = MR::getCoinNum();

    if (mInvalidCountUpFrame > 0) {
        mInvalidCountUpFrame--;
    } else if (mCoinDisplayNum < mCoinNum) {
        if (isNerve(GET_NERVE(CoinCounter, CoinCounterNrvWait))) {
            mInvalidCountUpFrame = ::cInvalidCountUpInterval;
            mCoinDisplayNum++;

            MR::startAnim(this, "Flash", 0);
            MR::emitEffect(this, "CoinCounterLight");
            mPaneRumbler->start();
        }

        if (!isNerve(GET_NERVE(CoinCounter, CoinCounterNrvAppear))) {
            if (!isNerve(GET_NERVE(CoinCounter, CoinCounterNrvWait))) {
                setNerve(GET_NERVE(CoinCounter, CoinCounterNrvAppear));
            } else {
                setNerve(GET_NERVE(CoinCounter, CoinCounterNrvWait));
            }
        }
    }

    const char* pPaneName = "Position001";

    if (mCoinDisplayNum >= 100) {
        pPaneName = "Position100";
    } else if (mCoinDisplayNum >= 10) {
        pPaneName = "Position010";
    }

    MR::copyPaneTrans(&mFollowPos, this, pPaneName);
    MR::setTextBoxNumberRecursive(this, "Counter", mCoinDisplayNum);
}

void CoinCounter::exeHide() {
    if (MR::isFirstStep(this)) {
        mInvalidCountUpFrame = 0;

        MR::hideLayout(this);
    }
}

void CoinCounter::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::showLayout(this);

        if (MR::isStageAstroLocation()) {
            mLayoutAppearer->appear(Vec2f(0.0f, ::cAstroLocationOffsetY));
        } else {
            mLayoutAppearer->appear(Vec2f(0.0f, 0.0f));
        }

        MR::startAnim(this, "Wait", 1);
    }

    if (mLayoutAppearer->isAppeared()) {
        setNerve(GET_NERVE(CoinCounter, CoinCounterNrvWait));
    }
}

void CoinCounter::exeWait() {
    if (mIsForceAppear) {
        return;
    }

    if (mCoinDisplayNum != mCoinNum) {
        return;
    }

    if (CounterLayoutController::isWaitToDisappearCounter(this)) {
        setNerve(GET_NERVE(CoinCounter, CoinCounterNrvDisappear));
    }
}

void CoinCounter::exeDisappear() {
    if (MR::isFirstStep(this)) {
        mLayoutAppearer->disappear();
    }

    if (mLayoutAppearer->isDisappeared()) {
        setNerve(GET_NERVE(CoinCounter, CoinCounterNrvHide));
    }
}
