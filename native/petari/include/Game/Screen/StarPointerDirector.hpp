#pragma once

#include "Game/Screen/StarPointerGuidance.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class StarPointerController;
class StarPointerLayout;
class DpdInfo;

class StarPointerPeekZ {
public:
    StarPointerPeekZ();

    // The depth under each pointer, from the picture where the drawing has reached (its readback channel).
    void setDrawSyncToken();

    static const u32 cReadbackChannel = 1;

    DpdInfo** mInfos;
    f32 mProjectionParameters[7];
    f32 mViewportParameters[6];
};

class StarPointerTransformHolder {
public:
    StarPointerTransformHolder();

    void movement();

    inline f32 getFovy() const {
        return mFovy;
    }

    TPos3f mViewMtx;
    TProj3f mProjMtx;
    f32 mFovy;
    f32 mFocalLength;
};

class StarPointerDirector {
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
    s32 _18;
    TVec3f mNozzleAimPos;
};

namespace StarPointerFunction {
    bool forceOutsideScreenEdge(TVec2f*, const TVec2f&, const TVec2f&, f32, f32);
    bool isInScreen(const TVec2f&, f32, f32);
    bool forceInsideScreenEdge(TVec2f*);
    StarPointerDirector* getStarPointerDirector();
    s32 getNumStarPointer();
    s32 getPastPointNum(s32);
    const TVec2f& getPastPosition(s32, s32);
    const TVec2f& getNextPastPosition(s32, s32);
    s32 getNextPastPointNum(s32);
    bool canShoot(s32);
};  // namespace StarPointerFunction
