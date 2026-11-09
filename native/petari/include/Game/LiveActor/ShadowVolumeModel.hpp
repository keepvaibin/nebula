#pragma once

#include "Game/LiveActor/ShadowVolumeDrawer.hpp"

class ModelData;

class ShadowVolumeModel : public ShadowVolumeDrawer {
public:
    ShadowVolumeModel(const char*);

    virtual ~ShadowVolumeModel() override;
    virtual void drawShape(TypedDraw& rDraw) const override;

    void initVolumeModel(const char*);

    ModelData* mModelData;
};

inline ShadowVolumeModel::~ShadowVolumeModel() {
}
