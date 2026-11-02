#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/HashUtil.hpp"

class EventSequence;

typedef void (EventSequence::*EventFunc1)(u16 eventFrame, u16 sequenceFrame);
typedef bool (EventSequence::*EventFunc2)(u16 sequenceFrame);

class EventSequence {
private:
    u16 mEventNumMax;
    u16 mEventNum;

    u16 mPhase;
    u16 mFrameInPhase;

    // Event callbacks.
    EventFunc1* mEventFuncs;
    EventFunc2* mEventCheckFuncs;

    // Stores either the start frame of the event or a special value for status/phase events.
    u16* mStartFrameOrModes;

    // Stores either the stop frame of the event or the phase for phase events.
    u16* mStopFrameOrPhases;

    // How many frames has each event been running for?
    u16* mEventFrames;

    bool mStopSequence;
    bool mNextPhase;

public:
    EventSequence(u32 eventNumMax);

    virtual void updateBefore();
    virtual void updateAfter();

    /// @brief Resets the sequence.
    void clearFlag();

    /// @brief Adds an event to the sequence that runs at a specific frame.
    void addEventOnTime(const char* pName, EventFunc1 eventFn, u16 frame);

    /// @brief Adds an event to the sequence that runs between two frames (inclusive).
    void addEventInTime(const char* pName, EventFunc1 eventFn, u16 startFrame, u16 stopFrame);

    /// @brief Adds an event to the sequence that runs while a condition is met.
    void addEventInStatus(const char* pName, EventFunc1 eventFn, EventFunc2 eventCheckFn);

    /// @brief Adds an event to the sequence that runs during a phase.
    void addEventInPhase(const char* pName, EventFunc1 eventFn, u16 phase);

    bool checkAndRun(u32 sequenceFrame);
    void playSound(const char* pName);
    void playAnimation(const char* pName);
    void setSpot(f32, u32);

    bool isMissLayoutClosed(u16 sequenceFrame);
    void decLeft(u16 eventFrame, u16 sequenceFrame);
    void closeDefaultLayout(u16 eventFrame, u16 sequenceFrame);
    void doCloseWipe(u16 eventFrame, u16 sequenceFrame);
    void doWaitAfterWipe(u16 eventFrame, u16 sequenceFrame);

    void stopSequence() {
        mStopSequence = true;
    }

    void nextPhase() {
        mNextPhase = true;
    }

    u16 getPhase() const {
        return mPhase;
    }
};

class EventSequencer : public NameObj {
public:
    /// @brief Creates a new `EventSequencer`.
    EventSequencer();

    virtual ~EventSequencer();
    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();

    void startEvent(const char* pName);

    template < typename T >
    void addEventSequence(const char* pName) {
        mHashTable->add(pName, reinterpret_cast< HashSortValue >(new T()), false);
    }

private:
    HashSortTable* mHashTable;
    EventSequence* mSequence;
    u16 mSequenceFrame;
};
