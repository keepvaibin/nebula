#pragma once

#include "Game/NameObj/NameObj.hpp"

class StageSwitchCtrl;
class SwitchEventListener;

class SwitchWatcher : public NameObj {
public:
    SwitchWatcher(const StageSwitchCtrl*);

    virtual void movement();

    void checkSwitch(SwitchEventListener*, u32, bool);
    bool isSameSwitch(const StageSwitchCtrl*) const;
    void addSwitchListener(SwitchEventListener*, u32);

    u32 mFlags;
    const StageSwitchCtrl* mSwitchCtrl;
    SwitchEventListener* mSwitchListenerA;
    SwitchEventListener* mSwitchListenerB;
    SwitchEventListener* mSwitchListenerAppear;
};
