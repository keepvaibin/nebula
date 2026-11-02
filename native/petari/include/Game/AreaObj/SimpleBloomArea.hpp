#pragma once

#include "Game/AreaObj/ImageEffectArea.hpp"

class SimpleBloomArea : public ImageEffectArea {
public:
    enum EMaskFilterColor {
        MASK_FILTER_ALL,
        MASK_FILTER_RED,
        MASK_FILTER_GREEN,
        MASK_FILTER_BLUE,
    };

    SimpleBloomArea(int, const char*);

    virtual void init(const JMapInfoIter&);

    virtual const char* getManagerName() const {
        return "ImageEffectArea";
    }

    u32 mMaskFilterColor;
    u8 mThreshold;
    u8 mIntensity;
};
