#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Color.hpp"

class JUTTexture;

class WaterCameraFilter : public LiveActor {
public:
    WaterCameraFilter();

    virtual void init(const JMapInfoIter& rIter);
    virtual void draw() const;
    virtual void control();

    void exeAirToWater();
    void exeWaterToAir();
    void exeWater();
    void exeAir();
    void loadMaterial() const;

    inline void initScreenTex();

    f32 mWaterAlpha;
    f32 mWavePhase;
    Color8 mWaterColor;
    JUTTexture* mScreenTex;
    JUTTexture* mFilterTex;
};
