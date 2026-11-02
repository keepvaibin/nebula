#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class StarPiece;

class StarPieceMother : public LiveActor {
public:
    StarPieceMother(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void control();

    void exeMoveOnRail();
    void placementPieceOnRail(f32);
    void onSwitchA();
    void offSwitchA();
    void attackSensor(HitSensor*, HitSensor*);
    void emitStarPieces();
    void exeWait();

private:
    StarPiece** mStarPieceArray;
    f32* _90;
    s32 _94;
    u32 _98;
    u32 _9C;
    f32 _A0;
};
