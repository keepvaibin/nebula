#pragma once

#include "Game/Boss/DodoryuStateBase.hpp"

class DodoryuStateLv1 : public DodoryuStateBase {
public:
    DodoryuStateLv1(Dodoryu* pHost, const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    virtual void start();
    virtual void catchAttackSensor(HitSensor*, HitSensor*);
    virtual void catchHipDrop();
    virtual bool catchPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual void catchStartClipped();

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
    TVec3f _98;
    bool _A4;
    bool mIsValidPop;
    f32 mPopAccel;
    f32 mPopSpeed;
    f32 mRailSpeed;
    f32 mRailSpeedMax;
};
