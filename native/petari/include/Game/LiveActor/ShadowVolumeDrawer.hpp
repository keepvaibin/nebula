#pragma once

#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/ShadowDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Util/Color.hpp"

class ShadowVolumeDrawInit : public NameObj {
public:
    ShadowVolumeDrawInit();

    virtual ~ShadowVolumeDrawInit() override;
};

class ShadowVolumeDrawer : public ShadowDrawer {
public:
    ShadowVolumeDrawer(const char*);

    virtual ~ShadowVolumeDrawer() override {
    }

    virtual void draw() const override;
    // The matrix the shape draws under.
    virtual void loadModelDrawMtx(TypedDraw& rDraw) const;
    virtual void drawShape(TypedDraw& rDraw) const;
    virtual bool isDraw() const;
    // Changes when the shape is built from a different arrangement of its points (a box whose drop direction crossed a
    // face): the draws of each arrangement have numbers of their own, so two arrangements are never blended.
    virtual u32 shapeVariant() const {
        return 0;
    }

    void setStartDrawShepeOffset(f32);
    void setEndDrawShepeOffset(f32);
    void onCutDropShadow();
    void offCutDropShadow();
    void calcBaseDropPosition(Vec3f*) const;
    void calcBaseDropPosition(Vec3f*, const ShadowController*) const;
    f32 calcBaseDropLength() const;
    f32 calcBaseDropLength(const ShadowController*) const;

    f32 mStartDrawShapeOffset;
    f32 mEndDrawShapeOffset;
    bool mIsCutDropShadow;
};
