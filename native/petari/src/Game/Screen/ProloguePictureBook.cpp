#include "Game/Screen/ProloguePictureBook.hpp"
#include "Game/System/NebulaFrameGen.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE_LIST(s32, sBookPageInfo, , {0, 350, 700, 1050, 1400, 1748, -1});
};  // namespace

namespace NrvProloguePictureBook {
    NEW_NERVE(ProloguePictureBookActive, ProloguePictureBook, Active);
    NEW_NERVE(ProloguePictureBookPlaying, ProloguePictureBook, Playing);
    NEW_NERVE(ProloguePictureBookKeyWait, ProloguePictureBook, KeyWait);
    NEW_NERVE(ProloguePictureBookEnd, ProloguePictureBook, End);
};  // namespace NrvProloguePictureBook

ProloguePictureBook::ProloguePictureBook() : LayoutActor("プロローグの絵本", true), mIconAButton(), mPage() {
    lumacore::setClass(this, lumacore::kClassId< ProloguePictureBook >);
}

void ProloguePictureBook::init(const TableIter& /*rIter*/) {
    MR::connectToSceneLayout(this);
    initLayoutManager("PrologueDemo", 1);
    initNerve(GET_NERVE(ProloguePictureBook, ProloguePictureBookPlaying));

    mIconAButton = MR::createAndSetupIconAButton(this, true, false);

    kill();
}

void ProloguePictureBook::appear() {
    LayoutActor::appear();
    MR::requestMovementOn(mIconAButton);
    MR::startAnim(this, "Prologue", 0);
    setNerve(GET_NERVE(ProloguePictureBook, ProloguePictureBookPlaying));
}

void ProloguePictureBook::kill() {
    LayoutActor::kill();
    mIconAButton->kill();
}

void ProloguePictureBook::exeActive() {

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE(ProloguePictureBook, ProloguePictureBookEnd));
    }
}

void ProloguePictureBook::exePlaying() {
    FrameCtrl* animCtrl = MR::getAnimCtrl(this, 0);

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE(ProloguePictureBook, ProloguePictureBookEnd));
        return;
    }
    const s32& bookPageInfo = ::sBookPageInfo[mPage + 1];

    if (bookPageInfo < 0) {
        return;
    }

    MR::testSystemPadTriggerDecide();

    if (bookPageInfo > static_cast< s32 >(animCtrl->getFrame())) {
        return;
    }

    animCtrl->setFrame(bookPageInfo - 1.0f);

    setNerve(GET_NERVE(ProloguePictureBook, ProloguePictureBookKeyWait));
}

void ProloguePictureBook::exeKeyWait() {
    FrameCtrl* animCtrl = MR::getAnimCtrl(this, 0);

    if (MR::isFirstStep(this)) {
        mIconAButton->openWithoutMessage();
        animCtrl->setRate(0.0f);
    }

    if (MR::isPressedJump(PLAYER_1)) {
        MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");
        mIconAButton->term();

        mPage++;
        animCtrl->setRate(1.0f);

        setNerve(GET_NERVE(ProloguePictureBook, ProloguePictureBookPlaying));
    }
}

void ProloguePictureBook::exeEnd() {
}

bool ProloguePictureBook::isEnd() const {
    return isNerve(GET_NERVE(ProloguePictureBook, ProloguePictureBookEnd));
}

void ProloguePictureBook::control() {
    NebulaFrameGen::noteFullscreenUi();
    MR::setLayoutScalePosAtPaneScaleTrans(mIconAButton, this, "AButtonPosition");
}
