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

    /* 0x14 */ u8 _14;
    /* 0x18 */ f32 _18;
    /* 0x1C */ f32 _1C;
    /* 0x20 */ u8 _20;
};
