#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>


class CocoNut : public LiveActor {
public:
    CocoNut(const char*);

    virtual ~CocoNut();
    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void startClipped();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);


    void initMapToolInfo(const TableIter&);
    void initModel();
    void initSensor();
    void initEffect();
    void updateRotate(f32);
    void updateGravity();
    bool isInGroundGracePeriod() const {
        return mOffGroundStep < 10;
    }
    void processMove();
    bool isOnGround() const;
    f32 calcMoveSpeed() const;
    void setFrontVec(const Vec3f&);
    bool getWallNormal(Vec3f*) const;

    bool sendMsgToBindedSensor();
    bool sendMsgEnemyAttackToBindedSensor(HitSensor*);

    bool tryHit(HitSensor*, HitSensor*);
    bool isPossibleToHit(const Vec3f&, const Vec3f&, const Vec3f&) const;
    void calcHitSpeedAndFrontVec(f32*, f32*, Vec3f*, Vec3f*, const Vec3f&, const Vec3f&) const;
    void hit(const Vec3f&, f32);
    bool tryPushedFromActor(HitSensor*, HitSensor*);

    void reviseFrontVec();
    void statusToWait();

    bool isValidReceiveMsg(const HitSensor*) const;

    void statusToHide();
    void emitEffectSpinHit(const HitSensor*, const HitSensor*);

    void tryMoveEnd();

    bool tryDisappear();
    bool isContactWithOtherCocoNut() const;

    bool isValidPushedFromPlayer(const HitSensor*, const HitSensor*) const;

    void calcHitSpeedAndFrontVec(f32*, f32*, Vec3f*, Vec3f*, Vec3f*, Vec3f*) const;

    static const char* getModelName();
    static const char* getBreakEffectName();

    void exeWait();
    void exeWaitOnBind();
    void exeMove();
    void exeInWater();
    void exeBreak();
    void exeReplaceReady();

    inline f32 getSize() {
        return mRadius;
    }

private:
    f32 mMoveSpeed;
    f32 mFallSpeed;
    Vec3f mFront;
    Pos3f mRollMtx;
    f32 mRadius;
    bool mIsBindOff;
    Pos3f mSmokeMtx;
    Pos3f mSpinHitMtx;
    s32 mOffGroundStep;
    bool mIsKicked;
    Vec3f mSpawnPosition;
    bool mIsOnMoveArea;
    Vec3f mAreaVelocity;
    bool mSphericalShadow;  // also affects some other properties besides shadow
    bool mRespawnWhenOutOfView;
    bool mIsGroupClipping;  // something about clipping
    bool mContinueRolling;
};
