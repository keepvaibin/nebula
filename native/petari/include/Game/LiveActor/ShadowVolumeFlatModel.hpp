#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"
#include <nebula_geometry.hpp>

class J3DModelData;

class ShadowVolumeFlatModel : public ShadowVolumeModel {
public:
    ShadowVolumeFlatModel(const char*);
    ShadowVolumeFlatModel();

    inline virtual ~ShadowVolumeFlatModel();
    virtual void draw() const;
    virtual void loadModelDrawMtx() const;

    void initModel(const char*);
    void setBaseMatrixPtr(MtxPtr);
    void initJointDrawMtxResister();
    void calcRootJoint(Pos3f*, const Vec3f&) const;
    void calcDropJoint(Pos3f*, const Vec3f&) const;

    MtxPtr mBaseMtx;
    s32 mRootDrawMtxIndex;
    s32 mDropDrawMtxIndex;
};
