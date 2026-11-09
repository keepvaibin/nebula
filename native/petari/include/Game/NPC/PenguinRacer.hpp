#pragma once

#include "Game/Map/RaceManager.hpp"
#include "Game/NPC/NPCActor.hpp"

class PenguinRacer : public NPCActor, public AbstractRacer {
public:
    PenguinRacer(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual void initRacer() override;
    virtual void prepRacer(const RaceManager* pRaceManager) override;
    virtual void startRacer() override;
    virtual bool updateRacer(const RaceManager* pRaceManager) override;
    virtual bool goalRacer() override;
    virtual void loseRacer() override;
    virtual void resetRacer(const RaceManager* pRaceManager) override;
    virtual void exitRacer() override;
    virtual const Nerve* getMoveNerve() const;
    virtual f32 getMoveSpeed() const;

    void initModel();
    void initSensor();
    void initShadow();
    void initBind();
    void initColor(const TableIter& rIter);
    void initEtc(const TableIter& rIter);

    bool tryReaction();

    void exeReaction();
    void exeWait();
    void exeRaceWait();
    void exeRaceWalk();
    void exeRaceSwim();
    void exeRaceStoW();
    void exeRaceWtoS();
    void exeRaceFall();
    void exeRaceGoal();
    void exeRaceTurn();

    void updateMove();
    void updateVelocity();
    void updateRail();

    bool isInRace() const;

    s32 mRaceDisabled;
    f32 mMoveSpeed;
    f32 mSpeed;
    s32 mRailPointNo;
};
