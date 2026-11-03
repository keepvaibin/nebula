#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"

class HitSensor;
class StarPiece;
class StarPieceDirector;
struct StarPieceHostInfo;
struct StarPieceReceiverInfo;
class StarPieceShooter;

namespace MR {
    StarPieceDirector* getStarPieceDirector();

    void createStarPiece();
    StarPiece* getDeadStarPiece();

    void resetChasingStarPiece();
    bool isResetChasingStarPiece();

    bool gotByPlayer();

    bool giftStarPieceToTarget(HitSensor*, u32);

    void incNumStarPieceGettable(s32);
    void incNumStarPieceNewed();

};  // namespace MR

class StarPieceDirector : public LiveActorGroup {
public:
    StarPieceDirector(const char*);

    virtual ~StarPieceDirector();

    virtual void movement();

    void declare(const NameObj*, s32);
    void declareReceiveNum(const NameObj*, s32);
    s32 getDeclareRemnantStarPieceCount(const NameObj*);
    s32 getDeclareRemnantStarPieceCountNotExist(const NameObj*);
    bool hopPiece(const NameObj*, const TVec3f&, const TVec3f&);
    bool appearPiece(const NameObj*, const TVec3f&, s32, f32, f32, bool, bool);
    bool appearPieceToDirection(const NameObj*, const TVec3f&, const TVec3f&, s32, f32, f32, bool, bool);
    bool gotByPlayer();
    void updateSound();
    void initCSDelay();
    void initCSSound();
    bool giftToTarget(HitSensor*, u32);
    StarPiece* getDeadStarPiece();
    TVec3f calcPosCameraShoot(s32);
    void clearGotCountReceiver(const NameObj*);
    StarPieceHostInfo* findHostInfo(const NameObj*);
    StarPieceReceiverInfo* findReceiverInfo(const NameObj*);
    void calcShootGoalUsingPointingDepth();

    u32 mNumStarPieceNewed;
    u32 mNumStarPieceGettable;
    s32 mColorIndex;
    s32 mNumStarPiecesLaunched;

    StarPieceHostInfo* mHostInfoArray[0x200];

    u32 mNewHostInfoIndex;

    StarPieceReceiverInfo* mReceiverInfoArray[0x40];
    u32 mNewReceiverInfoIndex;
    bool mResetChasingStarPiece;
    StarPieceShooter* mStarPieceShooters[2];

    // only fist 16 entries ever get used
    bool mGetSoundArray[0x20];

    s32 mSoundIndex;
    bool mQueueNewGetSound;
};

class StarPieceShooter : public LiveActor {
public:
    StarPieceShooter(s32, const char*);

    virtual ~StarPieceShooter();

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeLockOn();

    bool shoot();
    bool tryShoot();

    void calcShootGoalUsingPointingDepth();

    f32 _8C;
    HitSensor* _90;
    f32 _94;
    HitSensor* _98;
    f32 _9C;
    s32 _A0;
    s32 _A4;
    TVec3f _A8;
    TVec3f _B4;
};
