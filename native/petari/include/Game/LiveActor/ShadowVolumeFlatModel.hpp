#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

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
    void calcRootJoint(TPos3f*, const TVec3f&) const;
    void calcDropJoint(TPos3f*, const TVec3f&) const;

    MtxPtr mBaseMtx;
    s32 mRootDrawMtxIndex;
    s32 mDropDrawMtxIndex;
};
