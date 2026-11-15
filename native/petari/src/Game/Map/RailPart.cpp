#include "Game/Map/RailPart.hpp"
#include "Game/Map/BezierRail.hpp"
#include <nebula_geometry.hpp>

RailPart::RailPart() : mRailPartLinear(nullptr), mRailPartBezier(nullptr) {
}

void RailPart::init(const Vec3f& rPoint1, const Vec3f& rPoint1Ctrl, const Vec3f& rPoint2Ctrl, const Vec3f& rPoint2) {
    if ((rPoint1.epsilonEquals(rPoint1Ctrl, 0.1f)) && (rPoint2.epsilonEquals(rPoint2Ctrl, 0.1f))) {
        mRailPartLinear = new LinearRailPart();
        mRailPartLinear->set(rPoint1, rPoint2);
    } else {
        initForBezier(rPoint1, rPoint1Ctrl, rPoint2Ctrl, rPoint2);
    }
}

void RailPart::initForBezier(const Vec3f& rPoint1, const Vec3f& rPoint1Ctrl, const Vec3f& rPoint2Ctrl, const Vec3f& rPoint2) {
    mRailPartBezier = new BezierRailPart();
    mRailPartBezier->set(rPoint1, rPoint1Ctrl, rPoint2Ctrl, rPoint2);
}

void RailPart::calcPos(Vec3f* pOut, f32 t) const {
    if (mRailPartLinear) {
        pOut->scaleAdd(t, mRailPartLinear->mCtrlDegree1, mRailPartLinear->mStart);
    } else {
        mRailPartBezier->calcPos(pOut, t);
    }
}

void RailPart::calcVelocity(Vec3f* pOut, f32 t) const {
    if (mRailPartLinear) {
        pOut->set(mRailPartLinear->mCtrlDegree1);
    } else {
        mRailPartBezier->calcVelocity(pOut, t);
    }
}

f32 RailPart::getLength(f32 t1, f32 t2, int k) const {
    if (mRailPartLinear) {
        return (mRailPartLinear->mLength * (t2 - t1));
    }

    return mRailPartBezier->getLength(t1, t2, k);
}

f32 RailPart::getTotalLength() const {
    if (mRailPartLinear) {
        return mRailPartLinear->mLength;
    }

    return mRailPartBezier->mLength;
}

f32 RailPart::getParam(f32 t) const {
    if (mRailPartLinear) {
        // A part of no length (two points on one place) has no parameter.
        if (mRailPartLinear->mLength <= 0.0f) {
            return 0.0f;
        }

        return (t / mRailPartLinear->mLength);
    }

    return mRailPartBezier->getParam(t);
}

f32 RailPart::getNearestParam(const Vec3f& rPos, f32 t) const {
    if (mRailPartLinear) {
        return mRailPartLinear->getNearestParam(rPos, t);
    }

    return mRailPartBezier->getNearestParam(rPos, t);
}

void LinearRailPart::set(const Vec3f& rPoint1, const Vec3f& rPoint2) {
    mStart = rPoint1;
    mCtrlDegree1 = rPoint2;
    mCtrlDegree1 -= rPoint1;
    mLength = NebulaVecMagXYZ(&mCtrlDegree1);
}

f32 LinearRailPart::getNearestParam(const Vec3f& rPos, f32 /*t*/) const {
    Vec3f v = rPos;
    v -= mStart;

    // A part of no length (two points on one place) is nearest at its start.
    const f32 sq = mCtrlDegree1.squared();
    if (!(sq > 0.0f)) {
        return 0.0f;
    }

    f32 proj = v.dot(mCtrlDegree1) / sq;

    if (proj < 0.0f) {
        return 0.0f;
    }

    if (proj > 1.0f) {
        return 1.0f;
    }

    return proj;
}
