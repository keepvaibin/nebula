#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class ImageEffectArea : public AreaObj {
public:
    enum EImageEffectType {
        IMAGE_EFFECT_TYPE_BLOOM,
        IMAGE_EFFECT_TYPE_SIMPLE_BLOOM,
        IMAGE_EFFECT_TYPE_BLUR,
        IMAGE_EFFECT_TYPE_DOF,
    };

    ImageEffectArea(EImageEffectType, int, const char*);

    virtual bool isSyncWithPlayer() const {
        return false;
    }

    s32 mEffectType;
};

class ImageEffectAreaMgr : public AreaObjMgr {
public:
    ImageEffectAreaMgr(s32, const char*);

    virtual void initAfterPlacement();

    void sort();
};
