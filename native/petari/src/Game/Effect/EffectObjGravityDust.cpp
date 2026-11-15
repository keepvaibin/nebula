#include "Game/Effect/EffectObjGravityDust.hpp"
#include "Game/Map/GravityDust.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/System/LumaCore.hpp"

EffectObjGravityDust::EffectObjGravityDust(const char* pName) : SimpleEffectObj(pName), mParticleCallBack() {
    lumacore::setClass(this, lumacore::kClassId< EffectObjGravityDust >);
}

void EffectObjGravityDust::init(const TableIter& rIter) {
    SimpleEffectObj::init(rIter);
    mParticleCallBack = new GravityDustParticleCallBack(this);
    MR::emitEffectWithParticleCallBack(this, mObjName, mParticleCallBack);
    MR::setEffectLocalScale(this, mObjName, mScale);
}

void EffectObjGravityDust::endClipped() {
    SimpleEffectObj::endClipped();
    MR::emitEffectWithParticleCallBack(this, mObjName, mParticleCallBack);
}

f32 EffectObjGravityDust::getClippingRadius() const {
    return mScale.length() * 500.0f;
}

Vec3f EffectObjGravityDust::getClippingCenterOffset() const {
    return Vec3f(0.0f, 500.0f * mScale.y, 0.0f);
}
