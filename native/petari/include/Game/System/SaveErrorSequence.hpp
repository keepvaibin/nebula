#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/System/SaveRequest.hpp"

class SaveDataHandleSequence;
class SysInfoWindow;

// What the game shows when its save cannot be used: a damaged file (start a new one, or play without saving), or a
// save the drive refused (said once; the game goes on).
class SaveErrorSequence : public ActorStateBase< SaveDataHandleSequence > {
public:
    SaveErrorSequence(SaveDataHandleSequence*, SysInfoWindow*);

    void startSaveFailed();
    void startDamaged();
    bool tryUnrecoverable(SaveRequest::Result);
    void restoreFromReset();
    void exeIdle();
    void exeError();
    void exeDamagedConfirm();
    void exeNoSaveConfirm();
    bool isEnablePointer() const;
    bool isResultFileCorrupted() const;

private:
    enum Result {
        Result_None = 0,
        Result_NewFile = 1,  // the damaged file is to be made again
        Result_NoSave = 3,   // play on without saving
    };

    u32 mResult;  // Result
    SysInfoWindow* mSysInfoWindow;
};
