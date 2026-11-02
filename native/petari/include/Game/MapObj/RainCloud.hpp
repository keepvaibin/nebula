#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class LodCtrl;
class MapPartsRailMover;
class ModelObj;
class NameObjArchiveListCollector;

class RainCloud : public LiveActor {
public:
    RainCloud(const char* pName);

    virtual ~RainCloud();
    virtual void init(const JMapInfoIter& rIter);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeAppear();
    void exeWait();
    void exeDisappear();
    void exeEnd();
    void exeSoftTouch();
    void exeHardTouch();
    void switchEffect();
    void updateRainCylinder();
    bool isNextStartOK() const;

    static void makeArchiveList(NameObjArchiveListCollector* pCollector, const JMapInfoIter& rIter);

    ModelObj* mCloudCylinder;
    TPos3f mRainCylinderMtx;
    TPos3f mSplashMtx;
    MapPartsRailMover* mRailMover;
    LodCtrl* mLodCtrl;
    TVec3f mClippingCenter;
    f32 mRainLength;
    f32 mShadowDropLength;
    bool mIsOnPlayer;
    bool mNoRain;
};
