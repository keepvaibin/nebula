#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/LiveActor/RailRider.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcPosition_f1, "calcPosition.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcClippingInfo_f1, "calcClippingInfo.f1", 0.5f);

namespace {
    NEBULA_TUNABLE(f32, cInitDirectionStep, 0.01f);
    NEBULA_TUNABLE(s32, cParabolicLengthSamples, 32);
};  // namespace

SpinDriverShootPath::SpinDriverShootPath() : mRailRider(nullptr), mPath(nullptr), mStartPosition(0.0f, 0.0f, 0.0f), mUsesParabolic(false) {
}

void SpinDriverShootPath::init(const TableIter& rIter) {
    mRailRider = new RailRider(rIter);
    mUsesParabolic = false;
}

// A rail of two points or fewer is flown as an arc through its end, peaking at its start point's height.
void SpinDriverShootPath::initUsingParabolic(const TableIter& rIter, const Vec3f& rStartPos) {
    mRailRider = new RailRider(rIter);
    mUsesParabolic = mRailRider->getPointNum() <= 2;

    if (mUsesParabolic) {
        mPath = new ParabolicPath();
    }

    setStartPosition(rStartPos);
}

// On a rail, mStartPosition is the offset from the rail's start to `rStartPos`, faded out over the first half.
void SpinDriverShootPath::setStartPosition(const Vec3f& rStartPos) {
    if (mUsesParabolic) {
        Vec3f railStart(mRailRider->mStartPos);
        Vec3f railEnd(mRailRider->mEndPos);
        mPath->initFromMaxHeight(rStartPos, railEnd, railStart);
    }

    Vec3f pathStart;
    calcPosition(&pathStart, 0.0f);
    mStartPosition = rStartPos - pathStart;
}

// `rate` runs 0 to 1 along the path.
void SpinDriverShootPath::calcPosition(Vec3f* pOutPosition, f32 rate) const {
    if (mUsesParabolic) {
        mPath->calcPosition(pOutPosition, rate);
        return;
    }

    mRailRider->calcPosAtCoord(pOutPosition, rate * mRailRider->getTotalLength());
    pOutPosition->add(mStartPosition * MR::getEaseOutValue(MR::normalize(rate, 0.0f, cNum_calcPosition_f1), 1.0f, 0.0f, 1.0f));
}

// The chord from `rate` over `step`, kept inside the path.
void SpinDriverShootPath::calcDirection(Vec3f* pOutDirection, f32 rate, f32 step) const {
    f32 fromRate;
    f32 toRate;

    if (rate < step) {
        toRate = step;
        fromRate = 0.0f;
    } else {
        toRate = 1.0f;
        fromRate = (1.0f - step);

        if (rate <= fromRate) {
            fromRate = rate;
            toRate = (rate + step);
        }
    }

    Vec3f from;
    calcPosition(&from, fromRate);
    Vec3f to;
    calcPosition(&to, toRate);
    Vec3f chord = to;
    chord.sub(chord, from);
    pOutDirection->set(chord);
    MR::normalizeOrZero(pOutDirection);
}

// The flier's launch frame: up along the path's first chord (it flies head first), front toward the path's end (or
// any perpendicular) turned `rollDegree` about the up, side their cross product. Any output may be null.
void SpinDriverShootPath::calcInitPose(Vec3f* pUp, Vec3f* pFront, Vec3f* pSide, f32 rollDegree) const {
    Vec3f pathStart;
    calcPosition(&pathStart, 0.0f);

    Vec3f pathEnd;
    calcPosition(&pathEnd, 1.0f);

    Vec3f chordEnd;
    calcPosition(&chordEnd, cInitDirectionStep);
    Vec3f up(chordEnd);
    up.sub(up, pathStart);
    MR::normalizeOrZero(&up);

    Vec3f front;
    front.set(pathEnd - pathStart);
    front.orthogonalize(up);
    MR::normalizeOrZero(&front);

    if (MR::isNearZero(front)) {
        MR::makeAxisVerticalZX(&front, up);
    }

    MR::rotateVecDegree(&front, up, rollDegree);

    Vec3f side;
    side.cross(front, up);
    MR::normalize(&side);

    if (pUp != nullptr) {
        pUp->set(up);
    }

    if (pFront != nullptr) {
        pFront->set(front);
    }

    if (pSide != nullptr) {
        pSide->set(side);
    }
}

f32 SpinDriverShootPath::getTotalLength() const {
    if (mUsesParabolic) {
        return mPath->getTotalLength(cParabolicLengthSamples);
    }

    return mRailRider->getTotalLength();
}

// The box around samples every `sampleInterval` along the path, grown by `margin`: its center and half its diagonal.
void SpinDriverShootPath::calcClippingInfo(Vec3f* pCenter, f32* pRadius, f32 sampleInterval, f32 margin) {
    s32 sampleCount = getTotalLength() / sampleInterval;

    Vec3f pathStart;
    calcPosition(&pathStart, 0.0f);

    Box3f box;
    box.set(pathStart, pathStart);

    for (s32 i = 1; i <= sampleCount; i++) {
        Vec3f pos;
        calcPosition(&pos, static_cast< f32 >(i) / sampleCount);
        box.extend(pos);
    }

    Vec3f padding(margin);
    box.i.sub(padding);
    box.f.add(padding);
    box.getCenter(pCenter);

    Vec3f size;
    size.sub(box.f, box.i);

    *pRadius = cNum_calcClippingInfo_f1 * size.length();
}
