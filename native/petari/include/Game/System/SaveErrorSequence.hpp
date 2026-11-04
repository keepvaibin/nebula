#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/System/SaveRequest.hpp"

class SaveDataHandleSequence;
class SysInfoWindow;

// What the game shows when its save cannot be used: a damaged file (start a new one, or play without saving), or a
// save the drive refused (said once; the game goes on).
class SaveErrorSequence : public ActorStateBase< SaveDataHandleSequence > {
public:
    SaveErrorSequence(SaveDataHandleSequence*, SysInfoWindow*, SysInfoWindow*);

    virtual void control();

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
    u32 mResult;
    SysInfoWindow* mSysInfoWindow;
};
