#pragma once

#include "Game/AreaObj/ImageEffectArea.hpp"

class BloomArea : public ImageEffectArea {
public:
    BloomArea(int, const char*);

    virtual ~BloomArea() override;
    virtual void init(const TableIter&) override;

    virtual const char* getManagerName() const override {
        return "ImageEffectArea";
    }

    u8 mThreshold;
    u8 mIntensity;
    s32 mIntensity1;
    s32 mIntensity2;
};
