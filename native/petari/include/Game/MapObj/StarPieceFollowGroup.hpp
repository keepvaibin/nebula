#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class StarPiece;

class StarPieceFollowGroup : public LiveActor {
public:
    StarPieceFollowGroup(const char*);

    virtual void init(const TableIter& rIter) override;

    bool beginFollowPieces();
    void placementPiece(s32);
    void updateAxises(const Vec3f&, const Vec3f&, const Vec3f&, f32);
    void followPieces();
    void allKillPieces();
    void onSwitchA();
    void exeFollowToPlayer();
    void exeFollowEnd();

    StarPiece** mPieces;
    Vec3f* mPieceOffsets;
    s32 mNumPiecesTotal;
    s32 mNumPiecesUse;
    f32 mRadius;
    f32 mAngle;  // the ring's turn, 0.025 a step
    s32 mTimeLimit;
    Vec3f mLocalXAxis;
    Vec3f mLocalZAxis;
    Vec3f mLocalYAxis;
};
