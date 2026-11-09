#pragma once

#include "Game/Util/Array.hpp"
#include "Game/Util/DueRing.hpp"

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

    StarPiece* getDeadStarPiece();

    void resetChasingStarPiece();
    bool isResetChasingStarPiece();

    bool gotByPlayer();

    bool giftStarPieceToTarget(HitSensor*, u32);


};  // namespace MR

class StarPieceDirector : public LiveActorGroup {
public:
    StarPieceDirector(const char*);

    virtual void movement() override;

    void declare(const NameObj*, s32);
    void declareReceiveNum(const NameObj*, s32);
    s32 getDeclareRemnantStarPieceCount(const NameObj*);
    s32 getDeclareRemnantStarPieceCountNotExist(const NameObj*);
    bool hopPiece(const NameObj*, const Vec3f&, const Vec3f&);
    bool appearPiece(const NameObj*, const Vec3f&, s32, f32, f32, bool, bool);
    bool appearPieceToDirection(const NameObj*, const Vec3f&, const Vec3f&, s32, f32, f32, bool, bool);
    bool gotByPlayer();
    void updateSound();
    void initCSDelay();
    void initCSSound();
    bool giftToTarget(HitSensor*, u32);
    StarPiece* getDeadStarPiece();
    void createStarPieces(unsigned count = 70);
    Vec3f calcPosCameraShoot(s32);
    void clearGotCountReceiver(const NameObj*);
    StarPieceHostInfo* findHostInfo(const NameObj*);
    StarPieceReceiverInfo* findReceiverInfo(const NameObj*);
    void calcShootGoalUsingPointingDepth();

    s32 mColorIndex;
    s32 mNumStarPiecesLaunched;

    MR::Vector< MR::AssignableArray< StarPieceHostInfo* > > mHostInfos;
    MR::Vector< MR::AssignableArray< StarPieceReceiverInfo* > > mReceiverInfos;
    bool mResetChasingStarPiece;
    StarPieceShooter* mStarPieceShooters[2];

    // The gets waiting for their sound: the update step each is due at.
    MR::DueRing mGetSounds;
    u32 mSoundStep;  // updates since initCSSound
    bool mQueueNewGetSound;
};

class StarPieceShooter : public LiveActor {
public:
    StarPieceShooter(s32, const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeLockOn();

    bool shoot();
    bool tryShoot();

    void calcShootGoalUsingPointingDepth();

    f32 mShootSpeed;
    HitSensor* mSearchSensor;
    f32 mSearchDist;
    HitSensor* mLockOnSensor;
    s32 mPort;
    s32 mChargeStep;
    Vec3f mEffectPos;
    Vec3f mEffectRot;
};
