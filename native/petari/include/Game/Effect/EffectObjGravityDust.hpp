#pragma once

#include "Game/Effect/SimpleEffectObj.hpp"

class GravityDustParticleCallBack;

class EffectObjGravityDust : public SimpleEffectObj {
public:
    EffectObjGravityDust(const char*);

    virtual void init(const TableIter& rIter);
    virtual void endClipped();
    virtual f32 getClippingRadius() const;
    virtual f32 getFarClipDistance() const {
        return 50.0f;
    }
    virtual Vec3f getClippingCenterOffset() const;
    virtual bool isSyncClipping() const {
        return true;
    }

private:
    GravityDustParticleCallBack* mParticleCallBack;
};
