#include "Game/Effect/EffectObjGravityDust.hpp"
#include "Game/Map/GravityDust.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getClippingRadius_f1, "getClippingRadius.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_getClippingCenterOffset_f1, "getClippingCenterOffset.f1", 500.0f);

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
    return mScale.length() * cNum_getClippingRadius_f1;
}

Vec3f EffectObjGravityDust::getClippingCenterOffset() const {
    return Vec3f(0.0f, cNum_getClippingCenterOffset_f1 * mScale.y, 0.0f);
}
