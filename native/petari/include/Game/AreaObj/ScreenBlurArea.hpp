#pragma once

#include "Game/AreaObj/ImageEffectArea.hpp"

class ScreenBlurArea : public ImageEffectArea {
public:
    ScreenBlurArea(int, const char*);

    virtual void init(const TableIter&) override;

    virtual const char* getManagerName() const override {
        return "ImageEffectArea";
    }

    virtual bool isSyncWithPlayer() const override {
        return mIsPlayerSync;
    }

    bool mIsPlayerSync;
};
