#pragma once

#include "Game/AreaObj/ImageEffectArea.hpp"

class ScreenBlurArea : public ImageEffectArea {
public:
    ScreenBlurArea(int, const char*);

    virtual void init(const TableIter&);

    virtual const char* getManagerName() const {
        return "ImageEffectArea";
    }

    virtual bool isSyncWithPlayer() const {
        return mIsPlayerSync;
    }

    bool mIsPlayerSync;
};
