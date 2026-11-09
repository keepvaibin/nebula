#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Render/ModelData.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class VolumeModelDrawer : public NameObj {
public:
    VolumeModelDrawer(const char*, const char*, MtxPtr);

    virtual void draw() const override;


    MtxPtr mMtx;
    ModelData* mModelData;
    Color8 mColor;
};

class VolumeModelDrawInit : public NameObj {
public:
    VolumeModelDrawInit();
};
