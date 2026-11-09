#pragma once

#include "Game/Boss/DodoryuStateBase.hpp"

class DodoryuStateLv1 : public DodoryuStateBase {
public:
    DodoryuStateLv1(Dodoryu* pHost, const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    virtual void start() override;
    virtual void catchAttackSensor(HitSensor*, HitSensor*) override;
    virtual void catchHipDrop() override;
    virtual bool catchPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual void catchStartClipped() override;

    bool isAttackable() const;
    void executeHideJump();
    void startPop();
    void updatePop();
    bool isHipDropReactionJumpOut() const;
    bool isHipDropReactionPop() const;
    bool isHipDropReactionNothing() const;
    void setRailSpeed(f32);
    void startAnimAppear();
    void startAnimWalk();
    void startAnimHide();
    void startAnimEffectWalkAppear();
    void startAnimEffectWalk();
    void startAnimEffectWalkHide();

    void exeHide();
    void endHide();
    void exeAppearSign();
    void exeAppearStart();
    void endAppearStart();
    void exeAppear();
    void endAppear();
    void exeAppearEnd();
    void exeJumpOut();
    void exeDamage();
    void exeDive();
    void exeSpinOut();

    s32 mHideStep;
    s32 mAppearStep;
    Vec3f mKnockDir;
    bool mIsSpinCountered;
    bool mIsValidPop;
    f32 mPopAccel;
    f32 mPopSpeed;
    f32 mRailSpeed;
    f32 mRailSpeedMax;
};
