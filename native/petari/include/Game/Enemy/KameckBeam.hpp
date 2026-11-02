#pragma once

#include "Game/Util/CollisionPartsFilter.hpp"

class KameckBeamEventListener;
class KameckTurtle;
class KameckFireBall;

class KameckBeam : public LiveActor {
public:
    enum BeamType {
        BeamType_None,
        BeamType_Turtle,
        BeamType_FireBall1,
        BeamType_FireBall2,
        BeamType_FireBall3,
    };

    /// @brief Creates a new `KameckBeam`.
    /// @param pName A pointer to the null-terminated name of the object.
    KameckBeam(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAnim();
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void setWandLocalPosition(const TVec3f&);
    void setBeamKind(s32);
    void setEventListener(KameckBeamEventListener*);
    void resetBeam();
    bool requestFollowWand(MtxPtr, f32);
    void requestShootToPlayerGround(f32);
    void requestShootToPlayerCenter(f32);
    void requestShoot(const TVec3f&, f32);
    bool requestStorm(HitSensor*, HitSensor*);
    bool tryShootEnd();
    bool tryChangeTurtle();
    bool tryChangeFire();
    void exeFollowWand();
    void exeShoot();
    void exeExplosion();
    void exeJetTurtle();
    void exeFire();
    void startBeamLevelSound();
    void emitBeamReadyEffect();
    void emitBeamEffect();

    KameckBeamEventListener* mEventListener;
    KameckTurtle* mKameckTurtle;
    KameckFireBall* mKameckFireBalls[3];
    MtxPtr mFollowMtx;
    TVec3f _A4;
    TVec3f mWandLocalPosition;
    s32 mBeamKind;
    u8 _C0[4];
};

class KameckBeamEventListener {
public:
    /// @brief Creates a new `KameckBeamEventListener`.
    KameckBeamEventListener();

    virtual void hitBeam(s32){};
};

class KameckBeamCollisionFilter : public CollisionPartsFilterBase {
public:
    KameckBeamCollisionFilter(const TVec3f*, f32);

    virtual bool isInvalidParts(const CollisionParts*) const;

    const TVec3f* _4;
    f32 _8;
};

namespace MR {
    void setKameckBeamCollisionFilter(LiveActor*);
};  // namespace MR
