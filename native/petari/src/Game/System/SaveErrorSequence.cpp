#include "Game/System/SaveErrorSequence.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/SaveDataHandler.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    NEW_NERVE(SaveErrorSequenceIdle, SaveErrorSequence, Idle);
    NEW_NERVE(SaveErrorSequenceError, SaveErrorSequence, Error);
    NEW_NERVE(SaveErrorSequenceRemoveConfirm, SaveErrorSequence, RemoveConfirm);
    NEW_NERVE(SaveErrorSequenceRemove, SaveErrorSequence, Remove);
    NEW_NERVE(SaveErrorSequenceRemoveDoneKeyWait, SaveErrorSequence, RemoveDoneKeyWait);
    NEW_NERVE(SaveErrorSequenceNoSaveConfirmBeforeRemove, SaveErrorSequence, NoSaveConfirmBeforeRemove);
    NEW_NERVE(SaveErrorSequenceCheckEnableToCreate, SaveErrorSequence, CheckEnableToCreate);
};  // namespace

SaveErrorSequence::SaveErrorSequence(SaveDataHandleSequence* pHost, SysInfoWindow* pParam2, SysInfoWindow*)
    : ActorStateBase("SaveError", pHost), mSystemMessageId(nullptr), _14(false), _15(false), mResult(0), mSysInfoWindow(pParam2) {
    initNerve(GET_NERVE_ANON(SaveErrorSequenceIdle));
    kill();
    resetParam();
}

void SaveErrorSequence::startErrorToMenu(const char* pSystemMessageId) {
    appear();
    resetParam();

    mSystemMessageId = pSystemMessageId;

    setNerve(GET_NERVE_ANON(SaveErrorSequenceError));
}

void SaveErrorSequence::startRemoveFile() {
    appear();
    resetParam();

    _15 = false;

    setNerve(GET_NERVE_ANON(SaveErrorSequenceRemoveConfirm));
}

void SaveErrorSequence::startCheckEnableToCreate() {
    appear();
    resetParam();
    setNerve(GET_NERVE_ANON(SaveErrorSequenceCheckEnableToCreate));
}

bool SaveErrorSequence::tryUnrecoverable(SaveRequest::Result result) {
    if (result == SaveRequest::Result_Failed) {
        startErrorToMenu("NAND_11_2");
        return true;
    }

    return false;
}

bool SaveErrorSequence::isPermitToReset() const {
    return true;
}

void SaveErrorSequence::prepareReset() {
    if (isNerve(GET_NERVE_ANON(SaveErrorSequenceError))) {
        GameSystemFunction::setResetOperationReturnToMenu();
    }
}

bool SaveErrorSequence::isPreparedReset() const {
    if (mIsDead) {
        return true;
    }

    if (isNerve(GET_NERVE_ANON(SaveErrorSequenceIdle)) || isNerve(GET_NERVE_ANON(SaveErrorSequenceError)) ||
        isNerve(GET_NERVE_ANON(SaveErrorSequenceRemoveConfirm)) || isNerve(GET_NERVE_ANON(SaveErrorSequenceRemoveDoneKeyWait)) ||
        isNerve(GET_NERVE_ANON(SaveErrorSequenceNoSaveConfirmBeforeRemove))) {
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

void SaveErrorSequence::exeError() {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appear(mSystemMessageId, SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        GameSystemFunction::setResetOperationReturnToMenu();
    }

    if (MR::isDead(mSysInfoWindow)) {
        GameSystemFunction::requestGoWiiMenu(false);
    }
}

void SaveErrorSequence::exeRemoveConfirm() {
    bool isSelectedYes = false;

    if (!tryConfirm("NAND_07", &isSelectedYes)) {
        return;
    }

    if (isSelectedYes) {
        setNerve(GET_NERVE_ANON(SaveErrorSequenceRemove));
    } else {
        setNerve(GET_NERVE_ANON(SaveErrorSequenceNoSaveConfirmBeforeRemove));
    }
}

void SaveErrorSequence::exeRemove() {
    if (MR::isFirstStep(this)) {
        mHost->mSaveDataHandler->requestRemoveSaveData();
    }

    if (!mHost->mSaveDataHandler->isDone()) {
        return;
    }

    const SaveRequest::Result result = mHost->mSaveDataHandler->getLastResult();

    if (result == SaveRequest::Result_Done || result == SaveRequest::Result_NoFile) {
        setNerve(GET_NERVE_ANON(SaveErrorSequenceRemoveDoneKeyWait));
    } else {
        tryUnrecoverable(result);
    }
}

void SaveErrorSequence::exeRemoveDoneKeyWait() {
    if (tryProcessDoneKeyWait("System_Save05")) {
        setNerve(GET_NERVE_ANON(SaveErrorSequenceCheckEnableToCreate));
    }
}

void SaveErrorSequence::exeNoSaveConfirmBeforeRemove() {
    bool isSelectedYes = false;

    if (!tryConfirm("System_Save04", &isSelectedYes)) {
        return;
    }

    if (isSelectedYes) {
        mResult = 3;

        kill();
    } else {
        setNerve(GET_NERVE_ANON(SaveErrorSequenceRemoveConfirm));
    }
}

// The host's disk has room for a new save: the request round trip, then done.
void SaveErrorSequence::exeCheckEnableToCreate() {
    if (MR::isFirstStep(this)) {
        mHost->mSaveDataHandler->requestCheckEnableToCreate();
    }

    if (!mHost->mSaveDataHandler->isDone()) {
        return;
    }

    mResult = 1;

    kill();
}

bool SaveErrorSequence::isEnablePointer() const {
    if (isNerve(GET_NERVE_ANON(SaveErrorSequenceError)) ||
        isNerve(GET_NERVE_ANON(SaveErrorSequenceRemoveConfirm)) || isNerve(GET_NERVE_ANON(SaveErrorSequenceRemoveDoneKeyWait)) ||
        isNerve(GET_NERVE_ANON(SaveErrorSequenceNoSaveConfirmBeforeRemove))) {
        return true;
    }

    return false;
}

bool SaveErrorSequence::isResultFileCorrupted() const {
    return mResult == 3;
}

void SaveErrorSequence::control() {
}

void SaveErrorSequence::resetParam() {
    mSystemMessageId = nullptr;
    _14 = false;
    _15 = false;
    mResult = 0;
}

bool SaveErrorSequence::tryConfirm(const char* pSystemMessageId, bool* pIsSelectedYes) {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appear(pSystemMessageId, SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        MR::startSystemSE("SE_SY_SAVE_CONFIRM_INFO");
    }

    if (MR::isDead(mSysInfoWindow)) {
        *pIsSelectedYes = mSysInfoWindow->isSelectedYes();

        return true;
    }

    return false;
}

bool SaveErrorSequence::tryProcessDoneKeyWait(const char* pSystemMessageId) {
    if (MR::isFirstStep(this)) {
        mSysInfoWindow->appear(pSystemMessageId, SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (MR::isDead(mSysInfoWindow)) {
        return true;
    }

    return false;
}
