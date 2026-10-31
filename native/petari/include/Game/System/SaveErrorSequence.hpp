#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/System/SaveRequest.hpp"

class SaveDataHandleSequence;
class SysInfoWindow;

// What the game shows when its save file cannot be used: a corrupted file (remove it, or play without saving) and a
// save the host could not read or write (the error, then back to the menu).
class SaveErrorSequence : public ActorStateBase< SaveDataHandleSequence > {
public:
    SaveErrorSequence(SaveDataHandleSequence*, SysInfoWindow*, SysInfoWindow*);

    virtual void control();

    void startErrorToMenu(const char*);
    void startRemoveFile();
    void startCheckEnableToCreate();
    // A result no sequence recovers from: the error, then the menu. False for the others.
    bool tryUnrecoverable(SaveRequest::Result);
    bool isPermitToReset() const;
    void prepareReset();
    bool isPreparedReset() const;
    void restoreFromReset();
    void exeIdle();
    void exeError();
    void exeRemoveConfirm();
    void exeRemove();
    void exeRemoveDoneKeyWait();
    void exeNoSaveConfirmBeforeRemove();
    void exeCheckEnableToCreate();
    bool isEnablePointer() const;
    bool isResultFileCorrupted() const;
    void resetParam();
    bool tryConfirm(const char*, bool*);
    bool tryProcessDoneKeyWait(const char*);

private:
    /* 0x10 */ const char* mSystemMessageId;
    /* 0x14 */ bool _14;
    /* 0x15 */ bool _15;
    /* 0x1C */ u32 mResult;
    /* 0x20 */ SysInfoWindow* mSysInfoWindow;
};
