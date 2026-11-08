#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class Note;

class NoteFairy : public LiveActor {
public:
    NoteFairy(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void killAllNotes();
    void exeHide();
    void exeStartAppearDemo();
    void exeRailMoveStart();
    void exeDemoAppear();
    void exeAppearNoteBloom();
    void exeAppearNote();
    void exeEndWait();
    void exeSuccessDemo();
    void exeSuccess();
    void enterDemoAppear(const Nerve*, bool);
    bool isDeadAllNotes();
    void setLastGotNote(Note*);
    f32 getNoteCoord(s32);

    Vec3f mInitPos;
    Vec3f mDemoJumpPos;
    f32 mAppearanceSpeed;
    s32 mTimeLimit;
    s32 mMelodyNoteNum;
    s32 mAppearedNoteNum;
    s32 mSong;
    s32 mCompletionJingle;
    f32 mNoteCoord;
    Note** mNoteArray;
    Note* mLastGotNote;
    s32 mAppearanceType;
    bool mIsDemoUntilAllNotes;
    bool mIsNoDemo;
    bool mHasDemo;
    bool mIsLowerBgm;
    bool mIsNoRiddleSE;
    bool mIsNoteAtRailPoints;
    s32 mPowerStarSpawnType;
    ActorCameraInfo* mCameraInfo;
};
