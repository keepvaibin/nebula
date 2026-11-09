#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class JetTurtle;
class MapPartsRailPointPassChecker;
class WalkerStateBindStarPointer;

class NokonokoLand : public LiveActor {
public:
    NokonokoLand(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initJetTurtle();
    bool isEnableAttack() const;
    bool isStateWalk() const;
    bool isStateShell() const;
    bool isStateShellStart() const;
    bool isStateShellWait() const;
    bool isStateRattle() const;
    bool isStateBindStarPointer() const;
    bool isStateAttack() const;
    bool isLookDirRailDirection() const;
    bool tryAttack(HitSensor*, HitSensor*);
    bool tryJetTurtleTouch();
    bool tryRattle();
    bool tryBindStarPointer();
    bool receiveTrample(HitSensor*, HitSensor*);
    bool receivePunch(HitSensor*, HitSensor*);
    bool receiveHipDrop(HitSensor*, HitSensor*);
    bool receiveTurtleAttack(HitSensor*, HitSensor*);
    bool receiveStorm(HitSensor*, HitSensor*);
    bool receiveStarPieceAttack(HitSensor*, HitSensor*);
    void exeAppearReady();
    void exeAppearStart();
    void endAppearStart();
    void tearAppearStart();
    void exeAppear();
    void exeFlyAway();
    void exeDead();
    void exeWalk();
    void exeLookAround();
    void exeTurnStart();
    void exeTurnLoop();
    void exeTurnEnd();
    void exeAttack();
    void exeRattle();
    void exeBindStarPointer();
    void endBindStarPointer();
    void exeShellStartTrample();
    void exeShellStartPunch();
    void exeShellWait();
    void exeShellEnd();
    void exeBecomeJetTurtle();

    JetTurtle* mJetTurtle;
    MapPartsRailPointPassChecker* mRailPointPassChecker;
    AnimScaleController* mAnimScaleController;
    WalkerStateBindStarPointer* mBindStarPointerState;
    s32 mTurtleType;
    Vec3f mAppearPos;
    Quat4f mTargetQuat;
    Vec3f mTurnAxis;
};
