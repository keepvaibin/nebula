#pragma once

#include "Game/LiveActor/ShadowVolumeDrawer.hpp"

class ShadowVolumeLine : public ShadowVolumeDrawer {
public:
    ShadowVolumeLine();

    virtual ~ShadowVolumeLine() override;
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const override;
    virtual void drawShape(TypedDraw& rDraw) const override;
    virtual u32 calcDrawLives() const override;
    // Each flip of a side vector (the line swung through a drop direction) has draw numbers of its own.
    virtual u32 shapeVariant() const override {
        return mSideFlips & 7u;
    }
    bool calcLine(Vec3f* pFrom, Vec3f* pTo, Vec3f* pFromDrop, Vec3f* pToDrop, Vec3f* pFromSide, Vec3f* pToSide) const;

    void setFromShadowController(const ShadowController*);
    void setToShadowController(const ShadowController*);
    void setFromWidth(f32);
    void setToWidth(f32);

    const ShadowController* mFromShadowController;
    const ShadowController* mToShadowController;
    f32 mFromWidth;
    f32 mToWidth;
    mutable Vec3f mLastFromSide;
    mutable Vec3f mLastToSide;
    mutable u32 mSideFlips;
    mutable bool mHasSides;
};