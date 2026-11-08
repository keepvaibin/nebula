#pragma once

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/NPC/Tico.hpp"

class TicoEat : public Tico {
public:
    TicoEat(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual void startReactionSound();

    void initStarPiece(s32);
    bool tryEat();
    bool hasEnoughStarPiece() const;
    bool eventFunc(u32);
    void exeReaction();
    void exeEatPre();
    void exeEatNow();
    void exeEatPst();
    void exeEatEnd();

    s32 mStarPieceNeed;
    s32 mStarPieceLeft;
};

class TicoComet : public TicoEat {
public:
    TicoComet(const char*);

    virtual void init(const TableIter&);
    virtual void startReactionSound();

    bool branchFunc(u32);
    bool tryDemo();
    bool eventFunc(u32);
    void exeDemoAnim();
    void exeDemoFade();
    void exeDemoEnd();
};
