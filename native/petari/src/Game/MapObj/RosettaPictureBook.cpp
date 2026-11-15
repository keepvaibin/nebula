#include "Game/MapObj/RosettaPictureBook.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Screen/PictureBookLayout.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sFadeOutFrame = 60;
    const s32 sFadeInFrame = 60;
};  // namespace

namespace NrvRosettaPictureBook {
    NEW_NERVE(HostTypeNrvWait, RosettaPictureBook, Wait);
    NEW_NERVE(HostTypeNrvDemoWait, RosettaPictureBook, DemoWait);
    NEW_NERVE(HostTypeNrvFadeOut, RosettaPictureBook, FadeOut);
    NEW_NERVE(HostTypeNrvReading, RosettaPictureBook, Reading);
    NEW_NERVE(HostTypeNrvFadeIn, RosettaPictureBook, FadeIn);
};  // namespace NrvRosettaPictureBook

RosettaPictureBook::RosettaPictureBook(const char* pName) : LiveActor(pName), mLayout(), mIconAButton(), mIsValidOpenIconAButton() {
    lumacore::setClass(this, lumacore::kClassId< RosettaPictureBook >);
}

void RosettaPictureBook::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("RosettaPictureBook", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorMapObjSimple(this, "body", 8, 150.0f, Vec3f(0.0f, 0.0f, 0.0f));

    mLayout = new PictureBookLayout(1, MR::getPictureBookChapterCanRead(), false);
    mLayout->initWithoutIter();

    mIconAButton = new IconAButton(true, false);
    mIconAButton->initWithoutIter();

    initNerve(GET_NERVE(RosettaPictureBook, HostTypeNrvWait));
    MR::tryRegisterDemoCast(this, rIter);
    makeActorAppeared();
}

void RosettaPictureBook::appear() {
    mIsValidOpenIconAButton = false;

    setNerve(GET_NERVE(RosettaPictureBook, HostTypeNrvWait));
    LiveActor::appear();
}

void RosettaPictureBook::kill() {
    LiveActor::kill();
    mIconAButton->kill();
}

void RosettaPictureBook::control() {
    mIsValidOpenIconAButton = false;
}

void RosettaPictureBook::attackSensor(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isOnGroundPlayer()) {
        mIsValidOpenIconAButton = true;
    }
}

void RosettaPictureBook::exeWait() {
    if (mIsValidOpenIconAButton) {
        if (!mIconAButton->isOpen()) {
            MR::startSystemSE("SE_SY_TALK_BUTTON_APPEAR");
            mIconAButton->openWithRead();
        }
    } else if (mIconAButton->isOpen()) {
        mIconAButton->term();
    }

    if (!mIsValidOpenIconAButton || !mIconAButton->isOpen() || !MR::isPressedJump(PLAYER_1)) {
        return;
    }

    MR::requestStartDemoMarioPuppetableWithoutCinemaFrame(this, "ロゼッタ絵本デモ", GET_NERVE(RosettaPictureBook, HostTypeNrvFadeOut),
                                                          GET_NERVE(RosettaPictureBook, HostTypeNrvDemoWait));
}

void RosettaPictureBook::exeDemoWait() {
}

void RosettaPictureBook::exeFadeOut() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_TALK_START");
        MR::requestMovementOn(mIconAButton);
        mIconAButton->term();
        MR::closeWipeCircle(::sFadeOutFrame);
        MR::stopStageBGM(::sFadeOutFrame);
        MR::startBckPlayer("Wait");
    }

    if (MR::isGreaterStep(this, ::sFadeOutFrame)) {
        mLayout->appear();
        setNerve(GET_NERVE(RosettaPictureBook, HostTypeNrvReading));
    }
}

void RosettaPictureBook::exeReading() {
    if (MR::isFirstStep(this)) {
        mLayout->appear();
    }

    if (MR::isDead(mLayout)) {
        MR::endDemo(this, "ロゼッタ絵本デモ");
        setNerve(GET_NERVE(RosettaPictureBook, HostTypeNrvFadeIn));
    }
}

void RosettaPictureBook::exeFadeIn() {
    if (MR::isFirstStep(this)) {
        MR::openWipeCircle(::sFadeInFrame);
    }

    if (MR::isGreaterStep(this, ::sFadeInFrame)) {
        setNerve(GET_NERVE(RosettaPictureBook, HostTypeNrvWait));
    }
}
