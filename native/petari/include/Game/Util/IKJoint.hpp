#pragma once

#include <nebula_geometry.hpp>

class IKJoint {
public:
    IKJoint();

    void setRootBoneLength(f32);
    void setMiddleBoneLength(f32);
    void setFirstPose(const Vec3f&, const Vec3f&);
    static s32 checkReachIKTarget(f32, f32, f32);
    static f32 calcIKRootAngleCosign(f32, f32, f32);

    void update(MtxPtr, MtxPtr, MtxPtr, const Vec3f&);

    void getRootJointPosition(Vec3f*) const;
    void getEndJointPosition(Vec3f*) const;

    f32 getMaxLimitDistance() const;
    f32 getMinLimitDistance() const;

    void updateByLocalRootAndWorldTarget(const Pos3f&, const Vec3f&, const Vec3f&);

    s32 updateByLocalRootAndDirection(const Pos3f&, const Vec3f&, const Vec3f&);

    s32 updateByUpVector(const Vec3f&, const Vec3f&, const Vec3f&);

    s32 updateByDistanceOnly(f32);

    void calcToTargetMatrixByFirstPose(Pos3f* pMatrix, const Pos3f& rRootMatrix, const Vec3f& rRootPosition, const Vec3f& rDirection) const;

    Pos3f mPose;
    Pos3f mRootMtx;
    Pos3f mMiddleMtx;
    f32 mRootBoneLength;
    f32 mMiddleBoneLength;
};
