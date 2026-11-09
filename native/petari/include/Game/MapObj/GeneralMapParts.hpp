#pragma once

#include "Game/MapObj/MapParts.hpp"
#include "Game/Util/Array.hpp"

class ActorCameraInfo;
class MapPartsAppearController;
class MapPartsFunction;
class MapPartsRailGuideDrawer;
class MapPartsRailMover;
class MapPartsRailPosture;
class MapPartsRailRotator;
class MapPartsRotator;

class GeneralMapParts : public MapParts {
public:
    GeneralMapParts(const char* pName);

    virtual ~GeneralMapParts() {
    }

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initMapPartsFunction(const TableIter& rIter);
    void initSensorType(const TableIter& rIter);
    void initGravity(const TableIter& rIter);
    void receiveMsgSwitchBOn();
    void receiveMsgSwitchBOff();
    void broadcastMsgToAllFunctions(u32 msg);
    bool isFixed() const;
    void startMove();
    void exeWait();
    void exeWaitForPlayerOn();
    void exeMoveStart();

    ActorCameraInfo* mCameraInfo;
    s32 mMoveConditionType;
    s32 mSignMotionType;
    s32 mShadowType;
    MR::InlineVector< MapPartsFunction*, 8 > mFunctionArray;
    MapPartsAppearController* mAppearController;
    MapPartsRailRotator* mRailRotator;
    MapPartsRailMover* mRailMover;
    MapPartsRotator* mRotator;
    MapPartsRailGuideDrawer* mGuideDrawer;
    MapPartsRailPosture* mRailPosture;
    u8 mIsCameraEnded;
    u8 mIsPaused;
};
