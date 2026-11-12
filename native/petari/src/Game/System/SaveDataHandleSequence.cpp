#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/LumaSave.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/SaveIcon.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/SaveErrorSequence.hpp"
#include "Game/System/SaveRequest.hpp"
#include "Game/System/SaveDataHandler.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/Tunable.hpp"

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_trySave_1, "trySave.1", 20);

namespace {
    NEW_NERVE(SaveDataHandleSequenceNoOperation, SaveDataHandleSequence, NoOperation);
    NEW_NERVE(SaveDataHandleSequenceSaveConfirm, SaveDataHandleSequence, SaveConfirm);
    NEW_NERVE(SaveDataHandleSequenceSave, SaveDataHandleSequence, Save);
    NEW_NERVE(SaveDataHandleSequenceSaveWindowDisappear, SaveDataHandleSequence, SaveWindowDisappear);
    NEW_NERVE(SaveDataHandleSequenceSaveDoneKeyWait, SaveDataHandleSequence, SaveDoneKeyWait);
    NEW_NERVE(SaveDataHandleSequenceSaveAllWithoutKeyWait, SaveDataHandleSequence, SaveAllWithoutKeyWait);
    NEW_NERVE(SaveDataHandleSequenceSaveAllWithoutKeyWaitDisappear, SaveDataHandleSequence, SaveAllWithoutKeyWaitDisappear);
    NEW_NERVE(SaveDataHandleSequenceSaveAllWithoutWindow, SaveDataHandleSequence, SaveAllWithoutWindow);
    NEW_NERVE(SaveDataHandleSequencePreLoad, SaveDataHandleSequence, PreLoad);
    NEW_NERVE(SaveDataHandleSequencePreLoadDone, SaveDataHandleSequence, PreLoadDone);
    NEW_NERVE(SaveDataHandleSequenceNoSaveConfirmRemind, SaveDataHandleSequence, NoSaveConfirmRemind);
    NEW_NERVE(SaveDataHandleSequenceErrorHandling, SaveDataHandleSequence, ErrorHandling);
};  // namespace

SaveDataHandleSequence::SaveDataHandleSequence()
    : NerveExecutor("セーブ/ロード"), mCurrentUserFile(), mBackupUserFile(), mSaveDataHandler(), mSaveErrorSequence(),
      mSysInfoWindowConfirm(), mSysInfoWindowSave(), mSucceeded(), mIsConfirmRemind(), mIsSaveAndQuitMsg(), mIsCorrupted(), mIsNoSaveMode(), mWorkUserFile(),
      mNerveForError(), mSaveIcon(), mWindowFollows(false) {
    initNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

void SaveDataHandleSequence::initAfterResourceLoaded() {
    mCurrentUserFile = new UserFile();
    mBackupUserFile = new UserFile();
    mSaveDataHandler = new SaveDataHandler(mCurrentUserFile);

    mSysInfoWindowConfirm = MR::createSysInfoWindowExecuteWithChildren();
    mSysInfoWindowConfirm->kill();

    mSysInfoWindowSave = MR::createSysInfoWindowMiniExecuteWithChildren();
    mSysInfoWindowSave->kill();

    mSaveErrorSequence = new SaveErrorSequence(this, mSysInfoWindowConfirm);

    mSaveIcon = new SaveIcon(mSysInfoWindowSave);
    mSaveIcon->kill();
}

void SaveDataHandleSequence::update() {
    updateNerve();

    if (mSysInfoWindowConfirm != nullptr) {
        mSysInfoWindowConfirm->movement();
        mSysInfoWindowConfirm->calcAnim();
    }

    if (mSysInfoWindowSave != nullptr) {
        mSysInfoWindowSave->movement();
        mSysInfoWindowSave->calcAnim();
        mSaveIcon->movement();
        mSaveIcon->calcAnim();
    }

    if (isEnablePointer()) {
        MR::requestStarPointerModeSaveLoad(this);
    }
}

void SaveDataHandleSequence::draw() const {
    if (mSysInfoWindowConfirm != nullptr) {
        mSysInfoWindowConfirm->draw();
    }

    if (mSysInfoWindowSave != nullptr) {
        mSysInfoWindowSave->draw();
        mSaveIcon->draw();
    }
}

void SaveDataHandleSequence::startPreLoad() {
    setNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoad));
}

void SaveDataHandleSequence::startCreateUserFile(int index) {
    restoreUserFile(mCurrentUserFile, index, true);
    mCurrentUserFile->resetAllData();
    mCurrentUserFile->setCreated();
    mCurrentUserFile->updateLastModified();
    mSaveDataHandler->initializeUserFileMemory(index, mCurrentUserFile);
    luma::save::fileLoaded();  // the new file is the one in play, as a picked one is (services-save 1)
    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWait));
}

void SaveDataHandleSequence::startDeleteUserFile(int index) {
    restoreUserFile(mCurrentUserFile, index, true);
    mCurrentUserFile->resetAllData();
    mSaveDataHandler->initializeUserFileMemory(index, mCurrentUserFile);
    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWait));
}

void SaveDataHandleSequence::startSave(bool isConfirmRemind, bool isSaveAndQuitMsg) {
    mIsConfirmRemind = isConfirmRemind;
    mIsSaveAndQuitMsg = isSaveAndQuitMsg;
    mWindowFollows = false;
    mWorkUserFile = mCurrentUserFile;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm));
}

void SaveDataHandleSequence::startSaveBackup(bool isConfirmRemind, bool isSaveAndQuitMsg) {
    mIsConfirmRemind = isConfirmRemind;
    mIsSaveAndQuitMsg = isSaveAndQuitMsg;
    mWindowFollows = false;
    mWorkUserFile = mBackupUserFile;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm));
}

void SaveDataHandleSequence::startSaveAll() {
    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWait));
}

void SaveDataHandleSequence::startLoad(int userFileIndex, bool isPlayerMario) {
    restoreUserFile(mCurrentUserFile, userFileIndex, isPlayerMario);
    luma::save::fileLoaded();

    mSucceeded = true;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

void SaveDataHandleSequence::storeMiiOrIconId(int userFileIndex, const void* pMiiId, const u32* pIconId) {
    restoreUserFile(mCurrentUserFile, userFileIndex);
    mCurrentUserFile->setMiiOrIconId(pMiiId, pIconId);
    mCurrentUserFile->updateLastModified();
    mSaveDataHandler->storeUserFile(mCurrentUserFile);
}

void SaveDataHandleSequence::storeCopyUserFile(int indexDst, int indexSrc) {
    mSaveDataHandler->copyUserFileMemory(indexDst, indexSrc);
}

bool SaveDataHandleSequence::tryUnrecoverableError(SaveRequest::Result result) {
    if (mSaveErrorSequence->tryUnrecoverable(result)) {
        mNerveForError = GET_NERVE_ANON(SaveDataHandleSequenceNoOperation);

        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling));

        return true;
    }

    return false;
}

bool SaveDataHandleSequence::isActive() const {
    return !isNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

bool SaveDataHandleSequence::isPreparedReset() const {
    return mSaveDataHandler == nullptr || mSaveDataHandler->isDone();
}

void SaveDataHandleSequence::restoreFromReset() {
    mSaveErrorSequence->restoreFromReset();

    if (!MR::isDead(mSysInfoWindowConfirm)) {
        mSysInfoWindowConfirm->forceKill();
    }

    if (!MR::isDead(mSysInfoWindowSave)) {
        mSysInfoWindowSave->forceKill();
    }

    mSaveIcon->kill();
    mSucceeded = false;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

bool SaveDataHandleSequence::isInitializedGameDataHolder() const {
    return mCurrentUserFile != nullptr;
}

void SaveDataHandleSequence::restoreUserFile(UserFile* pUserFile, int index) {
    restoreUserFileConfigData(pUserFile, index);
    restoreUserFileGameData(pUserFile, index, pUserFile->isLastLoadedMario());
}

void SaveDataHandleSequence::restoreUserFile(UserFile* pUserFile, int index, bool isPlayerMario) {
    restoreUserFileConfigData(pUserFile, index);
    restoreUserFileGameData(pUserFile, index, isPlayerMario);
    pUserFile->setLastLoadedMario(isPlayerMario);
}

void SaveDataHandleSequence::backupCurrentUserFile() {
    mSaveDataHandler->copyUserFile(mBackupUserFile, mCurrentUserFile);
}

void SaveDataHandleSequence::exeNoOperation() {
    mIsConfirmRemind = false;
    mIsSaveAndQuitMsg = false;
    mIsNoSaveMode = false;
}

void SaveDataHandleSequence::exeSaveConfirm() {
    if (mIsNoSaveMode) {
        mWorkUserFile->setCreated();
        mWorkUserFile->updateLastModified();
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSave));
    } else {
        bool isSelectedYes = false;
        const char* const pSystemMessageId = mIsSaveAndQuitMsg ? "System_Save07" : "System_Save00";

        if (!tryConfirm(pSystemMessageId, &isSelectedYes)) {
            // Yes opens Saving...; No, the reminder when there is one.
            if (MR::isFirstStep(this)) {
                mSysInfoWindowConfirm->setDimHandOver((mIsCorrupted ? SysInfoWindow::DimHandOver_None : SysInfoWindow::DimHandOver_Yes) |
                                                      (mIsConfirmRemind ? SysInfoWindow::DimHandOver_No : SysInfoWindow::DimHandOver_None));
            }
            return;
        }

        if (isSelectedYes) {
            mWorkUserFile->setCreated();
            mWorkUserFile->updateLastModified();
            setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSave));
        } else if (mIsConfirmRemind) {
            setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoSaveConfirmRemind));
        } else {
            setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));

            mSucceeded = false;
        }
    }
}

void SaveDataHandleSequence::exeSave() {
    if (MR::isFirstStep(this)) {
        mSaveDataHandler->storeUserFile(mWorkUserFile);
    }

    if (tryNoSave()) {
        return;
    }

    if (mIsCorrupted) {
        bool isErr = true;

        executeSaveFinish(&isErr, GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm));
    } else if (trySave()) {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveWindowDisappear));
    }
    // Saving... is followed by the done message (or an error's window).
    else if (MR::isFirstStep(this)) {
        mSysInfoWindowSave->setDimHandOver(SysInfoWindow::DimHandOver_Always);
    }
}

void SaveDataHandleSequence::exeSaveWindowDisappear() {
    bool isErr = false;

    if (!trySaveWindowDisappear(&isErr, GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm))) {
        return;
    }

    if (isErr) {
        return;
    }

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveDoneKeyWait));
}

void SaveDataHandleSequence::exeSaveDoneKeyWait() {
    if (!tryProcessDoneKeyWait("System_Save02")) {
        if (MR::isFirstStep(this) && mWindowFollows) {
            mSysInfoWindowSave->setDimHandOver(SysInfoWindow::DimHandOver_Always);
        }
        return;
    }

    mSucceeded = true;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

void SaveDataHandleSequence::exeSaveAllWithoutKeyWait() {
    if (tryNoSave()) {
        return;
    }

    if (mIsCorrupted) {
        bool isErr = true;

        executeSaveFinish(&isErr, GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWait));
    } else if (trySave()) {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWaitDisappear));
    }
}

void SaveDataHandleSequence::exeSaveAllWithoutKeyWaitDisappear() {
    bool isErr = false;

    if (!trySaveWindowDisappear(&isErr, GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWait))) {
        return;
    }

    if (isErr) {
        return;
    }

    mSucceeded = true;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

void SaveDataHandleSequence::exeSaveAllWithoutWindow() {
    if (tryNoSave()) {
        return;
    }

    if (mIsCorrupted) {
        bool isErr = true;

        executeSaveFinish(&isErr, GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWait));
    } else {
        bool isErr = false;

        if (!trySaveWithoutWindow(&isErr, GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutWindow))) {
            return;
        }

        if (isErr) {
            return;
        }

        mSucceeded = true;

        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
    }
}

// The save, then its copies, then an old save: the first that can be read. Nothing there starts new; only damaged
// files, and the player is asked.
void SaveDataHandleSequence::exePreLoad() {
    if (MR::isFirstStep(this)) {
        if (!mSaveDataHandler->requestLoadSaveData(mSaveCopyLoading)) {
            mSaveCopyLoading = 0;
            if (mSaveDamaged) {
                mSaveDamaged = false;
                mNerveForError = GET_NERVE_ANON(SaveDataHandleSequenceNoOperation);
                mSaveErrorSequence->startDamaged();
                setNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling));
            } else {
                mSucceeded = true;
                setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
            }
            return;
        }
    }

    if (!mSaveDataHandler->isDone()) {
        return;
    }

    const SaveRequest::Result result = mSaveDataHandler->getLastResult();

    if (result == SaveRequest::Result_Done) {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoadDone));
    } else {
        if (result != SaveRequest::Result_NoFile) {
            mSaveDamaged = true;
        }
        tryNextSaveCopy();
    }
}

void SaveDataHandleSequence::tryNextSaveCopy() {
    mSaveCopyLoading++;
    setNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoad));
}

void SaveDataHandleSequence::exePreLoadDone() {
    if (!mSaveDataHandler->acceptLoaded()) {
        mSaveDamaged = true;
        tryNextSaveCopy();
    } else {
        mSaveCopyLoading = 0;
        mSaveDamaged = false;
        mSucceeded = true;

        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
    }
}

void SaveDataHandleSequence::exeNoSaveConfirmRemind() {
    bool isSelectedYes = false;

    if (!tryConfirm("ConfirmEndGame", &isSelectedYes)) {
        // No asks the save's question again.
        if (MR::isFirstStep(this)) {
            mSysInfoWindowConfirm->setDimHandOver(SysInfoWindow::DimHandOver_No);
        }
        return;
    }

    if (isSelectedYes) {
        mSucceeded = true;

        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
    } else {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm));
    }
}

void SaveDataHandleSequence::exeErrorHandling() {
    mSaveErrorSequence->update();

    if (!mSaveErrorSequence->mIsDead) {
        return;
    }

    syncNoSaveFlagsFromErrorSequence();
    setNerve(mNerveForError);

    mNerveForError = nullptr;
}

GameDataHolder* SaveDataHandleSequence::getHolder() {
    return mCurrentUserFile->mGameDataHolder;
}

UserFile* SaveDataHandleSequence::getCurrentUserFile() {
    return mCurrentUserFile;
}

UserFile* SaveDataHandleSequence::getBackupUserFile() {
    return mBackupUserFile;
}

void SaveDataHandleSequence::restoreUserFileConfigData(UserFile* pUserFile, int index) {
    const SaveFileId file = {SaveFileId::Kind_Config, index};
    u32 size;
    const u8* const pData = mSaveDataHandler->getFile(file, &size);
    pUserFile->loadFromConfigDataBinary(file, pData, size);
}

void SaveDataHandleSequence::restoreUserFileGameData(UserFile* pUserFile, int index, bool isPlayerMario) {
    const SaveFileId file = {isPlayerMario ? SaveFileId::Kind_Mario : SaveFileId::Kind_Luigi, index};
    u32 size;
    const u8* const pData = mSaveDataHandler->getFile(file, &size);
    pUserFile->loadFromGameDataBinary(file, pData, size);

    pUserFile->mIsPlayerMario = isPlayerMario;
}

bool SaveDataHandleSequence::trySave() {
    if (MR::isFirstStep(this)) {
        mSaveDataHandler->requestSaveSaveData();
        mSysInfoWindowSave->appear("System_Save01", SysInfoWindow::Type_Blocking, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (MR::isDead(mSaveIcon) && mSysInfoWindowSave->isWait()) {
        mSaveIcon->appear();
    }

    if (MR::isGreaterStep(this, cStep_trySave_1)) {
        MR::startSystemLevelSE("SE_SY_LV_SAVING");
    }

    // The window has come up, so it does not flash.
    if (mSysInfoWindowSave->isWait() && mSaveDataHandler->isDone()) {
        return true;
    }

    return false;
}

bool SaveDataHandleSequence::trySaveWindowDisappear(bool* pIsErr, const Nerve* pNerveForError) {
    if (MR::isFirstStep(this)) {
        mSaveIcon->kill();
        mSysInfoWindowSave->disappear();
    }

    if (MR::isDead(mSysInfoWindowSave)) {
        return executeSaveFinish(pIsErr, pNerveForError);
    }

    return false;
}

bool SaveDataHandleSequence::trySaveWithoutWindow(bool* pIsErr, const Nerve* pNerveForError) {
    if (MR::isFirstStep(this)) {
        mSaveDataHandler->requestSaveSaveData();
    }

    if (mSaveDataHandler->isDone()) {
        return executeSaveFinish(pIsErr, pNerveForError);
    }

    return false;
}

bool SaveDataHandleSequence::tryConfirm(const char* pSystemMessageId, bool* pIsSelectedYes) {
    if (MR::isFirstStep(this)) {
        mSucceeded = false;

        mSysInfoWindowConfirm->appear(pSystemMessageId, SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        MR::startSystemSE("SE_SY_SAVE_CONFIRM_INFO");
    }

    if (MR::isDead(mSysInfoWindowConfirm)) {
        *pIsSelectedYes = mSysInfoWindowConfirm->isSelectedYes();

        return true;
    }

    return false;
}

bool SaveDataHandleSequence::tryProcessDoneKeyWait(const char* pSystemMessageId) {
    if (MR::isFirstStep(this)) {
        mSysInfoWindowSave->appear(pSystemMessageId, SysInfoWindow::Type_Key, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (MR::isDead(mSysInfoWindowSave)) {
        return true;
    }

    return false;
}

bool SaveDataHandleSequence::tryNoSave() {
    if (mIsNoSaveMode) {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));

        mSucceeded = true;

        return true;
    }

    return false;
}

bool SaveDataHandleSequence::isEnablePointer() const {
    if (isNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation)) || isNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoad)) ||
        isNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoadDone))) {
        return false;
    }

    return true;
}

bool SaveDataHandleSequence::executeSaveFinish(bool* pIsErr, const Nerve* pNerveForError) {
    SaveRequest::Result result = mSaveDataHandler->getLastResult();

    if (mIsCorrupted) {
        result = SaveRequest::Result_Corrupted;
    }

    if (result == SaveRequest::Result_Done) {
        MR::startSystemSE("SE_SY_SAVE_SUCCESS");

        *pIsErr = false;
        mIsCorrupted = false;

        return true;
    }

    *pIsErr = true;

    MR::startSystemSE("SE_SY_SAVE_FAILURE");

    if (result == SaveRequest::Result_Corrupted) {
        mNerveForError = pNerveForError;

        mSaveErrorSequence->startDamaged();
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling));

        return true;
    }

    if (tryUnrecoverableError(result)) {
        return true;
    }

    mNerveForError = GET_NERVE_ANON(SaveDataHandleSequenceNoOperation);

    mSaveErrorSequence->startSaveFailed();
    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling));

    return true;
}

void SaveDataHandleSequence::syncNoSaveFlagsFromErrorSequence() {
    bool isResultFileCorrupted = mSaveErrorSequence->isResultFileCorrupted();

    mIsCorrupted = isResultFileCorrupted;
    mIsNoSaveMode = isResultFileCorrupted;
}
