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
        return _138 < 10;
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
        return _D0;
    }

private:
    f32 _8C;
    f32 _90;
    Vec3f _94;
    Pos3f _A0;
    f32 _D0;
    bool _D4;
    Pos3f _D8;
    Pos3f _108;
    s32 _138;
    bool _13C;
    Vec3f mSpawnPosition;
    bool _14C;
    Vec3f _150;
    bool mSphericalShadow;  // also affects some other properties besides shadow
    bool mRespawnWhenOutOfView;
    bool _15E;  // something about clipping
    bool mContinueRolling;
};
