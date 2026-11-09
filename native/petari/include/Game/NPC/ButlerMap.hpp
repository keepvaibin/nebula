#pragma once

#include "Game/NPC/NPCActor.hpp"

class ButlerStateStarPieceReaction;

class ButlerMap : public NPCActor {
public:
    ButlerMap(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void startLectureDemo();
    void resetStatus();
    void forceNerveToWait();

    void exeTalk();
    void exeShowGalaxyMap();
    void exeLectureDemoShowMapBefore();
    void exeLectureDemoShowMap();
    void exeLectureDemoShowMapAfter();
    void exeStarPieceReaction();

    ButlerStateStarPieceReaction* mStateStarPieceReaction;
    bool mObjArg0;
};
