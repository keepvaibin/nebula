#pragma once

#include "Game/Render/TypedDraw.hpp"

#include <nebula_geometry.hpp>

class ModelData;
struct NebulaFrameClipVolume;

class ClipAreaShape {
public:
    ClipAreaShape(const char*);

    virtual bool isInArea(const Vec3f&) const;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const;
    // The unit shape it is (NEBULA_CLIP_*) and its size along each axis there, or -1 for none.
    virtual s32 volumeShape(Vec3f* pSize) const;

    bool isInArea(const Vec3f&, f32, const Pos3f&, const Vec3f&) const;
    void drawVolumeShape(TypedDraw& rDraw, const Pos3f&, const Vec3f&) const;
    // The volume as the frame list takes it (Frame::clipVolume), as isInArea measures; false where it holds nothing.
    bool frameVolume(NebulaFrameClipVolume* pVolume, const Pos3f&, const Vec3f&) const;

    ModelData* mModelData;
};

class ClipAreaShapeSphere : public ClipAreaShape {
public:
    ClipAreaShapeSphere();

    virtual bool isInArea(const Vec3f&) const override;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const override;
    virtual s32 volumeShape(Vec3f* pSize) const override;

    f32 mRadius;
};

class ClipAreaShapeBox : public ClipAreaShape {
public:
    ClipAreaShapeBox(s32);

    virtual bool isInArea(const Vec3f&) const override;
    virtual void calcVolumeMatrix(Pos3f*, const Pos3f&, const Vec3f&) const override;
    virtual s32 volumeShape(Vec3f* pSize) const override;

    f32 mRadius;
    s32 mOriginType;
};

class ClipAreaShapeCone : public ClipAreaShape {
public:
    ClipAreaShapeCone(s32);

    virtual bool isInArea(const Vec3f&) const override;
    virtual s32 volumeShape(Vec3f* pSize) const override;

    f32 mRadius;
    f32 mHeight;
    s32 mOriginType;
};
