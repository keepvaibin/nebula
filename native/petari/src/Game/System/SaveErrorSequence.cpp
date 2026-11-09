#include "Game/System/SaveErrorSequence.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    NEW_NERVE(SaveErrorSequenceIdle, SaveErrorSequence, Idle);
    NEW_NERVE(SaveErrorSequenceError, SaveErrorSequence, Error);
    NEW_NERVE(SaveErrorSequenceDamagedConfirm, SaveErrorSequence, DamagedConfirm);
    NEW_NERVE(SaveErrorSequenceNoSaveConfirm, SaveErrorSequence, NoSaveConfirm);

    MR::GrowBuffer< char16_t > sText;
};  // namespace

SaveErrorSequence::SaveErrorSequence(SaveDataHandleSequence* pHost, SysInfoWindow* pConfirm)
    : ActorStateBase("SaveError", pHost), mResult(Result_None), mSysInfoWindow(pConfirm) {
    initNerve(GET_NERVE_ANON(SaveErrorSequenceIdle));
    kill();
}

void SaveErrorSequence::startSaveFailed() {
    appear();
    mResult = Result_None;
    setNerve(GET_NERVE_ANON(SaveErrorSequenceError));
}

void SaveErrorSequence::startDamaged() {
    appear();
    mResult = Result_None;
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
    mResult = Result_None;

    setNerve(GET_NERVE_ANON(SaveErrorSequenceIdle));
    kill();
}

void SaveErrorSequence::exeIdle() {
}

// The drive refused: said once; the game goes on.
void SaveErrorSequence::exeError() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appearKeyWithText(NebulaText::get(&sText, "SaveFailed"));
    }

    if (MR::isDead(mSysInfoWindow)) {
        kill();
    }
}

void SaveErrorSequence::exeDamagedConfirm() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appearYesNoWithText(NebulaText::get(&sText, "SaveDamaged"));
        MR::startSystemSE("SE_SY_SAVE_CONFIRM_INFO");
    }

    if (!MR::isDead(mSysInfoWindow)) {
        return;
    }

    if (mSysInfoWindow->isSelectedYes()) {
        mResult = Result_NewFile;
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
        mResult = Result_NoSave;
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
    return mResult == Result_NoSave;
}
