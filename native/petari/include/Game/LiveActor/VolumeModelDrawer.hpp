#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Color.hpp"
#include <JSystem/J3DGraphAnimator/J3DModelData.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class VolumeModelDrawer : public NameObj {
public:
    VolumeModelDrawer(const char*, const char*, MtxPtr);

    virtual void draw() const;

    void loadModelDrawMtx() const;

    MtxPtr mMtx;
    J3DModelData* mModelData;
    Color8 mColor;
};

class VolumeModelDrawInit : public NameObj {
public:
    VolumeModelDrawInit();
};
