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

    virtual void init(const TableIter&) override;

    virtual const char* getManagerName() const override {
        return "ImageEffectArea";
    }

    u32 mMaskFilterColor;
    u8 mThreshold;
    u8 mIntensity;
};
