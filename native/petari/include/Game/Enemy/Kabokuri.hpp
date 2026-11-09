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

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

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
    Quat4f mRotationQuat;
    Vec3f mFrontVec;
    bool mIsFloating;

    /// @brief Drops fire on every point of the rail reached, except the first one.
    bool mIsValidDropFire;
};
