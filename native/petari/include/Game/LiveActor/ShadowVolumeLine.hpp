#pragma once

#include "Game/LiveActor/ShadowVolumeDrawer.hpp"

class ShadowVolumeLine : public ShadowVolumeDrawer {
public:
    ShadowVolumeLine();

    virtual ~ShadowVolumeLine();
    virtual void loadModelDrawMtx() const;
    virtual void drawShape() const;

    void setFromShadowController(const ShadowController*);
    void setToShadowController(const ShadowController*);
    void setFromWidth(f32);
    void setToWidth(f32);

    const ShadowController* mFromShadowController;
    const ShadowController* mToShadowController;
    f32 mFromWidth;
    f32 mToWidth;
};