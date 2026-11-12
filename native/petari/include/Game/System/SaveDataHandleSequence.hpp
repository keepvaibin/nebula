#pragma once

#include "Game/System/NerveExecutor.hpp"
#include "Game/System/SaveRequest.hpp"

class GameDataHolder;
class SaveErrorSequence;
class SaveDataHandler;
class SaveIcon;
class SysInfoWindow;
class UserFile;

class SaveDataHandleSequence : public NerveExecutor {
public:
    SaveDataHandleSequence();

    void initAfterResourceLoaded();
    void update();
    void draw() const;
    void startPreLoad();
    void startCreateUserFile(int);
    void startDeleteUserFile(int);
    void startSave(bool, bool);
    void startSaveBackup(bool, bool);
    void startSaveAll();
    // LumaCore's FORGET (services-save 5): the stored files written as they are in memory, no window, no sound, no
    // question. False, nothing started: a sequence is running, the save was not read yet, or saves are off.
    bool startQuietSave();
    // True when startQuietSave never writes: the start read nothing usable, saves are off, or the save is damaged.
    bool isQuietSaveOff() const {
        return mPreLoadState == 2 || mIsNoSaveMode || mIsCorrupted;
    }
    void exeQuietSave();
    // A save was read at the start (1), or the start read none (2: no file, damaged, or saves off); 0 before.
    int getPreLoadState() const {
        return mPreLoadState;
    }
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
    bool isPreparedReset() const;
    void restoreFromReset();
    bool isInitializedGameDataHolder() const;
    void restoreUserFile(UserFile*, int);
    void restoreUserFile(UserFile*, int, bool);
    void backupCurrentUserFile();
    void exeNoOperation();
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
    // The last sequence ended done: saved, loaded, nothing to load, or the player went on without saving.
    bool mSucceeded;
    bool mIsConfirmRemind;
    bool mIsSaveAndQuitMsg;
    bool mIsCorrupted;
    bool mIsNoSaveMode;
    UserFile* mWorkUserFile;
    const Nerve* mNerveForError;
    SaveIcon* mSaveIcon;
    bool mWindowFollows;
    int mSaveCopyLoading = 0;
    bool mSaveDamaged = false;
    int mPreLoadState = 0;
};
