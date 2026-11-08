#pragma once

#include "Game/Map/RailPart.hpp"
#include <nebula_geometry.hpp>

class Table;
class TableIter;

class BezierRailPart {
public:
    BezierRailPart() {
    }

    void set(const Vec3f& rPoint1, const Vec3f& rPoint1Ctrl, const Vec3f& rPoint2Ctrl, const Vec3f& rPoint2);
    void calcPos(Vec3f* pOut, f32 t) const;
    void calcVelocity(Vec3f* pOut, f32 t) const;

    f32 getLength(f32 t1, f32 t2, int segments) const;
    f32 getParam(f32 targetLength) const;

    f32 getNearestParam(const Vec3f& rPos, f32 step) const;

    inline f32 getVelocityLength(f32 t) const {
        Vec3f v;
        calcVelocity(&v, t);
        return v.length();
    }

    // Power-basis coefficients of the cubic (see set)
    Vec3f mStart;
    Vec3f mCtrlDegree1;
    Vec3f mCtrlDegree2;
    Vec3f mCtrlDegree3;
    f32 mLength;
};

class BezierRail {
public:
    BezierRail(const TableIter& rIter, const Table* pInfo);

    f32 normalizePos(f32 t, int direction) const;
    f32 getPartLength(int idx) const;
    void calcPos(Vec3f* pOut, f32 t) const;
    void calcDirection(Vec3f* pOut, f32 t) const;
    void calcPosDir(Vec3f* pPos, Vec3f* pDir, f32 t) const;
    f32 getNearestRailPosCoord(const Vec3f& rPos) const;
    f32 getRailPosCoord(int idx) const;
    void calcCurrentRailCtrlPointIter(TableIter* pIter, f32 t, bool forward) const;
    void calcRailCtrlPointIter(TableIter* pIter, int idx) const;
    void getIncludedSection(const RailPart** pRailPart, f32* pPos, f32 t, int direction) const;
    int getCurrentCtrlPointIndex(f32 t, bool forward) const;

    f32 getTotalLength() const;

    inline RailPart* getRailPart(s32 idx) const {
        return &mRailParts[idx];
    }

    bool mIsClosed;
    s32 mPointNum;
    s32 mNumRailParts;
    RailPart* mRailParts;  // mNumRailParts parts
    f32* mPointCoords;     // coordinate at the end of each part
    TableIter* mIter;
    const Table* mInfo;
};
