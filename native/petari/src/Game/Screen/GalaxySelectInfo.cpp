#include "Game/Screen/GalaxySelectInfo.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/GalaxyInfoLayoutSetter.hpp"
#include "Game/Screen/IconComet.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/ValueControl.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(s32, cFadeinoutFrame, 5);
};  // namespace

namespace NrvGalaxySelectInfo {
    NEW_NERVE(GalaxySelectInfoNrvWait, GalaxySelectInfo, Wait);
    NEW_NERVE(GalaxySelectInfoNrvDisappear, GalaxySelectInfo, Disappear);
};  // namespace NrvGalaxySelectInfo

GalaxySelectInfo::GalaxySelectInfo(bool param1)
    : LayoutActor("ギャラクシー選択情報", true), mIsOnPause(param1), mShownGalaxyName(nullptr), mIsShownThisFrame(false), mFadeinoutControl(nullptr), mInfoLayoutSetter(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< GalaxySelectInfo >);
    mFadeinoutControl = new ValueControl(::cFadeinoutFrame);
    mFadeinoutControl->setZero();
}

void GalaxySelectInfo::init(const TableIter& /*rIter*/) {
    initLayoutManager("GalaxyInfo", GalaxyInfoLayoutSetter::getAnimLayerNum());
    MR::setInfluencedAlphaToChild(this);

    mInfoLayoutSetter = new GalaxyInfoLayoutSetter(this);

    if (mIsOnPause) {
        MR::connectToSceneLayoutOnPause(this);
        MR::connectToSceneLayoutOnPauseMovementCalcAnim(mInfoLayoutSetter->mIconComet);
    } else {
        MR::connectToSceneLayout(this);
        MR::connectToSceneLayoutMovementCalcAnim(mInfoLayoutSetter->mIconComet);
    }

    initNerve(GET_NERVE(GalaxySelectInfo, GalaxySelectInfoNrvWait));
}

void GalaxySelectInfo::draw() const {
    LayoutActor::draw();
    mInfoLayoutSetter->mIconComet->draw();
}

void GalaxySelectInfo::appear() {
    MR::startAnim(this, "Appear", 0);
    LayoutActor::appear();
    mInfoLayoutSetter->updateCometPos();
}

void GalaxySelectInfo::kill() {
    LayoutActor::kill();
    mInfoLayoutSetter->mIconComet->kill();
}

bool GalaxySelectInfo::show(const char* pParam1, u32 param2, int param3) {
    return show(pParam1, param2, true, param3);
}

void GalaxySelectInfo::control() {
    mIsShownThisFrame = false;

    mFadeinoutControl->update();

    if (mFadeinoutControl->mFrame == 0) {
        kill();
    }

    MR::setLayoutAlphaFloat(this, mFadeinoutControl->getValue());

    LayoutActor* pActor = mInfoLayoutSetter->mIconComet;
    MR::setLayoutAlphaFloat(pActor, mFadeinoutControl->getValue());

    mInfoLayoutSetter->updateCometPos();
}

bool GalaxySelectInfo::show(const char* pParam1, u32 param2, bool param3, int param4) {
    mIsShownThisFrame = true;

    if (MR::isDead(this) || !MR::isEqualString(mShownGalaxyName, pParam1)) {
        if (param3) {
            mInfoLayoutSetter->updateInfo(pParam1, GalaxyInfoLayoutSetter::GalaxyState(param2), param4);
        } else {
            mInfoLayoutSetter->updateInfo(pParam1, GalaxyInfoLayoutSetter::GalaxyState(param2));
        }

        mShownGalaxyName = pParam1;

        mFadeinoutControl->setDirToOne();
        setNerve(GET_NERVE(GalaxySelectInfo, GalaxySelectInfoNrvWait));
        appear();

        return true;
    }

    return false;
}

void GalaxySelectInfo::exeWait() {
    if (mIsShownThisFrame) {
        return;
    }

    mFadeinoutControl->setDirToZero();
    setNerve(GET_NERVE(GalaxySelectInfo, GalaxySelectInfoNrvDisappear));
}

void GalaxySelectInfo::exeDisappear() {
    if (mFadeinoutControl->mFrame == 0) {
        kill();
    }
}
