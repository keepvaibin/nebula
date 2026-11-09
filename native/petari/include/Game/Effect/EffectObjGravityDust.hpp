#pragma once

#include "Game/Effect/SimpleEffectObj.hpp"

class GravityDustParticleCallBack;

class EffectObjGravityDust : public SimpleEffectObj {
public:
    EffectObjGravityDust(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void endClipped() override;
    virtual f32 getClippingRadius() const override;
    virtual f32 getFarClipDistance() const override {
        return 50.0f;
    }
    virtual Vec3f getClippingCenterOffset() const override;
    virtual bool isSyncClipping() const override {
        return true;
    }

private:
    GravityDustParticleCallBack* mParticleCallBack;
};
