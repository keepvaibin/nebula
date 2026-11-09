#pragma once

#include "Game/NPC/NPCActor.hpp"

class ButlerStateStarPieceReaction;
class TableIter;
class TalkMessageCtrl;

class Butler : public NPCActor {
public:
    Butler(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void killIfBatlerMapAppear();
    void startDemoButlerReport(const char*);
    void startDemoDomeLecture1();
    void startDemoDomeLecture2();
    void startDemoStarPiece1();
    void startDemoStarPiece2();
    void tryStartShowGalaxyMap();
    void resetStatus();
    bool messageBranchFunc(u32);
    void initTalkCtrlArray(const TableIter&);
    void initForAstroDome(const TableIter&);
    void initForAstroGalaxy(const TableIter&);
    TalkMessageCtrl* createTalkCtrl(const TableIter&, const char*);
    void forceNerveToWait();
    void tryReplaceStarPieceIfExecLecture();
    bool tryStartStarPieceReaction();
    void exeStarPieceReaction();
    void exeDemo();
    void exeDemoDomeLecture2();
    void exeDemoStarPiece2();
    void exeDemoShowGalaxyMap();
    inline void exeDemoWait();

    TalkMessageCtrl* getTalkMessage(s32 index) const {
        return mTalkMessage[index];
    }

    TalkMessageCtrl** mTalkMessage;
    bool mIsStarPieceLecture;
    s32 mStarPieceHitCount;
    s32 mReplaceStep;
    ButlerStateStarPieceReaction* mButlerState;
    bool mIsAstroDome;
    bool mIsPointed;
};
