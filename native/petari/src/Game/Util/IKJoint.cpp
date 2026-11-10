#include "Game/Util/IKJoint.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcIKRootAngleCosign_f1, "calcIKRootAngleCosign.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.01f);

IKJoint::IKJoint() : mPose(), mRootMtx(), mMiddleMtx(), mRootBoneLength(100.0f), mMiddleBoneLength(100.0f) {
    mPose.identity();
    mMiddleMtx.identity();
    mRootMtx.identity();
}

void IKJoint::setRootBoneLength(f32 boneLength) {
    mRootBoneLength = boneLength;
}

void IKJoint::setMiddleBoneLength(f32 boneLength) {
    mMiddleBoneLength = boneLength;
}

void IKJoint::setFirstPose(const Vec3f& rSide, const Vec3f& rUp) {
    MR::makeMtxSideUp(&mPose, rSide, rUp);
}

s32 IKJoint::checkReachIKTarget(f32 distance, f32 rootLength, f32 middleLength) {
    int result = 0;

    if (distance > (rootLength + middleLength)) {
        result = 2;
    } else {
        if (distance < MR::abs(rootLength - middleLength)) {
            result = 1;
        } else {
            return result;
        }
    }

    return result;
}

f32 IKJoint::calcIKRootAngleCosign(f32 distance, f32 rootLength, f32 middleLength) {
    s32 reach = checkReachIKTarget(distance, rootLength, middleLength);

    if (reach == 1) {
        return 1.0f;
    }

    if (reach == -1) {
        if (rootLength < middleLength) {
            return -1.0f;
        }

        return 1.0f;
    }

    f32 distanceSquared = distance * distance;
    f32 rootLengthSquared = rootLength * rootLength;
    f32 middleLengthSquared = middleLength * middleLength;
    f32 value = middleLengthSquared - distanceSquared - rootLengthSquared;
    value /= -cNum_calcIKRootAngleCosign_f1 * rootLength * distance;
    return MR::clamp(value, -1.0f, 1.0f);
}

void IKJoint::update(MtxPtr pRootMatrix, MtxPtr pMiddleMatrix, MtxPtr pEndMatrix, const Vec3f& rTarget) {
    Pos3f rootMatrix;
    Vec3f rootPosition;
    Vec3f middlePosition;
    Vec3f endPosition;
    MR::extractMtxTrans(pRootMatrix, &rootPosition);
    MR::extractMtxTrans(pMiddleMatrix, &middlePosition);
    MR::extractMtxTrans(pEndMatrix, &endPosition);
    mRootBoneLength = rootPosition.distance(middlePosition);
    mMiddleBoneLength = middlePosition.distance(endPosition);

    Vec3f targetDirection(rTarget - rootPosition);
    Vec3f side(endPosition - rootPosition);
    if (MR::normalizeOrZero(&side)) {
        MR::extractMtxXDir(pRootMatrix, &side);
    }

    Vec3f up(middlePosition - rootPosition);
    if (MR::isSameDirection(side, up, cNum_update_f1)) {
        MR::extractMtxYDir(pRootMatrix, &up);
    }

    MR::makeMtxSideUp(&mPose, side, up);
    rootMatrix.identity();
    updateByLocalRootAndDirection(rootMatrix, rootPosition, targetDirection);
}

void IKJoint::updateByLocalRootAndWorldTarget(const Pos3f& rRootMatrix, const Vec3f& rRootPosition, const Vec3f& rTarget) {
    Pos3f inverseRoot = rRootMatrix;
    inverseRoot.invert(rRootMatrix);
    Vec3f localTarget;
    inverseRoot.mult(rTarget, localTarget);
    updateByLocalRootAndDirection(rRootMatrix, rRootPosition, localTarget - rRootPosition);
}

s32 IKJoint::updateByLocalRootAndDirection(const Pos3f& rRootMatrix, const Vec3f& rRootPosition, const Vec3f& rDirection) {
    Pos3f matrix;
    Vec3f direction(rDirection);
    f32 distance;
    MR::separateScalarAndDirection(&distance, &direction, direction);
    s32 result = updateByDistanceOnly(distance);
    calcToTargetMatrixByFirstPose(&matrix, rRootMatrix, rRootPosition, direction);
    mRootMtx.concat(matrix, mRootMtx);
    mMiddleMtx.concat(matrix, mMiddleMtx);
    return result;
}

s32 IKJoint::updateByUpVector(const Vec3f& rRootPosition, const Vec3f& rTarget, const Vec3f& rUp) {
    f32 distance;
    Pos3f matrix;
    Vec3f direction = rTarget - rRootPosition;
    MR::separateScalarAndDirection(&distance, &direction, direction);
    s32 result = updateByDistanceOnly(distance);
    MR::makeMtxSideUpPos(&matrix, direction, rUp, rRootPosition);
    mRootMtx.concat(matrix, mRootMtx);
    mMiddleMtx.concat(matrix, mMiddleMtx);
    return result;
}

s32 IKJoint::updateByDistanceOnly(f32 distance) {
    s32 result = checkReachIKTarget(distance, mRootBoneLength, mMiddleBoneLength);
    f32 cosine = calcIKRootAngleCosign(distance, mRootBoneLength, mMiddleBoneLength);
    f32 sine = NebulaSqrt(1.0f - cosine * cosine);
    Vec3f rootSide(cosine, sine, 0.0f);
    Vec3f rootUp(-sine, cosine, 0.0f);
    mRootMtx.identity();
    mRootMtx.setXDir(rootSide);
    mRootMtx.setYDir(rootUp);

    Vec3f middlePosition(rootSide * mRootBoneLength);
    Vec3f middleSide(Vec3f(distance, 0.0f, 0.0f) - middlePosition);
    MR::normalizeOrZero(&middleSide);
    Vec3f middleUp(-middleSide.y, middleSide.x, 0.0f);
    mMiddleMtx.identity();
    mMiddleMtx.setXDir(middleSide);
    mMiddleMtx.setYDir(middleUp);
    mMiddleMtx.setTrans(middlePosition);
    return result;
}

void IKJoint::calcToTargetMatrixByFirstPose(Pos3f* pMatrix, const Pos3f& rRootMatrix, const Vec3f& rRootPosition, const Vec3f& rDirection) const {
    Pos3f matrix;
    Vec3f side;
    mPose.getXDir(side);
    matrix.identity();
    matrix.setRotate(side, rDirection);
    matrix.concat(matrix, mPose);
    matrix.setTrans(rRootPosition);
    matrix.concat(rRootMatrix, matrix);
    pMatrix->set(matrix);
}

void IKJoint::getRootJointPosition(Vec3f* pPos) const {
    mRootMtx.getTrans(*pPos);
}

void IKJoint::getEndJointPosition(Vec3f* pPos) const {
    Vec3f xDir;
    mMiddleMtx.getXDir(xDir);
    mMiddleMtx.getTrans(*pPos);
    pPos->add(xDir * mMiddleBoneLength);
}

f32 IKJoint::getMinLimitDistance() const {
    return MR::abs(mRootBoneLength - mMiddleBoneLength);
}

f32 IKJoint::getMaxLimitDistance() const {
    return mRootBoneLength + mMiddleBoneLength;
}
