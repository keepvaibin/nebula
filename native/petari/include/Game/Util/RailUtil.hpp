#pragma once

#include <nebula_geometry.hpp>

#include <NebulaGpu.h>
#include <nebula_math.h>

class LiveActor;
class RailRider;

namespace MR {
    void moveTransToOtherActorRailPos(LiveActor*, const LiveActor*);
    void moveTransToCurrentRailPos(LiveActor*);

    void moveCoord(LiveActor*, f32);
    void moveCoordAndTransToRailPoint(LiveActor*, s32);
    s32 moveCoordAndCheckPassPointNo(LiveActor*, f32);
    s32 moveCoordAndFollowTransAndCheckPassPointNo(LiveActor*);
    void moveCoordToRailPoint(LiveActor*, s32);
    void moveCoordToNearestPos(LiveActor*, const Vec3f&);
    void moveCoordAndFollowTrans(LiveActor*, f32);
    void moveCoordAndFollowTrans(LiveActor*);
    void moveCoordToStartPos(LiveActor*);
    void moveCoordToEndPos(LiveActor*);

    void moveCoordAndTransToNearestRailPos(LiveActor*);
    void moveCoordAndTransToNearestRailPos(LiveActor*, const Vec3f&);
    void moveCoordAndTransToNearestRailPoint(LiveActor*);
    void moveCoordAndTransToNextPoint(LiveActor*);
    void moveCoordAndTransToRailStartPoint(LiveActor*);

    void setRailDirectionToStart(LiveActor*);
    void setRailDirectionToEnd(LiveActor*);
    void setRailDirectionCloseToCoord(LiveActor*, f32);
    void setRailDirectionCloseToNearestPos(LiveActor*, const Vec3f&);
    void reverseRailDirection(LiveActor*);

    f32 calcMovingDirectionAlongRail(LiveActor*, Vec3f*, const Vec3f&, f32, bool, bool*);
    f32 calcMovingDirectionAlongRailH(LiveActor*, Vec3f*, const Vec3f&, f32, bool*);

    void calcRailClippingInfo(Vec3f*, f32*, const LiveActor*, f32, f32);
    void initAndSetRailClipping(Vec3f*, LiveActor*, f32, f32);

    f32 calcNearestRailCoord(const LiveActor*, const Vec3f&);
    f32 calcNearestRailPos(Vec3f*, const LiveActor*, const Vec3f&);
    f32 calcNearestRailDirection(Vec3f*, const LiveActor*, const Vec3f&);
    f32 calcNearestRailPosAndDirection(Vec3f*, Vec3f*, const LiveActor*, const Vec3f&);
    f32 calcRailPosNearestPlayer(Vec3f*, const LiveActor*);

    void calcBoundingBox(RailRider*, Box3f*, f32);
    void calcBoundingBox(const LiveActor*, Box3f*, f32);

    f32 calcDifferenceRailCoord(const LiveActor*, f32, f32);
    f32 calcDifferenceRailCoord(const LiveActor*, f32);
    f32 calcDistanceHorizonToCurrentPos(const LiveActor*);

    void calcRailPosAtCoord(Vec3f*, const LiveActor*, f32);
    void calcRailPosFrontCoord(Vec3f*, const LiveActor*, f32);
    void calcRailDirectionAtCoord(Vec3f*, const LiveActor*, f32);
    void calcRailPosAndDirectionAtCoord(Vec3f*, Vec3f*, const LiveActor*, f32);
    void calcRailStartPos(Vec3f*, const LiveActor*);
    void calcRailEndPos(Vec3f*, const LiveActor*);
    void calcRailPointPos(Vec3f*, const LiveActor*, int);
    void calcRailStartPointPos(Vec3f*, const LiveActor*);
    void calcRailEndPointPos(Vec3f*, const LiveActor*);
    void calcRailStartPointDirection(Vec3f*, const LiveActor*);
    void calcRailEndPointDirection(Vec3f*, const LiveActor*);
    void calcRailStartPointPosAndDirection(Vec3f*, Vec3f*, const LiveActor*);
    void calcRailEndPointPosAndDirection(Vec3f*, Vec3f*, const LiveActor*);

    void calcDistanceToNextRailPoint(const LiveActor*, f32*);
    void calcDistanceToCurrentAndNextRailPoint(const LiveActor*, f32*, f32*);
    void calcRailRateToNextPoint(f32*, const LiveActor*);

    bool isExistRail(const LiveActor*);

    s32 getRailPointNum(const LiveActor*);
    s32 getCurrentRailPointNo(const LiveActor*);
    s32 getNextRailPointNo(const LiveActor*);
    f32 getRailTotalLength(const LiveActor*);
    f32 getRailPartLength(const LiveActor*, int);
    f32 getRailPointCoord(const LiveActor*, int);
    const Vec3f& getRailPos(const LiveActor*);
    const Vec3f& getRailPointPosStart(const LiveActor*);
    const Vec3f& getRailPointPosEnd(const LiveActor*);
    const Vec3f& getRailDirection(const LiveActor*);
    f32 getRailCoord(const LiveActor*);
    f32 getRailCoordSpeed(const LiveActor*);

    void setRailCoord(LiveActor*, f32);
    void setRailCoordSpeed(LiveActor*, f32);
    void accelerateRailCoordSpeed(LiveActor*, f32);
    void adjustmentRailCoordSpeed(LiveActor*, f32, f32);
    void slowDownRailCoordSpeed(LiveActor*, f32);
    void moveRailRider(LiveActor*);

    bool isLoopRail(const LiveActor*);
    bool isRailReachedGoal(const LiveActor*);
    bool isRailReachedNearGoal(const LiveActor*, f32);
    bool isRailReachedEdge(const LiveActor*);
    bool isRailGoingToEnd(const LiveActor*);
    bool isRailReachedNearNextPoint(const LiveActor*, f32);
    bool isRailReachedHorizonCurrentPos(const LiveActor*, f32);

    bool getRailArg0NoInit(const LiveActor*, s32*);
    bool getRailArg1NoInit(const LiveActor*, s32*);
    bool getRailArg2NoInit(const LiveActor*, s32*);
    bool getRailArg3NoInit(const LiveActor*, s32*);
    bool getRailArg4NoInit(const LiveActor*, s32*);
    bool getRailArg1WithInit(const RailRider*, s32*);

    bool getRailPointArg0WithInit(const LiveActor*, s32, s32*);
    bool getRailPointArg2WithInit(const LiveActor*, s32, s32*);
    bool getRailPointArg0NoInit(const LiveActor*, s32, s32*);
    bool getRailPointArg1NoInit(const LiveActor*, s32, s32*);
    bool getRailPointArg4NoInit(const LiveActor*, s32, s32*);
    bool getRailPointArg6NoInit(const LiveActor*, s32, s32*);
    bool getRailPointArg7NoInit(const LiveActor*, s32, s32*);
    bool getRailPointArg2WithInit(const LiveActor*, s32, f32*);
    bool getRailPointArg3WithInit(const LiveActor*, s32, f32*);
    bool getRailPointArg0NoInit(const LiveActor*, s32, f32*);
    bool getRailPointArg1NoInit(const LiveActor*, s32, f32*);

    bool getCurrentRailPointArg0WithInit(const LiveActor*, s32*);
    bool getCurrentRailPointArg1WithInit(const LiveActor*, s32*);
    bool getCurrentRailPointArg0NoInit(const LiveActor*, s32*);
    bool getCurrentRailPointArg1NoInit(const LiveActor*, s32*);
    bool getCurrentRailPointArg5NoInit(const LiveActor*, s32*);
    bool getCurrentRailPointArg7NoInit(const LiveActor*, s32*);
    bool getCurrentRailPointArg0WithInit(const LiveActor*, f32*);
    bool getCurrentRailPointArg0NoInit(const LiveActor*, f32*);
    bool getCurrentRailPointArg1NoInit(const LiveActor*, f32*);

    bool getNextRailPointArg0WithInit(const LiveActor*, s32*);
    bool getNextRailPointArg1NoInit(const LiveActor*, s32*);
    bool getNextRailPointArg0NoInit(const LiveActor*, f32*);
    bool getNextRailPointArg1NoInit(const LiveActor*, f32*);
    bool getNextRailPointArg2WithInit(const LiveActor*, bool*);

    bool getRailArg0WithInit(const LiveActor*, s32*);
};  // namespace MR
