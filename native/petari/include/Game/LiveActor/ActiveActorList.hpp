#pragma once


class LiveActor;

class ActiveActorList {
public:
    ActiveActorList(int);

    bool hasTooMany() const {
        return (mCurCount >= mMaxCount);
    }

    bool isFull() const;
    void addActor(LiveActor*);
    void removeDeadActor();
    void clear();
    void killAll();

    LiveActor** mActorList;
    s32 mCurCount;
    int mMaxCount;
};