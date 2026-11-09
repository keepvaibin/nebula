#pragma once

#include "Game/Render/TypedDraw.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Color.hpp"

class GameTexture;

class WaterCameraFilter : public LiveActor {
public:
    WaterCameraFilter();

    virtual void init(const TableIter& rIter);
    virtual void draw() const;
    virtual void control();

    void exeAirToWater();
    void exeWaterToAir();
    void exeWater();
    void exeAir();
    SiteMaterial loadMaterial() const;

    inline void initScreenTex();

    f32 mWaterAlpha;
    f32 mWavePhase;
    Color8 mWaterColor;
    GameTexture* mScreenTex;
    GameTexture* mFilterTex;
};
