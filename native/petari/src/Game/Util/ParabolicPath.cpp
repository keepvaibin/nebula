#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

ParabolicPath::ParabolicPath() : mPosition(0, 0, 0), mAxisY(0, 1, 0), mAxisZ(0, 0, 1) {
    mQuadCoef = 0.0f;
    mLinearCoef = 0.0f;
    mDistance = 0.0f;
}

// Up is from the end toward rPeak; the peak's height is rPeak's along it.
void ParabolicPath::initFromMaxHeight(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rPeak) {
    f32 length;
    Vec3f up;

    MR::separateScalarAndDirection(&length, &up, rPeak - rEnd);

    initFromUpVector(rStart, rEnd, up, up.dot(rPeak - rStart));
}

// maxHeight is above rStart, along rUp.
void ParabolicPath::initFromUpVector(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rUp, f32 maxHeight) {
    mAxisY.set(rUp);

    f32 rise = mAxisY.dot(rEnd - rStart);
    mAxisZ = rEnd - rStart - (mAxisY * rise);

    MR::separateScalarAndDirection(&mDistance, &mAxisZ, mAxisZ);
    MR::calcParabolicFunctionParam(&mQuadCoef, &mLinearCoef, maxHeight, rise);

    mPosition.set(rStart);
}

// The peak is height above the higher end.
void ParabolicPath::initFromUpVectorAddHeight(const Vec3f& rStart, const Vec3f& rEnd, const Vec3f& rUp, f32 height) {
    f32 rise = rUp.dot(rEnd - rStart);

    if (rise < 0.0f) {
        rise = 0.0f;
    }

    initFromUpVector(rStart, rEnd, rUp, height + rise);
}

void ParabolicPath::calcPosition(Vec3f* pOutPosition, f32 param) const {
    f32 forward = mDistance * param;
    f32 up = (param * (mLinearCoef + (mQuadCoef * param)));

    pOutPosition->set(mPosition + mAxisY * up + mAxisZ * (forward));
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

    // Chords: each segment runs mDistance * step across and the parabola's height change up.
    f32 runSq = (mDistance * parameterLengthSegment) * (mDistance * parameterLengthSegment);
    f32 prevHeight = startParam * (mQuadCoef * startParam + mLinearCoef);

    for (int i = 0; i < numSegments; i++) {
        f32 endSegmentParam = startParam + parameterLengthSegment * (i + 1);
        f32 height = endSegmentParam * (mQuadCoef * endSegmentParam + mLinearCoef);

        length += NebulaSqrt((height - prevHeight) * (height - prevHeight) + runSq);
        prevHeight = height;
    }

    return length;
};

f32 ParabolicPath::getTotalLength(s32 numSegments) const {
    return getLength(0.0f, 1.0f, numSegments);
}

f32 ParabolicPath::calcPathSpeedFromAverageSpeed(f32 speed) const {
    return speed / getLength(0.0f, 1.0f, 10);
}
