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

    u8 _14;
    f32 _18;
    f32 _1C;
    u8 _20;
};
