#include "Game/AreaObj/SoundEmitterSphere.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/AudioLib/AudMicWrap.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const char* const cSoundNameTable[] = {"SE_AT_LV_MAGMA_AMBIENT",      "SE_OJ_LV_FLOATING_SAND",    "SE_AT_LV_WIND_DESERT",
                                     "SE_AT_LV_MAGMA_WIND_VOLCANO", "SE_AT_LV_KOOPA_FLOAT_SAND", "SE_AT_LV_WIND_RABBIT_MAZE"};
};  // namespace

SoundEmitterSphere::SoundEmitterSphere(int formType, const char* pName)
    : AreaObj(formType, pName), mSoundObj(), mSoundPos(0.0f, 0.0f, 0.0f), mSoundName(), mSoundIndex() {
    lumacore::setClass(this, lumacore::kClassId< SoundEmitterSphere >);
}

void SoundEmitterSphere::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    // Outside the table (unset is -1): silent.
    mSoundName = mObjArg0 >= 0 && mObjArg0 < ARRAY_SIZE(::cSoundNameTable) ? ::cSoundNameTable[mObjArg0] : nullptr;
    mSoundIndex = mObjArg0;
    mSoundObj = new AudAnmSoundObject(&mSoundPos, 4, NebulaArena::current());

    MR::connectToSceneAreaObj(this);
    MR::registerDemoSimpleCastAll(this);
}

void SoundEmitterSphere::movement() {
    switch (mSoundIndex) {
    case 1:
    case 2:
    case 4:
    case 5:
        mSoundPos.set(MR::getCameraWatchPos());
        break;
    case 0:
    case 3:
    default:
        mSoundPos.set(AudMicWrap::getMicPos());
        break;
    }

    if (!isInVolume(mSoundPos)) {
        Vec3f center;
        MR::calcSpherePos(&center, this);
        f32 radius = MR::getSphereRadius(this);
        f32 rate = radius / (mSoundPos - center).length();
        mSoundPos.x = center.x + rate * (mSoundPos.x - center.x);
        mSoundPos.y = center.y + rate * (mSoundPos.y - center.y);
        mSoundPos.z = center.z + rate * (mSoundPos.z - center.z);
    }

    mSoundObj->process();
    if (mSoundName != nullptr) {
        MR::startSoundObjectLevel(mSoundObj, mSoundName);
    }
}
