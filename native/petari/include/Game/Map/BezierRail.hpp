#pragma once

#include "Game/Map/RailPart.hpp"
#include <nebula_geometry.hpp>

class Table;
class TableIter;

class BezierRailPart {
public:
    BezierRailPart() {
    }

    void set(const Vec3f&, const Vec3f&, const Vec3f&, const Vec3f&);
    void calcPos(Vec3f*, f32) const;
    void calcVelocity(Vec3f*, f32) const;

    f32 getLength(f32, f32, int) const;
    f32 getParam(f32) const;

    f32 getNearestParam(const Vec3f&, f32) const;

    inline f32 getVelocityLength(f32 t) const {
        Vec3f v;
        calcVelocity(&v, t);
        return v.length();
    }

    // 0th to 3rd degree control vectors for bezier control
    Vec3f mStart;
    Vec3f mCtrlDegree1;  // "velocity"
    Vec3f mCtrlDegree2;  // "acceleration"
    Vec3f mCtrlDegree3;  // "jerk"
    f32 mLength;
};

class BezierRail {
public:
    BezierRail(const TableIter&, const Table*);

    f32 normalizePos(f32, int) const;
    f32 getPartLength(int) const;
    void calcPos(Vec3f*, f32) const;
    void calcDirection(Vec3f*, f32) const;
    void calcPosDir(Vec3f*, Vec3f*, f32) const;
    f32 getNearestRailPosCoord(const Vec3f&) const;
    f32 getRailPosCoord(int) const;
    void calcCurrentRailCtrlPointIter(TableIter*, f32, bool) const;
    void calcRailCtrlPointIter(TableIter*, int) const;
    void getIncludedSection(const RailPart**, f32*, f32, int) const;
    int getCurrentCtrlPointIndex(f32, bool) const;

    f32 getTotalLength() const;

    inline RailPart* getRailPart(s32 idx) const {
        return &mRailParts[idx];
    }

    bool mIsClosed;
    s32 mPointNum;
    s32 mNumRailParts;
    RailPart* mRailParts;  // this is a direct array of RailParts, not a single RailPart
    f32* mPointCoords;
    TableIter* mIter;
    const Table* mInfo;
};
