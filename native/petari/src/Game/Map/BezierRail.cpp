#include "Game/Map/BezierRail.hpp"
#include "Game/Map/RailPart.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>

namespace {
    const s32 cLengthSegments = 10;        // Simpson pairs per length integral
    const f32 cParamTolerance = 0.01f;     // getParam stops within this length
    const s32 cParamIterations = 5;
    const f32 cDirectionStep = 0.01f;      // param step for a zero-velocity direction
    const f32 cNearestSampleStep = 100.0f; // length between getNearestParam samples

    int getNextIndex(int index, int pointNum) {
        return pointNum > 0 ? (index + 1) % pointNum : 0;
    }

    void calcRailDirection(Vec3f* pOut, const RailPart* pRailPart, f32 t) {
        pRailPart->calcVelocity(pOut, t);

        if (MR::isNearZero(*pOut)) {
            f32 tStart = t;
            f32 tEnd;
            if (t < 0.5f) {
                tEnd = cDirectionStep + t;
            } else {
                tEnd = t;
                tStart = t - cDirectionStep;
            }

            Vec3f start;
            pRailPart->calcPos(&start, tStart);
            pRailPart->calcPos(pOut, tEnd);
            pOut->sub(start);
        }

        MR::normalize(pOut);
    }
};  // namespace

// Power basis: pos(t) = mStart + 3t * mCtrlDegree1 + 3t^2 * mCtrlDegree2 + t^3 * mCtrlDegree3.
void BezierRailPart::set(const Vec3f& rPoint1, const Vec3f& rPoint1Ctrl, const Vec3f& rPoint2Ctrl, const Vec3f& rPoint2) {
    Vec3f d1(rPoint1Ctrl);
    d1.sub(rPoint1);
    Vec3f d2(rPoint2Ctrl);
    d2.sub(rPoint1Ctrl);
    Vec3f d3(rPoint2);
    d3.sub(rPoint2Ctrl);

    mStart = rPoint1;
    mCtrlDegree1 = d1;
    mCtrlDegree2.sub(d2, d1);
    mCtrlDegree3.sub(d3, d2);
    mCtrlDegree3.sub(mCtrlDegree2);

    mLength = getLength(0.0f, 1.0f, cLengthSegments);
}

void BezierRailPart::calcPos(Vec3f* pOut, f32 t) const {
    f32 w1 = 3.0f * t;
    f32 w2 = 3.0f * t * t;
    f32 w3 = t * t * t;

    Vec3f term1(mCtrlDegree1);
    Vec3f term2(mCtrlDegree2);
    Vec3f term3(mCtrlDegree3);

    term1.scale(w1);
    term2.scale(w2);
    term3.scale(w3);

    pOut->set(mStart);
    pOut->add(term1);
    pOut->add(term2);
    pOut->add(term3);
}

void BezierRailPart::calcVelocity(Vec3f* pOut, f32 t) const {
    f32 w3 = t * t;
    f32 w2 = 2.0f * t;

    Vec3f term3(mCtrlDegree3);
    Vec3f term2(mCtrlDegree2);
    term3.scale(w3);
    term2.scale(w2);

    pOut->set(mCtrlDegree1);
    pOut->add(term2);
    pOut->add(term3);
    pOut->scale(3.0f);
}

// Simpson's rule over 2 * segments steps with the end samples at half weight, floored to 1/1024.
// Rail coordinates are measured in these lengths, so the weights stay as they are.
f32 BezierRailPart::getLength(f32 t1, f32 t2, int segments) const {
    f32 endsHalf = (1.0f / 2.0f) * (getVelocityLength(t1) + getVelocityLength(t2));

    f32 tDiff = (t2 - t1);
    f32 stepRate = (1.0f / (segments * 2.0));
    f32 step = tDiff * stepRate;

    f32 oddSum = 0.0f;
    f32 evenSum = 0.0f;

    for (s32 seg = 1; seg <= segments; seg++) {
        s32 oddIndex = ((seg * 2) - 1);
        s32 evenIndex = (seg * 2);

        f32 tOdd = t1 + step * oddIndex;
        f32 tEven = t1 + step * evenIndex;

        oddSum += getVelocityLength(tOdd);

        if (seg != segments) {
            evenSum += getVelocityLength(tEven);
        }
    }

    f32 len = floor(1024.0f * static_cast< f32 >((1.0 / 3.0) * step * ((oddSum * 4.0f) + (endsHalf + (evenSum * 2.0f)))));
    f32 rescale = 1.0f / 1024.0f;
    len *= rescale;
    return len;
}

// Newton steps from the uniform guess; param stays clamped to 0..1.
f32 BezierRailPart::getParam(f32 targetLength) const {
    // A part of no length (two points on one place) has only its start.
    if (!(mLength > 0.0f)) {
        return 0.0f;
    }

    f32 param = targetLength / mLength;
    f32 length = getLength(0.0f, param, cLengthSegments);

    if (MR::abs(targetLength - length) <= cParamTolerance) {
        return param;
    }

    for (s32 count = 0; count < cParamIterations; count++) {
        f32 speed = getVelocityLength(param);
        param = FloatUtil::clamp(param + (targetLength - length) / speed, 0.0f, 1.0f);
        length = getLength(0.0f, param, cLengthSegments);

        if (MR::abs(targetLength - length) <= cParamTolerance) {
            return param;
        }
    }

    return param;
}

f32 BezierRailPart::getNearestParam(const Vec3f& rPos, f32 step) const {
    f32 nearestParam = -1.0f;
    f32 nearestDistSq = FLOAT_MAX;

    for (f32 t = 0.0f; t <= 1.0f; t += step) {
        Vec3f pos;
        calcPos(&pos, t);
        f32 distSq = pos.squared(rPos);
        if (distSq < nearestDistSq) {
            nearestParam = t;
            nearestDistSq = distSq;
        }
    }

    return nearestParam;
}

f32 BezierRail::normalizePos(f32 t, int direction) const {
    if (mIsClosed) {
        f32 pos = MR::mod(t, getTotalLength());
        if (direction < 0 && MR::isNearZero(pos)) {
            pos = getTotalLength();
        }
        if (pos < 0.0f) {
            pos += getTotalLength();
        }
        return pos;
    } else {
        return FloatUtil::clamp(t, 0.0f, getTotalLength());
    }
}

f32 BezierRail::getTotalLength() const {
    return mPointCoords[mNumRailParts - 1];
}

f32 BezierRail::getPartLength(int idx) const {
    return mRailParts[idx].getTotalLength();
}

void BezierRail::calcPos(Vec3f* pOut, f32 t) const {
    const RailPart* part;
    f32 partPos;

    getIncludedSection(&part, &partPos, t, 1);
    part->calcPos(pOut, part->getParam(partPos));
}

void BezierRail::calcDirection(Vec3f* pOut, f32 t) const {
    const RailPart* part;
    f32 partPos;

    getIncludedSection(&part, &partPos, t, 1);
    calcRailDirection(pOut, part, part->getParam(partPos));
}

void BezierRail::calcPosDir(Vec3f* pPos, Vec3f* pDir, f32 t) const {
    const RailPart* part;
    f32 partPos;

    getIncludedSection(&part, &partPos, t, 1);
    f32 param = part->getParam(partPos);
    part->calcPos(pPos, param);
    calcRailDirection(pDir, part, param);
}

f32 BezierRail::getNearestRailPosCoord(const Vec3f& rPos) const {
    RailPart* part = mRailParts;
    int idx = 0;

    f32 nearestParam = part->getNearestParam(rPos, cNearestSampleStep / part->getTotalLength());

    Vec3f pos;
    part->calcPos(&pos, nearestParam);
    pos.sub(rPos);

    f32 nearestDistSq = pos.squared();

    for (int i = 1; i < mNumRailParts; i++) {
        RailPart* curPart = getRailPart(i);

        f32 curNearParam = curPart->getNearestParam(rPos, cNearestSampleStep / curPart->getTotalLength());
        curPart->calcPos(&pos, curNearParam);

        pos.sub(rPos);
        f32 curDistSq = pos.squared();

        if (curDistSq < nearestDistSq) {
            nearestDistSq = curDistSq;
            idx = i;
            nearestParam = curNearParam;
        }
    }

    f32 partStart = idx == 0 ? 0.0f : mPointCoords[idx - 1];
    return (partStart + getRailPart(idx)->getLength(0.0f, nearestParam, cLengthSegments));
}

f32 BezierRail::getRailPosCoord(int idx) const {
    if (idx == 0) {
        return 0.0f;
    }

    if (mIsClosed || idx != mPointNum - 1) {
        return mPointCoords[idx - 1];
    }

    return getTotalLength();
}

void BezierRail::calcCurrentRailCtrlPointIter(TableIter* pIter, f32 t, bool forward) const {
    calcRailCtrlPointIter(pIter, getCurrentCtrlPointIndex(t, forward));
}

void BezierRail::calcRailCtrlPointIter(TableIter* pIter, int idx) const {
    pIter->mIndex = idx;
    pIter->mInfo = mInfo;
}

// Forward, a point coordinate belongs to the part after it; backward, to the part before it.
void BezierRail::getIncludedSection(const RailPart** pRailPart, f32* pPos, f32 t, int direction) const {
    f32 pos = normalizePos(t, direction);
    s32 idx = -1;

    if (direction > 0) {
        for (s32 i = 0; i < mNumRailParts; i++) {
            if (!(mPointCoords[i] <= pos) || i == mNumRailParts - 1) {
                idx = i;
                break;
            }
        }
    } else {
        for (s32 i = mNumRailParts - 1; i >= 0; i--) {
            if (i == 0 || (mPointCoords[i - 1] < pos && pos <= mPointCoords[i])) {
                idx = i;
                break;
            }
        }
    }

    f32 partPos = idx == 0 ? pos : pos - mPointCoords[idx - 1];
    RailPart* part = getRailPart(idx);
    *pRailPart = part;
    *pPos = FloatUtil::clamp(partPos, 0.0f, part->getTotalLength());
}

int BezierRail::getCurrentCtrlPointIndex(f32 t, bool forward) const {
    f32 pos = normalizePos(t, forward ? 1 : -1);

    if (MR::isNearZero(pos)) {
        return 0;
    }

    if (MR::isNearZero(getTotalLength() - pos)) {
        return !mIsClosed ? mPointNum - 1 : 0;
    }

    if (forward) {
        for (int i = 0; i < mNumRailParts; i++) {
            if (pos < mPointCoords[i]) {
                return i;
            }
        }
    } else {
        for (int i = mNumRailParts - 1; i >= 0; i--) {
            f32 partStart = i > 0 ? mPointCoords[i - 1] : 0.0f;
            if (partStart < pos && pos <= mPointCoords[i]) {
                return getNextIndex(i, mPointNum);
            }
        }
    }

    return 0;
}

BezierRail::BezierRail(const TableIter& rIter, const Table* pInfo) {
    mInfo = pInfo;
    mIter = new TableIter(rIter);

    const char* closedValue = "";
    rIter.mInfo->getValue(rIter.mIndex, "closed", &closedValue);
    mIsClosed = MR::isEqualString(closedValue, "CLOSE");

    mPointNum = pInfo->getNumEntries();

    mNumRailParts = mPointNum - 1;
    if (mIsClosed) {
        mNumRailParts++;
    }

    // A rail of one point (bad placement data) is one part from that point to itself; the length reads need a part.
    if (mNumRailParts < 1) {
        mNumRailParts = 1;
    }

    mRailParts = new RailPart[mNumRailParts];
    mPointCoords = new f32[mNumRailParts];

    // Kahan summation: compensation holds the low bits pointCoord dropped.
    f32 pointCoord = 0.0f;
    f32 compensation = 0.0f;

    for (int i = 0; i < mNumRailParts; i++) {
        TableIter iterPoint1 = TableIter(pInfo, i);
        TableIter iterPoint2 = TableIter(pInfo, getNextIndex(i, mPointNum));

        Vec3f point1;
        Vec3f point2;
        Vec3f point1Ctrl;
        Vec3f point2Ctrl;

        MR::getRailPointPos0(iterPoint1, &point1);
        MR::getRailPointPos2(iterPoint1, &point1Ctrl);
        MR::getRailPointPos1(iterPoint2, &point2Ctrl);
        MR::getRailPointPos0(iterPoint2, &point2);

        mRailParts[i].init(point1, point1Ctrl, point2Ctrl, point2);

        f32 term = compensation + mRailParts[i].getTotalLength();
        f32 prevPointCoord = pointCoord;
        pointCoord += term;
        compensation = term - (pointCoord - prevPointCoord);
        mPointCoords[i] = pointCoord;
    }
}
