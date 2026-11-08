#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

ParabolicPath::ParabolicPath() : mPosition(0, 0, 0), mAxisY(0, 1, 0), mAxisZ(0, 0, 1) {
    mQuadCoef = 0.0f;
    mLinearCoef = 0.0f;
    mDistance = 0.0f;
}

void ParabolicPath::initFromMaxHeight(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rUp) {
    f32 scalar;
    Vec3f direction;

    MR::separateScalarAndDirection(&scalar, &direction, rUp - rEnd);

    initFromUpVector(rStart, rEnd, direction, direction.dot(rUp - rStart));
}

void ParabolicPath::initFromUpVector(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rUp, f32 f1) {
    mAxisY.set(rUp);

    f32 dot = mAxisY.dot(rEnd - rStart);
    mAxisZ = rEnd - rStart - (mAxisY * dot);

    MR::separateScalarAndDirection(&mDistance, &mAxisZ, mAxisZ);
    MR::calcParabolicFunctionParam(&mQuadCoef, &mLinearCoef, f1, dot);

    mPosition.set(rStart);
}

void ParabolicPath::initFromUpVectorAddHeight(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rUp, f32 a4) {
    f32 dot = rUp.dot(rEnd - rStart);

    if (dot < 0.0f) {
        dot = 0.0f;
    }

    initFromUpVector(rStart, rEnd, rUp, a4 + dot);
}

void ParabolicPath::calcPosition(Vec3f* pOutPosition, f32 a2) const {
    f32 thing = mDistance * a2;
    f32 v5 = (a2 * (mLinearCoef + (mQuadCoef * a2)));

    pOutPosition->set(mPosition + mAxisY * v5 + mAxisZ * (thing));
}

void ParabolicPath::calcDirection(Vec3f* pOutDirection, f32 param, f32 step) const {
    f32 startParam;
    f32 endParam;

    if (param < step) {
        endParam = step;
        startParam = 0.0f;
    } else if (param > 1.0f - step) {
        startParam = 1.0f - step;
        endParam = 1.0f;
    } else {
        startParam = param;
        endParam = param + step;
    }

    Vec3f startPosition;
    calcPosition(&startPosition, startParam);
    Vec3f endPosition;
    calcPosition(&endPosition, endParam);
    pOutDirection->set(endPosition - startPosition);
    MR::normalizeOrZero(pOutDirection);
}

f32 ParabolicPath::getLength(f32 startParam, f32 endParam, s32 numSegments) const {
    if (numSegments <= 0) {
        numSegments = 1;
    }

    f32 parameterLengthSegment = (endParam - startParam) / numSegments;
    f32 length = 0.0f;

    f32 f6 = (mDistance * parameterLengthSegment) * (mDistance * parameterLengthSegment);
    f32 f2 = startParam * (mQuadCoef * startParam + mLinearCoef);

    for (int i = 0; i < numSegments; i++) {
        f32 endSegmentParam = startParam + parameterLengthSegment * (i + 1);
        f32 f4 = endSegmentParam * (mQuadCoef * endSegmentParam + mLinearCoef);

        length += NebulaSqrt((f4 - f2) * (f4 - f2) + f6);
        f2 = f4;
    }

    return length;
};

f32 ParabolicPath::getTotalLength(s32 a1) const {
    return getLength(0.0f, 1.0f, a1);
}

f32 ParabolicPath::calcPathSpeedFromAverageSpeed(f32 speed) const {
    return speed / getLength(0.0f, 1.0f, 10);
}
