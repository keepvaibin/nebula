#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class StarPiece;

class StarPieceMother : public LiveActor {
public:
    StarPieceMother(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;

    void exeMoveOnRail();
    void placementPieceOnRail(f32);
    void onSwitchA();
    void offSwitchA();
    void attackSensor(HitSensor*, HitSensor*) override;
    void emitStarPieces();
    void exeWait();

private:
    StarPiece** mStarPieceArray;
    f32* mRailCoords;
    s32 mNumPieces;
    u32 mNextPiece;
    f32 mRailSpeed;
};
