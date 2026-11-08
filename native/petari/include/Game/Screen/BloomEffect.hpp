#pragma once

#include "Game/Screen/ImageEffectBase.hpp"

class BloomEffect : public ImageEffectBase {
public:
    BloomEffect(const char* pName);

    virtual ~BloomEffect() {
    }

    void preDraw() const;
    void postDraw() const;
    u8 getIntensity1Default() const;
    u8 getIntensity2Default() const;
    bool isForEachModel() const;
    void initDraw() const;

    u8 mBloomIntensity;
    f32 mIntensity1;
    f32 mIntensity2;
    u8 mThreshold;
};
