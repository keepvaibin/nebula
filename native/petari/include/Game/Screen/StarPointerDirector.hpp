#pragma once

#include "Game/Screen/StarPointerGuidance.hpp"
#include <nebula_geometry.hpp>

class StarPointerController;
class StarPointerLayout;

class StarPointerTransformHolder {
public:
    StarPointerTransformHolder();

    void movement();

    inline f32 getFovy() const {
        return mFovy;
    }

    Pos3f mViewMtx;
    Proj3f mProjMtx;
    f32 mFovy;
};

class StarPointerDirector {
    mutable u32 mDrawSlot = 0;

public:
    StarPointerDirector();

    void update();
    void draw();

    void startHandPointer();
    void startHandPointerReactionWithCrossCursor();
    void startFingerPointer();
    void startStarPointer();
    void startStarPointerNozzle();
    void setGameSceneCameraMtx();
    StarPointerController* getStarPointerController(s32 channel) const;
    StarPointerLayout* getStarPointerLayout(s32 channel) const;

    void createLayout();

    void init() {
        mIsUpdateTransHolder = true;
    }

    void invalidate() {
        mIsUpdateTransHolder = false;
    }

    bool isEnableStarPointerShootStarPiece() const {
        return mIsAllowP1StarPieceShot || mIsAllowP2StarPieceShot;
    }

    bool mIsUpdateTransHolder;
    bool mIsAllowP1StarPieceShot;
    bool mIsAllowP2StarPieceShot;
    bool mIsSystemPointerMode;
    StarPointerController* mControllers;
    StarPointerLayout* mStarPointerLayouts;
    StarPointerTransformHolder* mTransHolder;
    StarPointerGuidance* mGuidance;
    Vec3f mNozzleAimPos;
};

namespace StarPointerFunction {
    bool forceOutsideScreenEdge(Vec2f*, const Vec2f&, const Vec2f&, f32, f32);
    bool isInScreen(const Vec2f&, f32, f32);
    bool forceInsideScreenEdge(Vec2f*);
    StarPointerDirector* getStarPointerDirector();
    s32 getNumStarPointer();
    const Vec2f& getNextPastPosition(s32, s32);
    s32 getNextPastPointNum(s32);
    bool canShoot(s32);
};  // namespace StarPointerFunction
