#pragma once

#include "Game/LiveActor/ShadowVolumeModel.hpp"

class ShadowVolumeOval : public ShadowVolumeModel {
public:
    ShadowVolumeOval();

    virtual void loadModelDrawMtx() const;
    virtual bool isDraw() const;

    void setSize(const TVec3f&);

    TVec3f mSize;
};
