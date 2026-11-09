#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class StarPiece;

class StarPieceGroup : public LiveActor {
public:
    StarPieceGroup(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;

    bool isExistAnyStarPiece();
    void forceReplaceStarPieceAll();
    void forceKillStarPieceAll(bool);
    void placementAllPiece();
    void placementPieceOnCircle();
    void placementPieceOnRail();
    void placementPieceOnRailPoint();
    void onSwitchA();
    void offSwitchA();
    void onSwitchB();
    void exeFlow();
    void exeGroup();

    StarPiece** mPieces;
    f32* mRailCoords;
    bool mHasAppeared;
    bool mIsAppearEffect;
    s32 mNumPieces;
    f32 mCircleRadius;
    bool mIsRail;
    bool mPlaceAtPathPoints;
    s32 mLifeStep;
    f32 mRailSpeed;
    Vec3f mRailClippingCenter;
};
