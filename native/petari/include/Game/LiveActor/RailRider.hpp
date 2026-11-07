#pragma once

#include "Game/Map/BezierRail.hpp"

class RailRider {
public:
    RailRider(const TableIter&);
    RailRider(s32, s32);

    void move();
    void moveToNearestPos(const Vec3f&);
    void moveToNearestPoint(const Vec3f&);
    void moveToNextPoint();
    void reverse();
    void calcPosAtCoord(Vec3f*, f32) const;
    void calcDirectionAtCoord(Vec3f*, f32) const;
    f32 calcNearestPos(const Vec3f&) const;
    f32 getTotalLength() const;
    f32 getPartLength(int) const;
    bool isLoop() const;
    bool isReachedGoal() const;
    bool isReachedEdge() const;
    void setCoord(f32);
    void setSpeed(f32);
    bool getRailArgWithInit(const char*, s32*) const;
    bool getRailArgNoInit(const char*, s32*) const;
    f32 getNextPointCoord() const;
    f32 getCurrentPointCoord() const;
    s32 getPointNum() const;
    void copyPointPos(Vec3f*, s32) const;
    f32 getPointCoord(s32) const;
    void initBezierRail(const TableIter&, const Table*);
    bool getPointArgS32NoInit(const char*, s32*, s32) const;
    bool getPointArgS32WithInit(const char*, s32*, s32) const;
    bool getCurrentPointArgS32NoInit(const char*, s32*) const;
    bool getCurrentPointArgS32WithInit(const char*, s32*) const;
    bool getNextPointArgS32NoInit(const char*, s32*) const;
    bool getNextPointArgS32WithInit(const char*, s32*) const;
    s32 getNextPointNo() const;
    void syncPosDir();

    inline Vec3f getStartPos() {
        return mStartPos;
    }

    inline Vec3f getEndPos() {
        return mEndPos;
    }

    BezierRail* mBezierRail;
    f32 mCoord;
    f32 mSpeed;
    bool mIsNotReverse;
    u8 _D;
    u8 _E;
    u8 _F;
    Vec3f mCurPos;
    Vec3f mCurDirection;
    Vec3f mStartPos;
    Vec3f mEndPos;
    s32 mCurPoint;
};
