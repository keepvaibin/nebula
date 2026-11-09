#include "Game/MapObj/ClipAreaShape.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MtxUtil.hpp"

ClipAreaShape::ClipAreaShape(const char* pName) : mModelData() {
    mModelData = MR::getModelData(pName);
}

bool ClipAreaShape::isInArea(const Vec3f& rPosition, f32 /*radius*/, const Pos3f& rMtx, const Vec3f& rScale) const {
    if (MR::isNearZero(rScale.x) || (MR::isNearZero(rScale.y) || MR::isNearZero(rScale.z))) {
        return false;
    }

    Pos3f mtx;
    mtx.identity();
    mtx.invert(rMtx);
    Vec3f srcVec;
    mtx.mult(rPosition, srcVec);

    srcVec.x = (srcVec.x / rScale.x);
    srcVec.y = (srcVec.y / rScale.y);
    srcVec.z = (srcVec.z / rScale.z);
    return isInArea(srcVec);
}

void ClipAreaShape::calcVolumeMatrix(Pos3f* pVolMtx, const Pos3f& rSrcMtx, const Vec3f& rScale) const {
    pVolMtx->set(rSrcMtx);
    MR::preScaleMtx(pVolMtx->toMtxPtr(), rScale);
}

void ClipAreaShape::drawVolumeShape(TypedDraw& rDraw, const Pos3f& rMtx, const Vec3f& rPos) const {
    Pos3f volMtx;
    volMtx.identity();
    calcVolumeMatrix(&volMtx, rMtx, rPos);
    rDraw.setWorld(volMtx);
    rDraw.drawModelFile(mModelData);
}

bool ClipAreaShapeSphere::isInArea(const Vec3f& rVec) const {
    return rVec.squared() < mRadius * mRadius;
}

void ClipAreaShapeSphere::calcVolumeMatrix(Pos3f* pPos, const Pos3f& rPos, const Vec3f& rVec) const {
    pPos->set(rPos);

    // The volume model is 100 units across.
    Vec3f volumeScale(rVec.scaleInline(mRadius));
    volumeScale.scale(0.01f);
    MR::preScaleMtx(pPos->toMtxPtr(), volumeScale);
}

bool ClipAreaShapeBox::isInArea(const Vec3f& rVec) const {
    switch (mOriginType) {
    case 0:
        return (MR::isInRange(rVec.x, -mRadius, mRadius) && MR::isInRange(rVec.y, -mRadius, mRadius) && MR::isInRange(rVec.z, -mRadius, mRadius));
    case 1:
        return (MR::isInRange(rVec.x, -mRadius, mRadius) && MR::isInRange(rVec.y, 0.0f, 2.0f * mRadius) && MR::isInRange(rVec.z, -mRadius, mRadius));
    }

    return false;
}

void ClipAreaShapeBox::calcVolumeMatrix(Pos3f* pPos, const Pos3f& rPos, const Vec3f& rVec) const {
    pPos->set(rPos);

    if (mOriginType == 1) {
        MR::addTransMtxLocalY(pPos->toMtxPtr(), mRadius * rVec.y);
    }

    MR::preScaleMtx(pPos->toMtxPtr(), rVec);
}

ClipAreaShapeCone::ClipAreaShapeCone(s32 originType) : ClipAreaShape("ClipVolumeSphere") {
    mRadius = 500.0f;
    mHeight = 1000.0f;
    mOriginType = originType;
}

// rVec is in the cone's space, its tip at y 0 (origin type 1: at mHeight).
bool ClipAreaShapeCone::isInArea(const Vec3f& rVec) const {
    f32 heightRate = (rVec.y / mHeight);

    if (!MR::isInRange(heightRate, 0.0f, 1.0f)) {
        return false;
    }

    if (mOriginType == 1) {
        heightRate = (1.0f - heightRate);
    }

    f32 distanceSq = ((rVec.x * rVec.x) + (rVec.z * rVec.z));
    f32 radiusSq = (heightRate * mRadius) * (heightRate * mRadius);
    return distanceSq < radiusSq;
}

bool ClipAreaShape::isInArea(const Vec3f&) const {
    return false;
}

ClipAreaShapeSphere::ClipAreaShapeSphere() : ClipAreaShape("ClipVolumeSphere"), mRadius(500.0f) {
}

ClipAreaShapeBox::ClipAreaShapeBox(s32 u1) : ClipAreaShape("ClipVolumeBox") {
    mRadius = 500.0f;
    mOriginType = u1;
}
