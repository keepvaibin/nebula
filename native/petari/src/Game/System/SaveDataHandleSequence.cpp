#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/SaveIcon.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/SaveErrorSequence.hpp"
#include "Game/System/SaveRequest.hpp"
#include "Game/System/SaveDataHandler.hpp"
#include "Game/System/SysConfigFile.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
#include <cstdio>

namespace {
    NEW_NERVE(SaveDataHandleSequenceNoOperation, SaveDataHandleSequence, NoOperation);
    NEW_NERVE(SaveDataHandleSequenceCheckEnableToCreate, SaveDataHandleSequence, CheckEnableToCreate);
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
    : NerveExecutor("\203\132\201\133\203\165/\203\215\201\133\203\150" /* セーブ/ロード */), mSysConfigFile(), mCurrentUserFile(), mBackupUserFile(), mSaveDataHandler(), mSaveErrorSequence(),
      mSysInfoWindowConfirm(), mSysInfoWindowSave(), _24(), mIsConfirmRemind(), mIsSaveAndQuitMsg(), _2A(), _2B(), _2C(), mWorkUserFile(),
      mNerveForError(), mTempBuffer(), mOnSaveSuccessFunc(), mJustBeforeSaveFunc(), mSaveIcon()
#ifdef TARGET_PC
      ,
      mWindowFollows(false)
#endif
{
    mTempBuffer = new (32) u8[SaveDataHandler::getEnoughtTempBufferSize()];

    initNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

void SaveDataHandleSequence::initAfterResourceLoaded() {
    mSysConfigFile = new SysConfigFile();
    mCurrentUserFile = new UserFile();
    mBackupUserFile = new UserFile();
    mSaveDataHandler = new SaveDataHandler(mSysConfigFile, mCurrentUserFile);

    mSysInfoWindowConfirm = MR::createSysInfoWindowExecuteWithChildren();
    mSysInfoWindowConfirm->kill();

    mSysInfoWindowSave = MR::createSysInfoWindowMiniExecuteWithChildren();
    mSysInfoWindowSave->kill();

    mSaveErrorSequence = new SaveErrorSequence(this, mSysInfoWindowConfirm, mSysInfoWindowSave);

    mSaveIcon = new SaveIcon(mSysInfoWindowSave);
    mSaveIcon->kill();
}

void SaveDataHandleSequence::registerFunctorOnSaveSuccess(const MR::FunctorBase& rFunctor) {
    mOnSaveSuccessFunc = rFunctor.clone(nullptr);
}

void SaveDataHandleSequence::registerFunctorJustBeforeSave(const MR::FunctorBase& rFunctor) {
    mJustBeforeSaveFunc = rFunctor.clone(nullptr);
}

void SaveDataHandleSequence::update() {
    updateNerve();

    if (mSaveDataHandler != nullptr) {
        mSaveDataHandler->update();
    }

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
#ifdef TARGET_PC
    mWindowFollows = false;
#endif
    mWorkUserFile = mCurrentUserFile;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm));
}

void SaveDataHandleSequence::startSaveBackup(bool isConfirmRemind, bool isSaveAndQuitMsg) {
    mIsConfirmRemind = isConfirmRemind;
    mIsSaveAndQuitMsg = isSaveAndQuitMsg;
#ifdef TARGET_PC
    mWindowFollows = false;
#endif
    mWorkUserFile = mBackupUserFile;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm));
}

void SaveDataHandleSequence::startSaveAll() {
    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWait));
}

void SaveDataHandleSequence::startLoad(int userFileIndex, bool isPlayerMario) {
    restoreUserFile(mCurrentUserFile, userFileIndex, isPlayerMario);

    _24 = 2;

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

bool SaveDataHandleSequence::isPermitToReset() const {
    if (mSaveErrorSequence != nullptr) {
        return mSaveErrorSequence->isPermitToReset();
    }

    return true;
}

void SaveDataHandleSequence::prepareReset() {
    if (mSaveErrorSequence != nullptr) {
        mSaveErrorSequence->prepareReset();
    }
}

bool SaveDataHandleSequence::isPreparedReset() const {
    if (mSaveErrorSequence != nullptr && !mSaveErrorSequence->isPreparedReset()) {
        return false;
    }

    if (isNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation)) || isNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm)) ||
        isNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveDoneKeyWait)) || isNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoSaveConfirmRemind))) {
        return true;
    }

    if (isNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoad)) || isNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoadDone)) ||
        isNerve(GET_NERVE_ANON(SaveDataHandleSequenceSave)) || isNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveWindowDisappear)) ||
        isNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWait)) ||
        isNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutKeyWaitDisappear)) ||
        isNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveAllWithoutWindow))) {
        return false;
    }

    if (isNerve(GET_NERVE_ANON(SaveDataHandleSequenceCheckEnableToCreate)) || isNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling))) {
        return mSaveErrorSequence->isPreparedReset();
    }

    return false;
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
    _24 = 3;

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
    mCurrentUserFile->makeGameDataBinary(mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
    mBackupUserFile->loadFromGameDataBinary(mCurrentUserFile->getGameDataName(), mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
    mCurrentUserFile->makeConfigDataBinary(mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
    mBackupUserFile->loadFromConfigDataBinary(mCurrentUserFile->getConfigDataName(), mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
}

void SaveDataHandleSequence::exeNoOperation() {
    mIsConfirmRemind = false;
    mIsSaveAndQuitMsg = false;
    _2B = false;
}

void SaveDataHandleSequence::exeCheckEnableToCreate() {
    if (MR::isFirstStep(this)) {
        mSaveErrorSequence->startCheckEnableToCreate();
    }

    mSaveErrorSequence->update();

    if (!mSaveErrorSequence->mIsDead) {
        return;
    }

    syncNoSaveFlagsFromErrorSequence();
    _24 = 2;
    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

void SaveDataHandleSequence::exeSaveConfirm() {
    bool b = _2C || _2B;

    if (b) {
        mWorkUserFile->setCreated();
        mWorkUserFile->updateLastModified();
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSave));
    } else {
        bool isSelectedYes = false;
        const char* const pSystemMessageId = mIsSaveAndQuitMsg ? "System_Save07" : "System_Save00";

        if (!tryConfirm(pSystemMessageId, &isSelectedYes)) {
#ifdef TARGET_PC
            // Yes opens Saving...; No, the reminder when there is one.
            if (MR::isFirstStep(this)) {
                mSysInfoWindowConfirm->setDimHandOver((_2A ? SysInfoWindow::DimHandOver_None : SysInfoWindow::DimHandOver_Yes) |
                                                      (mIsConfirmRemind ? SysInfoWindow::DimHandOver_No : SysInfoWindow::DimHandOver_None));
            }
#endif
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

            _24 = 3;
        }
    }
}

void SaveDataHandleSequence::exeSave() {
    if (MR::isFirstStep(this)) {
        if (mJustBeforeSaveFunc != nullptr) {
            (*mJustBeforeSaveFunc)();
        }

        mSaveDataHandler->storeSysConfigFile(mSysConfigFile);
        mSaveDataHandler->storeUserFile(mWorkUserFile);
    }

    if (tryNoSave()) {
        return;
    }

    if (_2A) {
        bool isErr = true;

        executeSaveFinish(&isErr, GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm));
    } else if (trySave()) {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveWindowDisappear));
    }
#ifdef TARGET_PC
    // Saving... is followed by the done message (or an error's window).
    else if (MR::isFirstStep(this)) {
        mSysInfoWindowSave->setDimHandOver(SysInfoWindow::DimHandOver_Always);
    }
#endif
}

void SaveDataHandleSequence::exeSaveWindowDisappear() {
    bool isErr = false;

    if (!trySaveWindowDisappear(&isErr, GET_NERVE_ANON(SaveDataHandleSequenceSaveConfirm))) {
        return;
    }

    if (isErr) {
        return;
    }

    if (mOnSaveSuccessFunc != nullptr) {
        (*mOnSaveSuccessFunc)();
    }

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceSaveDoneKeyWait));
}

void SaveDataHandleSequence::exeSaveDoneKeyWait() {
    if (!tryProcessDoneKeyWait("System_Save02")) {
#ifdef TARGET_PC
        if (MR::isFirstStep(this) && mWindowFollows) {
            mSysInfoWindowSave->setDimHandOver(SysInfoWindow::DimHandOver_Always);
        }
#endif
        return;
    }

    _24 = 2;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

void SaveDataHandleSequence::exeSaveAllWithoutKeyWait() {
    if (MR::isFirstStep(this)) {
        if (mJustBeforeSaveFunc != nullptr) {
            (*mJustBeforeSaveFunc)();
        }

        mSaveDataHandler->storeSysConfigFile(mSysConfigFile);
    }

    if (tryNoSave()) {
        return;
    }

    if (_2A) {
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

    if (mOnSaveSuccessFunc != nullptr) {
        (*mOnSaveSuccessFunc)();
    }

    _24 = 2;

    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
}

void SaveDataHandleSequence::exeSaveAllWithoutWindow() {
    if (MR::isFirstStep(this)) {
        if (mJustBeforeSaveFunc != nullptr) {
            (*mJustBeforeSaveFunc)();
        }

        mSaveDataHandler->storeSysConfigFile(mSysConfigFile);
    }

    if (tryNoSave()) {
        return;
    }

    if (_2A) {
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

        if (mOnSaveSuccessFunc != nullptr) {
            (*mOnSaveSuccessFunc)();
        }

        _24 = 2;

        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
    }
}

void SaveDataHandleSequence::exePreLoad() {
    if (MR::isFirstStep(this)) {
        mSaveDataHandler->requestLoadSaveData();
    }

    if (!mSaveDataHandler->isDone()) {
        return;
    }

    const SaveRequest::Result result = mSaveDataHandler->getLastResult();

    if (result == SaveRequest::Result_Done) {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoadDone));
    } else if (result == SaveRequest::Result_NoFile) {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceCheckEnableToCreate));
    } else if (result == SaveRequest::Result_Corrupted) {
        mNerveForError = GET_NERVE_ANON(SaveDataHandleSequenceNoOperation);

        mSaveErrorSequence->startRemoveFile();
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling));
    } else if (tryUnrecoverableError(result)) {
        return;
    }
}

void SaveDataHandleSequence::exePreLoadDone() {
    if (!mSaveDataHandler->requestVerifyAfterLoadGameDataFile()) {
        mNerveForError = GET_NERVE_ANON(SaveDataHandleSequenceNoOperation);
        mSaveErrorSequence->startRemoveFile();

        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling));
    } else {
        restoreSysConfigFile(mSysConfigFile);
        _24 = 2;

        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));
    }
}

void SaveDataHandleSequence::exeNoSaveConfirmRemind() {
    bool isSelectedYes = false;

    if (!tryConfirm("ConfirmEndGame", &isSelectedYes)) {
#ifdef TARGET_PC
        // No asks the save's question again.
        if (MR::isFirstStep(this)) {
            mSysInfoWindowConfirm->setDimHandOver(SysInfoWindow::DimHandOver_No);
        }
#endif
        return;
    }

    if (isSelectedYes) {
        _24 = 2;

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

SysConfigFile* SaveDataHandleSequence::getSysConfigFile() {
    return mSysConfigFile;
}

UserFile* SaveDataHandleSequence::getCurrentUserFile() {
    return mCurrentUserFile;
}

UserFile* SaveDataHandleSequence::getBackupUserFile() {
    return mBackupUserFile;
}

void SaveDataHandleSequence_FORCE_MATCH_STRINGS() {
    MR::isEqualString("%s%1d", "%s%1d");
    MR::isEqualString("mario", "mario");
    MR::isEqualString("luigi", "luigi");
    MR::isEqualString("config%1d", "config%1d");
    MR::isEqualString("sysconf", "sysconf");
}

void SaveDataHandleSequence::restoreUserFileConfigData(UserFile* pUserFile, int index) {
    char dataName[16];
    snprintf(dataName, sizeof(dataName), "config%1d", index);

    mSaveDataHandler->restoreGameDataFile(dataName, mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
    pUserFile->loadFromConfigDataBinary(dataName, mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
}

void SaveDataHandleSequence::restoreUserFileGameData(UserFile* pUserFile, int index, bool isPlayerMario) {
    char dataName[16];
    snprintf(dataName, sizeof(dataName), "%s%1d", isPlayerMario ? "mario" : "luigi", index);

    mSaveDataHandler->restoreGameDataFile(dataName, mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
    pUserFile->loadFromGameDataBinary(dataName, mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());

    pUserFile->mIsPlayerMario = isPlayerMario;
}

void SaveDataHandleSequence::restoreSysConfigFile(SysConfigFile* pSysConfigFile) {
    const char* pName = "sysconf";
    mSaveDataHandler->restoreGameDataFile(pName, mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
    pSysConfigFile->loadFromDataBinary(mTempBuffer, SaveDataHandler::getEnoughtTempBufferSize());
}

bool SaveDataHandleSequence::trySave() {
    if (MR::isFirstStep(this)) {
        mSaveDataHandler->requestSaveSaveData();
        mSysInfoWindowSave->appear("System_Save01", SysInfoWindow::Type_Blocking, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
    }

    if (MR::isDead(mSaveIcon) && mSysInfoWindowSave->isWait()) {
        mSaveIcon->appear();
    }

    if (MR::isGreaterStep(this, 20)) {
        MR::startSystemLevelSE("SE_SY_LV_SAVING");
    }

    if (MR::isGreaterStep(this, 120) && mSaveDataHandler->isDone()) {
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
        _24 = 1;

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
    bool b = _2C || _2B;

    if (b) {
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation));

        _24 = 2;

        return true;
    }

    return false;
}

bool SaveDataHandleSequence::isEnablePointer() const {
    if (isNerve(GET_NERVE_ANON(SaveDataHandleSequenceNoOperation)) || isNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoad)) ||
        isNerve(GET_NERVE_ANON(SaveDataHandleSequencePreLoadDone))) {
        return false;
    }

    if (isNerve(GET_NERVE_ANON(SaveDataHandleSequenceCheckEnableToCreate)) && !mSaveErrorSequence->isEnablePointer()) {
        return false;
    }

    return true;
}

bool SaveDataHandleSequence::executeSaveFinish(bool* pIsErr, const Nerve* pNerveForError) {
    SaveRequest::Result result = mSaveDataHandler->getLastResult();

    if (_2A) {
        result = SaveRequest::Result_Corrupted;
    }

    if (result == SaveRequest::Result_Done) {
        MR::startSystemSE("SE_SY_SAVE_SUCCESS");

        *pIsErr = false;
        _2A = false;

        return true;
    }

    *pIsErr = true;

    MR::startSystemSE("SE_SY_SAVE_FAILURE");

    if (result == SaveRequest::Result_Corrupted) {
        mNerveForError = pNerveForError;

        mSaveErrorSequence->startRemoveFile();
        setNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling));

        return true;
    }

    if (tryUnrecoverableError(result)) {
        return true;
    }

    mNerveForError = GET_NERVE_ANON(SaveDataHandleSequenceNoOperation);

    mSaveErrorSequence->startErrorToMenu("NAND_11_2");
    setNerve(GET_NERVE_ANON(SaveDataHandleSequenceErrorHandling));

    return true;
}

void SaveDataHandleSequence::syncNoSaveFlagsFromErrorSequence() {
    // The console's "no room to create the save" answer, which a host's disk never gives.
    _2C = false;

    bool isResultFileCorrupted = mSaveErrorSequence->isResultFileCorrupted();

    _2A = isResultFileCorrupted;
    _2B = isResultFileCorrupted;
}
