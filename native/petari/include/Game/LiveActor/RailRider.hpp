#pragma once

#include "Game/Map/BezierRail.hpp"

class RailRider {
public:
    RailRider(const TableIter&);
    RailRider(s32 railId, s32 zoneId);

    void move();
    void moveToNearestPos(const Vec3f&);
    void moveToNearestPoint(const Vec3f&);
    void moveToNextPoint();
    void reverse();
    void calcPosAtCoord(Vec3f*, f32 coord) const;
    void calcDirectionAtCoord(Vec3f*, f32 coord) const;
    f32 calcNearestPos(const Vec3f&) const;
    f32 getTotalLength() const;
    f32 getPartLength(int) const;
    bool isLoop() const;
    bool isReachedGoal() const;
    bool isReachedEdge() const;
    void setCoord(f32 coord);
    void setSpeed(f32 speed);
    bool getRailArgWithInit(const char* pStr, s32* pOut) const;
    bool getRailArgNoInit(const char* pStr, s32* pOut) const;
    f32 getNextPointCoord() const;
    f32 getCurrentPointCoord() const;
    s32 getPointNum() const;
    void copyPointPos(Vec3f* pOut, s32 pointNum) const;
    f32 getPointCoord(s32 idx) const;
    void initBezierRail(const TableIter& rIter, const Table* pInfo);
    bool getPointArgS32NoInit(const char* pStr, s32* pOut, s32 pointNum) const;
    bool getPointArgS32WithInit(const char* pStr, s32* pOut, s32 pointNum) const;
    bool getCurrentPointArgS32NoInit(const char* pStr, s32* pOut) const;
    bool getCurrentPointArgS32WithInit(const char* pStr, s32* pOut) const;
    bool getNextPointArgS32NoInit(const char* pStr, s32* pOut) const;
    bool getNextPointArgS32WithInit(const char* pStr, s32* pOut) const;
    s32 getNextPointNo() const;
    void syncPosDir();

    BezierRail* mBezierRail;
    f32 mCoord;
    f32 mSpeed;
    bool mIsNotReverse;
    Vec3f mCurPos;
    Vec3f mCurDirection;
    Vec3f mStartPos;
    Vec3f mEndPos;
    s32 mCurPoint;
};
