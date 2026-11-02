#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class Note;

class NoteFairy : public LiveActor {
public:
    NoteFairy(const char*);

    virtual ~NoteFairy();
    virtual void init(const JMapInfoIter&);
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

    TVec3f _8C;
    TVec3f _98;
    f32 mAppearanceSpeed;
    s32 mTimeLimit;
    s32 mMelodyNoteNum;
    s32 _B0;
    s32 mSong;
    s32 mCompletionJingle;
    f32 mNoteCoord;
    Note** mNoteArray;
    Note* mLastGotNote;
    s32 mAppearanceType;
    bool _CC;
    bool _CD;
    bool mHasDemo;
    bool _CF;
    bool _D0;
    bool _D1;
    s32 mPowerStarSpawnType;
    ActorCameraInfo* mCameraInfo;
};
