#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"
#include <nebula_geometry.hpp>

class ModelData;

class ShadowVolumeFlatModel : public ShadowVolumeModel {
public:
    ShadowVolumeFlatModel(const char*);
    ShadowVolumeFlatModel();

    inline virtual ~ShadowVolumeFlatModel() override;
    virtual void draw() const override;
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const override;

    void initModel(const char*);
    void setBaseMatrixPtr(MtxPtr);
    void initJointDrawMtxResister();
    void calcRootJoint(Pos3f*, const Vec3f&) const;
    void calcDropJoint(Pos3f*, const Vec3f&) const;

    MtxPtr mBaseMtx;
    u32 mRootJoint;
    u32 mDropJoint;
};
