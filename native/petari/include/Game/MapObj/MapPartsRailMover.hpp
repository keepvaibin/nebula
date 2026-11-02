#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <JSystem/JGeometry.hpp>

class MapPartsRailPointPassChecker;

class MapPartsRailMover : public MapPartsFunction {
public:
    MapPartsRailMover(LiveActor*);

    virtual ~MapPartsRailMover();
    virtual void init(const JMapInfoIter&);
    virtual void movement();
    virtual bool isWorking() const;
    virtual void start();
    virtual void end();
    virtual bool receiveMsg(u32);

    void moveToInitPos();
    void startWithSignalMotion();
    void cancelSignalMotion();
    bool tryResetPositionRepeat();
    void resetToInitPos();
    void passPoint();
    void reachedEnd();
    void reachedEndPlayerOn();
    bool isReachedEnd() const;
    bool isDone() const;
    void setStateStopAtEndBeforeRotate();
    void calcTimeToNextRailPoint(f32*) const;
    void endRotateAtPoint();
    void calcMoveSpeed(f32*) const;
    void calcMoveSpeedDirect(f32*) const;
    void calcMoveSpeedTime(f32*) const;
    void updateAccel();
    bool tryPassPoint();
    bool tryRestartAtEnd();
    void restartAtEnd();
    void exeMove();
    void exeMoveStart();
    void exeStopAtPoint();
    void exeStopAtEnd();
    void exeWait();
    void exeVanish();
    void exeRotateAtPoint();
    void exeWaitForRestartByPlayerOn();
    void exeStopAtEndWithPlayerOn();
    void exeRotateAtEndPoint();

    MapPartsRailPointPassChecker* mRailPointPassChecker;
    s32 mMoveConditionType;
    s32 mMoveStopType;
    s32 mSignMotionType;
    TVec3f _28;
    f32 _34;
    s32 mStopTime;
    f32 mSpeed;
    s32 mAccelTime;
    f32 mAcceleration;
    f32 _48;
};
