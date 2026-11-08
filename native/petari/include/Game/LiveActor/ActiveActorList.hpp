#pragma once

class LiveActor;

// The actors a launcher has out at once; the cap is how many shots the fight allows.
class ActiveActorList {
public:
    explicit ActiveActorList(s32 maxCount);

    bool isFull() const {
        return mCurCount >= mMaxCount;
    }

    // Ignored when full.
    void addActor(LiveActor* pActor);
    // Drops dead actors; the last actor takes each dropped slot.
    void removeDeadActor();
    void clear();
    void killAll();

private:
    LiveActor** mActorList;
    s32 mCurCount;
    s32 mMaxCount;
};
