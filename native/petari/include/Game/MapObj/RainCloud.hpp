#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LodCtrl;
class MapPartsRailMover;
class ModelObj;

class RainCloud : public LiveActor {
public:
    RainCloud(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void updateHitSensor(HitSensor* pSensor) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
