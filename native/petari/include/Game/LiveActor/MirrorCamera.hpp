#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class ModelData;

class MirrorCamera : public NameObj {
public:
    MirrorCamera(const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    void setMirrorMapInfo(const Vec3f&, const Vec3f&);
    void setMirrorMapInfo(ModelData*);
    void updateViewMtx();
    void updateModelTexMtx();
    f32 getDistance(const Vec3f&) const;

    // The mirror plane: mMirrorNormal . p + mPlaneD = 0.
    Vec3f mMirrorPos;
    Vec3f mMirrorNormal;
    f32 mPlaneD;
    Pos3f mViewMtx;
    Pos3f mModelTexMtx;
};

namespace MR {
    MirrorCamera* getMirrorCamera();
    f32 getDistanceToMirror(const Vec3f&);
};  // namespace MR
