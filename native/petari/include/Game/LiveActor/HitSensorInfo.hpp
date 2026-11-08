#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitSensor;

// Places one sensor each frame: at a matrix, a position or the host, moved by the offset in that frame; or by the host.
class HitSensorInfo {
public:
    HitSensorInfo(const char* pName, HitSensor* pSensor, const Vec3f* pFollowPos, MtxPtr pFollowMtx, const Vec3f& rOffset, bool isHostCallback);

    void update();
    void doObjCol();

    void setOffset(const Vec3f& rOffset) {
        mOffset = rOffset;
    }

    const char* mName;
    s32 mHashCode;
    HitSensor* mSensor;
    Vec3f mOffset;
    const Vec3f* mFollowPos;  // null: the host's position
    MtxPtr mFollowMtx;        // set: wins over mFollowPos
    bool mIsHostCallback;     // the host's updateHitSensor places it
};
