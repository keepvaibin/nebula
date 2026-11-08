#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class OceanRing;
class OceanRingPipeInside;
class OceanRingPipeOutside;

class OceanRingPipe : public LiveActor {
public:
    OceanRingPipe(const OceanRing*, f32, f32);

    virtual void init(const TableIter&);
    virtual void movement();

    void initPoints();
    f32 getAngle() {
        f32 f = 180.0f / (mNumSides - 1);
        return PI_180 * f;
    }
    inline int calcPointIndex(int i, int j) const {
        return (i * mNumSides) + j;
    }

    const OceanRing* mOceanRing;
    bool mIsCameraInside;
    u32 mNumPoints;
    s32 mNumSegments;
    s32 mNumSides;
    Vec3f* mPositions;
    // Twice unit length: the outside's highlight lookup (texture matrix 1) takes the normal at that scale.
    Vec3f* mNormals;
    f32 mWidth;
    f32 mWidthArg;
    OceanRingPipeInside* mPipeInside;
    OceanRingPipeOutside* mPipeOutside;
};
