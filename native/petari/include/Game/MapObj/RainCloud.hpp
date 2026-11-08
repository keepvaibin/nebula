#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LodCtrl;
class MapPartsRailMover;
class ModelObj;

class RainCloud : public LiveActor {
public:
    RainCloud(const char* pName);

    virtual void init(const TableIter& rIter);
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


    ModelObj* mCloudCylinder;
    Pos3f mRainCylinderMtx;
    Pos3f mSplashMtx;
    MapPartsRailMover* mRailMover;
    LodCtrl* mLodCtrl;
    Vec3f mClippingCenter;
    f32 mRainLength;
    f32 mShadowDropLength;
    bool mIsOnPlayer;
    bool mNoRain;
};
