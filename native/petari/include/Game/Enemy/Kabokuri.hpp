#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class Kuribo;
class ModelObj;
class WalkerStateBindStarPointer;

/// @brief Jack O'Goomba
class Kabokuri : public LiveActor {
public:
    Kabokuri(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void initSensor();
    void updatePose();
    void addVelocityBase();
    void addVelocityHitAttacked();
    void addVelocityToRailPoint(f32);
    bool requestTrampled();
    bool requestStarPieceHitted();
    bool requestHipDropped();
    bool requestHitAttacked(HitSensor*, HitSensor*);
    bool tryPointBind();
    void exeWait();
    void exeWalk();
    void exeDropFire();
    void exeAttacksuccess();
    void exeTrampled();
    void exeStarPieceHitted();
    void exeHipDropped();
    void exeHitAttacked();
    void exeBreak();
    void exeBindStarPointer();
    bool isEnableAttack() const;
    bool isEnablePointBind() const;
    bool isEnableTrampled() const;
    bool isEnablePush() const;
    void endBindStarPointer() const;

    Kuribo* mKuribo;
    ModelObj* mBreakModel;
    AnimScaleController* mAnimeScale;
    WalkerStateBindStarPointer* mStateBindStartPointer;
    TQuat4f mRotationQuat;
    TVec3f mFrontVec;
    s32 _B8;
    bool mIsFloating;

    /// @brief Drops fire on every point of the rail reached, except the first one.
    bool mIsValidDropFire;
};
