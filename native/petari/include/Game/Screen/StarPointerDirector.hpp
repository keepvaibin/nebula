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

    /* 0x08 */ DpdInfo** mInfos;
    /* 0x0C */ f32 mProjectionParameters[7];
    /* 0x28 */ f32 mViewportParameters[6];
};

class StarPointerTransformHolder {
public:
    StarPointerTransformHolder() NO_INLINE;

    void movement();

    inline f32 getFovy() const {
        return mFovy;
    }

    /* 0x00 */ TPos3f mViewMtx;
    /* 0x30 */ TProj3f mProjMtx;
    /* 0x70 */ f32 mFovy;
    /* 0x74 */ f32 mFocalLength;
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

    /* 0x00 */ bool mIsUpdateTransHolder;
    /* 0x01 */ bool mIsAllowP1StarPieceShot;
    /* 0x02 */ bool mIsAllowP2StarPieceShot;
    /* 0x03 */ bool mIsOSPointerMode;
    /* 0x04 */ StarPointerController* mControllers;
    /* 0x08 */ StarPointerLayout* mStarPointerLayouts;
    /* 0x0C */ StarPointerTransformHolder* mTransHolder;
    /* 0x10 */ StarPointerPeekZ* mPeekZ;
    /* 0x14 */ StarPointerGuidance* mGuidance;
    /* 0x18 */ s32 _18;
    /* 0x1C */ TVec3f mNozzleAimPos;
};

namespace StarPointerFunction {
#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
    bool forceOutsideScreenEdge(TVec2f*, const TVec2f&, const TVec2f&, f32, f32);
    bool isInScreen(const TVec2f&, f32, f32);
#endif
    bool isOnScreenEdge(const TVec2f&, f32, f32);
    bool isOnScreenEdge(s32);
    bool forceInsideScreenEdge(TVec2f*);
    StarPointerDirector* getStarPointerDirector() NO_INLINE;
    s32 getNumStarPointer();
    s32 getPastPointNum(s32);
    const TVec2f& getPastPosition(s32, s32);
    s32 getNextPastPointNum(s32);
    bool canShoot(s32);
};  // namespace StarPointerFunction
