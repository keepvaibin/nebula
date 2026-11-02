#pragma once

#include "Game/System/NerveExecutor.hpp"
#include "Game/System/SaveRequest.hpp"

class GameDataHolder;
class SaveErrorSequence;
class SaveDataHandler;
class SaveIcon;
class SysInfoWindow;
class UserFile;

namespace MR {
    class FunctorBase;
};  // namespace MR

class SaveDataHandleSequence : public NerveExecutor {
public:
    /// @brief Creates a new `SaveDataHandleSequence`.
    SaveDataHandleSequence();

    void initAfterResourceLoaded();
    void registerFunctorOnSaveSuccess(const MR::FunctorBase&);
    void registerFunctorJustBeforeSave(const MR::FunctorBase&);
    void update();
    void draw() const;
    void startPreLoad();
    void startCreateUserFile(int);
    void startDeleteUserFile(int);
    void startSave(bool, bool);
    void startSaveBackup(bool, bool);
    void startSaveAll();
    // The caller opens a system window when the save is done (the pause menu's question): the done message hands the
    // screen dim to it (SysInfoWindow::setDimHandOver). Until the next start.
    void setWindowFollows(bool follows) {
        mWindowFollows = follows;
    }
    void startLoad(int, bool);
    void storeMiiOrIconId(int, const void*, const u32*);
    void storeCopyUserFile(int, int);
    bool tryUnrecoverableError(SaveRequest::Result);
    bool isActive() const;
    bool isPermitToReset() const;
    void prepareReset();
    bool isPreparedReset() const;
    void restoreFromReset();
    bool isInitializedGameDataHolder() const;
    void restoreUserFile(UserFile*, int);
    void restoreUserFile(UserFile*, int, bool);
    void backupCurrentUserFile();
    void exeNoOperation();
    void exeCheckEnableToCreate();
    void exeSaveConfirm();
    void exeSave();
    void exeSaveWindowDisappear();
    void exeSaveDoneKeyWait();
    void exeSaveAllWithoutKeyWait();
    void exeSaveAllWithoutKeyWaitDisappear();
    void exeSaveAllWithoutWindow();
    void exePreLoad();
    void exePreLoadDone();
    void tryNextSaveCopy();
    void exeNoSaveConfirmRemind();
    void exeErrorHandling();
    GameDataHolder* getHolder();
    UserFile* getCurrentUserFile();
    UserFile* getBackupUserFile();
    void restoreUserFileConfigData(UserFile*, int);
    void restoreUserFileGameData(UserFile*, int, bool);
    bool trySave();
    bool trySaveWindowDisappear(bool*, const Nerve*);
    bool trySaveWithoutWindow(bool*, const Nerve*);
    bool tryConfirm(const char*, bool*);
    bool tryProcessDoneKeyWait(const char*);
    bool tryNoSave();
    bool isEnablePointer() const;
    bool executeSaveFinish(bool*, const Nerve*);
    void syncNoSaveFlagsFromErrorSequence();

    UserFile* mCurrentUserFile;
    UserFile* mBackupUserFile;
    SaveDataHandler* mSaveDataHandler;
    SaveErrorSequence* mSaveErrorSequence;
    SysInfoWindow* mSysInfoWindowConfirm;
    SysInfoWindow* mSysInfoWindowSave;
    s32 _24;
    bool mIsConfirmRemind;
    bool mIsSaveAndQuitMsg;
    bool _2A;
    bool _2B;
    bool _2C;
    UserFile* mWorkUserFile;
    const Nerve* mNerveForError;
    u8* mTempBuffer;
    MR::FunctorBase* mOnSaveSuccessFunc;
    MR::FunctorBase* mJustBeforeSaveFunc;
    SaveIcon* mSaveIcon;
    bool mWindowFollows;
    int mSaveCopyLoading = 0;
};
