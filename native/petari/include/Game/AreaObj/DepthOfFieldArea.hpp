#pragma once

#include "Game/AreaObj/ImageEffectArea.hpp"

class DepthOfFieldArea : public ImageEffectArea {
public:
    DepthOfFieldArea(int, const char*);

    virtual void init(const TableIter&) override;

    virtual const char* getManagerName() const override {
        return "ImageEffectArea";
    }

    virtual bool isSyncWithPlayer() const override {
        return mIsPlayerSync;
    }

    f32 getIntensity() const;
    s32 getBlurMaxDist() const;
    s32 getBlurMinDist() const;

    bool mIsPlayerSync;
};
