#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class ClippingJudge : public NameObj {
public:
    ClippingJudge(const char*);

    virtual ~ClippingJudge();
    virtual void init(const TableIter&);

    virtual void movement();

    bool isJudgedToClipFrustum(const Vec3f&, f32) const;
    bool isJudgedToClipFrustum(const Vec3f&, f32, s32) const;
    void calcViewingVolume(Hex3f*, f32);

    Hex3f mFrustum;
    Hex3f mClipFrustums[0x8];
    f32 mClipDistances[0x8];
};

namespace MR {
    ClippingJudge* getClippingJudge();
};  // namespace MR
