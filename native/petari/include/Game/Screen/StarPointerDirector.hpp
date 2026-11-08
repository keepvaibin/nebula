#pragma once

#include "Game/Screen/StarPointerGuidance.hpp"
#include <nebula_geometry.hpp>

class StarPointerController;
class StarPointerLayout;
class PointerInfo;

class StarPointerPeekZ {
public:
    StarPointerPeekZ();

    // The depth under each pointer, from the picture where the drawing has reached (its readback channel).
    void setDrawSyncToken();

    static const u32 cReadbackChannel = 1;

    PointerInfo** mInfos;
    f32 mProjectionParameters[7];
    f32 mViewportParameters[6];
    u32 mAsker;  // the asker its probes carry
};

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
    f32 mFocalLength;
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
    bool mIsOSPointerMode;
    StarPointerController* mControllers;
    StarPointerLayout* mStarPointerLayouts;
    StarPointerTransformHolder* mTransHolder;
    StarPointerPeekZ* mPeekZ;
    StarPointerGuidance* mGuidance;
    Vec3f mNozzleAimPos;
};

namespace StarPointerFunction {
    bool forceOutsideScreenEdge(Vec2f*, const Vec2f&, const Vec2f&, f32, f32);
    bool isInScreen(const Vec2f&, f32, f32);
    bool forceInsideScreenEdge(Vec2f*);
    StarPointerDirector* getStarPointerDirector();
    s32 getNumStarPointer();
    s32 getPastPointNum(s32);
    const Vec2f& getPastPosition(s32, s32);
    const Vec2f& getNextPastPosition(s32, s32);
    s32 getNextPastPointNum(s32);
    bool canShoot(s32);
};  // namespace StarPointerFunction
