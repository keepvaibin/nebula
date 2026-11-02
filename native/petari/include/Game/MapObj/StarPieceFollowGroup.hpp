#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class StarPiece;

class StarPieceFollowGroup : public LiveActor {
public:
    StarPieceFollowGroup(const char*);

    virtual void init(const JMapInfoIter& rIter);

    bool beginFollowPieces();
    void placementPiece(s32);
    void updateAxises(const TVec3f&, const TVec3f&, const TVec3f&, f32);
    void followPieces();
    void allKillPieces();
    void onSwitchA();
    void exeFollowToPlayer();
    void exeFollowEnd();

    StarPiece** mPieces;
    TVec3f* _90;
    s32 mNumPiecesTotal;
    s32 mNumPiecesUse;
    f32 mRadius;
    // appears to go unused
    u32 _A0;
    f32 mAngle;
    s32 mTimeLimit;
    TVec3f mLocalXAxis;
    TVec3f mLocalZAxis;
    TVec3f mLocalYAxis;
};
