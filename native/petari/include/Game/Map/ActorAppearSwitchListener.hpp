#pragma once


class LiveActor;

class SwitchEventListener {
public:
    virtual void refresh(bool useOn) {
        if (useOn) {
            listenSwitchOnEvent();
        } else {
            listenSwitchOffEvent();
        }
    }

    virtual void listenSwitchOnEvent() = 0;
    virtual void listenSwitchOffEvent() = 0;
};

class ActorAppearSwitchListener : public SwitchEventListener {
public:
    ActorAppearSwitchListener(LiveActor*, bool, bool);

    virtual void listenSwitchOnEvent();
    virtual void listenSwitchOffEvent();

    LiveActor* mActor;
    bool mUsesOn;
    bool mUsesOff;
};
