#pragma once

#include <nebula_geometry.hpp>

class HitSensor;
class TableIter;
class MapPartsRailGuideDrawer;
class MapPartsRailMover;
class MapPartsRailRotator;
class LiveActor;

namespace MR {
    class FormattedName;

    const HitSensor* getBodySensor(LiveActor*);
    void setBodySensorType(LiveActor*, u32);
    void setBodySensorTypeMapObj(LiveActor*);
    void setBodySensorTypePress(LiveActor*);
    void setBodySensorTypeMoveCollision(LiveActor*);
    bool isBodySensorTypeMapObj(const LiveActor*);
    bool receiveMapPartsRotateMsg(LiveActor*, u32, MapPartsRailMover*, MapPartsRailRotator*);
    bool tryStartMapPartsRotateAtPoint(LiveActor*, MapPartsRailMover*, MapPartsRailRotator*);
    bool tryEndMapPartsRotateAtPoint(LiveActor*, MapPartsRailMover*, MapPartsRailRotator*);
    bool tryStartMapPartsRotateBetweenPoints(LiveActor*, MapPartsRailMover*, MapPartsRailRotator*);
    bool isMapPartsRailMovePassedStartPointRepeat(const MapPartsRailMover*);
    bool isMapPartsRailMovePassedEndPointRepeat(const MapPartsRailMover*);
    void getMapPartsObjectName(FormattedName&, const TableIter&);
    void getMapPartsObjectNameIfExistShapeID(FormattedName&, const TableIter&);
    void getMapPartsObjectName(FormattedName&, const char*, s32);
    void initMapPartsClipping(LiveActor*, const TableIter&, Vec3f*, bool);
    bool isMapPartsPressOn(const TableIter&);
    MapPartsRailGuideDrawer* createMapPartsRailGuideDrawer(LiveActor*, const char*, const TableIter&);
    void initMapPartsShadow(LiveActor*, const TableIter&);

    bool getMapPartsArgMoveConditionType(s32*, const TableIter&);
    bool getMapPartsArgRotateSpeed(f32*, const TableIter&);
    bool getMapPartsArgRotateTime(s32*, const TableIter&);
    bool getMapPartsArgRotateAngle(f32*, const TableIter&);
    bool getMapPartsArgRotateAxis(s32*, const TableIter&);
    bool getMapPartsArgRotateAccelType(s32*, const TableIter&);
    bool getMapPartsArgRotateStopTime(s32*, const TableIter&);
    bool getMapPartsArgRotateType(s32*, const TableIter&);
    s32 getMapPartsArgShadowType(s32*, const TableIter&);
    bool getMapPartsArgSignMotionType(s32*, const TableIter&);
    bool getMapPartsArgMovePosture(s32*, const LiveActor*);
    bool getMapPartsArgMoveStopType(s32*, const LiveActor*);
    bool getMapPartsArgRailGuideType(s32*, const LiveActor*);
    bool getMapPartsArgRailInitPosType(s32*, const LiveActor*);
    bool getMapPartsArgMoveSpeed(f32*, const TableIter&);
    bool getMapPartsArgMoveSpeed(f32*, const LiveActor*);
    bool getMapPartsArgStopTime(s32*, const LiveActor*);
    bool getMapPartsArgAccelTime(s32*, const LiveActor*);
    bool getMapPartsArgSpeedCalcType(s32*, const LiveActor*);
    bool getMapPartsArgRailRotateSpeed(f32*, const TableIter&);
    bool getMapPartsArgRailRotateTime(s32*, const TableIter&);
    bool getMapPartsArgSpeedCalcType(s32*, const LiveActor*, s32);
    bool getMapPartsArgRailRotateSpeed(f32*, const LiveActor*, s32);
    bool getMapPartsArgRailRotateTime(s32*, const LiveActor*, s32);
    bool getMapPartsArgRailRotateAngle(f32*, const LiveActor*, s32);
    bool getMapPartsArgRailRotateAxis(s32*, const LiveActor*, s32);
    bool getMapPartsArgRailRotateType(s32*, const LiveActor*, s32);

    bool getMapPartsArgMoveTimeToNextPoint(s32*, const LiveActor*);

    bool isMapPartsSignMotionTypeMoveStart(s32);
    bool isMapPartsSignMotionTypeMoveWait(s32);
    bool hasMapPartsMoveStartSignMotion(s32);
    bool hasMapPartsVanishSignMotion(s32);
    bool isMapPartsShadowTypeNone(s32);
    bool hasMapPartsShadow(s32);
    bool isMoveStartTypeUnconditional(s32);
    bool isMoveStartTypePlayerOnStopEnd(s32);
    bool isMapPartsRailInitPosTypeRailPos(s32);
    bool isMapPartsRailInitPosTypeRailPoint(s32);
    bool isMapPartsRailInitPosTypePoint0(s32);
    bool isMapPartsRailSpeedCalcTypeTime(s32);

};  // namespace MR
