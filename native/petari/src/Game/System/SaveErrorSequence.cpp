#include "Game/System/SaveErrorSequence.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    NEW_NERVE(SaveErrorSequenceIdle, SaveErrorSequence, Idle);
    NEW_NERVE(SaveErrorSequenceError, SaveErrorSequence, Error);
    NEW_NERVE(SaveErrorSequenceDamagedConfirm, SaveErrorSequence, DamagedConfirm);
    NEW_NERVE(SaveErrorSequenceNoSaveConfirm, SaveErrorSequence, NoSaveConfirm);

    char16_t sText[256];
};  // namespace

SaveErrorSequence::SaveErrorSequence(SaveDataHandleSequence* pHost, SysInfoWindow* pConfirm, SysInfoWindow* pSave)
    : ActorStateBase("SaveError", pHost), mResult(0), mSysInfoWindow(pConfirm) {
    (void)pSave;
    initNerve(GET_NERVE_ANON(SaveErrorSequenceIdle));
    kill();
}

void SaveErrorSequence::startSaveFailed() {
    appear();
    mResult = 0;
    setNerve(GET_NERVE_ANON(SaveErrorSequenceError));
}

void SaveErrorSequence::startDamaged() {
    appear();
    mResult = 0;
    setNerve(GET_NERVE_ANON(SaveErrorSequenceDamagedConfirm));
}

bool SaveErrorSequence::tryUnrecoverable(SaveRequest::Result result) {
    if (result == SaveRequest::Result_Failed) {
        startSaveFailed();
        return true;
    }

    return false;
}

void SaveErrorSequence::restoreFromReset() {
    mResult = 0;

    setNerve(GET_NERVE_ANON(SaveErrorSequenceIdle));
    kill();
}

void SaveErrorSequence::exeIdle() {
}

// The drive refused: said once; the game goes on.
void SaveErrorSequence::exeError() {
    if (MR::isFirstStep(this)) {
        NebulaText::get(sText, ARRAY_SIZE(sText), "SaveFailed");
        mSysInfoWindow->appearKeyWithText(sText);
    }

    if (MR::isDead(mSysInfoWindow)) {
        kill();
    }
}

void SaveErrorSequence::exeDamagedConfirm() {
    if (MR::isFirstStep(this)) {
        NebulaText::get(sText, ARRAY_SIZE(sText), "SaveDamaged");
        mSysInfoWindow->appearYesNoWithText(sText);
        MR::startSystemSE("SE_SY_SAVE_CONFIRM_INFO");
    }

    if (!MR::isDead(mSysInfoWindow)) {
        return;
    }

    if (mSysInfoWindow->isSelectedYes()) {
        mResult = 1;
        kill();
    } else {
        setNerve(GET_NERVE_ANON(SaveErrorSequenceNoSaveConfirm));
    }
}

void SaveErrorSequence::exeNoSaveConfirm() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appear("System_Save04", SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        MR::startSystemSE("SE_SY_SAVE_CONFIRM_INFO");
    }

    if (!MR::isDead(mSysInfoWindow)) {
        return;
    }

    if (mSysInfoWindow->isSelectedYes()) {
        mResult = 3;
        kill();
    } else {
        setNerve(GET_NERVE_ANON(SaveErrorSequenceDamagedConfirm));
    }
}

bool SaveErrorSequence::isEnablePointer() const {
    return isNerve(GET_NERVE_ANON(SaveErrorSequenceError)) || isNerve(GET_NERVE_ANON(SaveErrorSequenceDamagedConfirm)) ||
           isNerve(GET_NERVE_ANON(SaveErrorSequenceNoSaveConfirm));
}

bool SaveErrorSequence::isResultFileCorrupted() const {
    return mResult == 3;
}

void SaveErrorSequence::control() {
}
