#pragma once

#include <nebula_geometry.hpp>

class J3DModelData;

class ClipAreaShape {
public:
    ClipAreaShape(const char*);

    virtual bool isInArea(const Vec3f&) const;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const;

    bool isInArea(const Vec3f&, f32, const Pos3f&, const Vec3f&) const;
    void drawVolumeShape(const Pos3f&, const Vec3f&) const;

    J3DModelData* mModelData;
};

class ClipAreaShapeSphere : public ClipAreaShape {
public:
    ClipAreaShapeSphere();

    virtual bool isInArea(const Vec3f&) const;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const;

    f32 mRadius;
};

class ClipAreaShapeBox : public ClipAreaShape {
public:
    ClipAreaShapeBox(s32);

    virtual bool isInArea(const Vec3f&) const;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const;

    f32 mRadius;
    s32 _C;
};

class ClipAreaShapeCone : public ClipAreaShape {
public:
    ClipAreaShapeCone(s32);

    virtual bool isInArea(const Vec3f&) const;

    f32 _8;
    f32 _C;
    s32 _10;
};
