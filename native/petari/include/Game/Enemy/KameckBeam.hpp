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

    KameckBeam(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void calcAnim();
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void setWandLocalPosition(const Vec3f&);
    void setBeamKind(s32);
    void setEventListener(KameckBeamEventListener*);
    void resetBeam();
    bool requestFollowWand(MtxPtr, f32);
    void requestShootToPlayerGround(f32);
    void requestShootToPlayerCenter(f32);
    void requestShoot(const Vec3f&, f32);
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
    Vec3f mShootDir;
    Vec3f mWandLocalPosition;
    s32 mBeamKind;
};

class KameckBeamEventListener {
public:
    KameckBeamEventListener();

    virtual void hitBeam(s32){};
};

class KameckBeamCollisionFilter : public CollisionPartsFilterBase {
public:
    KameckBeamCollisionFilter(const Vec3f*, f32);

    virtual bool isInvalidParts(const CollisionParts*) const;

    const Vec3f* mClipCenter;
    f32 mClipRadius;
};

namespace MR {
    void setKameckBeamCollisionFilter(LiveActor*);
};  // namespace MR
