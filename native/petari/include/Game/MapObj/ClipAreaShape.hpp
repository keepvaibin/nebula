#pragma once

#include "Game/Render/TypedDraw.hpp"

#include <nebula_geometry.hpp>

class ModelData;

class ClipAreaShape {
public:
    ClipAreaShape(const char*);

    virtual bool isInArea(const Vec3f&) const;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const;

    bool isInArea(const Vec3f&, f32, const Pos3f&, const Vec3f&) const;
    void drawVolumeShape(TypedDraw& rDraw, const Pos3f&, const Vec3f&) const;

    ModelData* mModelData;
};

class ClipAreaShapeSphere : public ClipAreaShape {
public:
    ClipAreaShapeSphere();

    virtual bool isInArea(const Vec3f&) const override;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const override;

    f32 mRadius;
};

class ClipAreaShapeBox : public ClipAreaShape {
public:
    ClipAreaShapeBox(s32);

    virtual bool isInArea(const Vec3f&) const override;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const override;

    f32 mRadius;
    s32 mOriginType;
};

class ClipAreaShapeCone : public ClipAreaShape {
public:
    ClipAreaShapeCone(s32);

    virtual bool isInArea(const Vec3f&) const override;

    f32 mRadius;
    f32 mHeight;
    s32 mOriginType;
};
