#include "Game/AreaObj/SoundEmitterCube.hpp"
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
    const char* const cSoundNameTable[] = {"SE_AT_LV_GRAVE", "SE_AT_LV_KOOPA_WIND", "SE_AT_LV_WIND_DESERT", "SE_AT_LV_WIND_ICE_MT_TOP"};
};  // namespace

SoundEmitterCube::SoundEmitterCube(int formType, const char* pName) : AreaObj(formType, pName), mSoundObj(), mSoundPos(0.0f, 0.0f, 0.0f), mSoundName() {
    lumacore::setClass(this, lumacore::kClassId< SoundEmitterCube >);
}

void SoundEmitterCube::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    // Outside the table (unset is -1): silent.
    mSoundName = mObjArg0 >= 0 && mObjArg0 < ARRAY_SIZE(::cSoundNameTable) ? ::cSoundNameTable[mObjArg0] : nullptr;
    mSoundObj = new AudAnmSoundObject(&mSoundPos, 4, NebulaArena::current());

    MR::connectToSceneAreaObj(this);
    MR::registerDemoSimpleCastAll(this);
}

void SoundEmitterCube::movement() {
    switch (mObjArg0) {
    case 0:
    case 3:
        mSoundPos.set(MR::getCameraWatchPos());
        break;
    default:
        mSoundPos.set(AudMicWrap::getMicPos());
        break;
    }

    DirBox3f worldBox;
    MR::calcCubeWorldBox(&worldBox, this);
    Box3f bounds;
    bounds.i.set(worldBox.mCorner);
    bounds.f.set(worldBox.mCorner);
    Vec3f cornerZ;
    Vec3f cornerY;
    Vec3f cornerX;
    cornerX.scaleAdd(worldBox.mSize.x, worldBox.mAxisX, worldBox.mCorner);
    cornerY.scaleAdd(worldBox.mSize.y, worldBox.mAxisY, worldBox.mCorner);
    cornerZ.scaleAdd(worldBox.mSize.z, worldBox.mAxisZ, worldBox.mCorner);
    bounds.extend(cornerX);
    bounds.extend(cornerY);
    bounds.extend(cornerZ);

    cornerX.scaleAdd(worldBox.mSize.y, worldBox.mAxisY, cornerX);
    cornerY.scaleAdd(worldBox.mSize.z, worldBox.mAxisZ, cornerY);
    cornerZ.scaleAdd(worldBox.mSize.x, worldBox.mAxisX, cornerZ);
    bounds.extend(cornerX);
    bounds.extend(cornerY);
    bounds.extend(cornerZ);

    cornerX.scaleAdd(worldBox.mSize.z, worldBox.mAxisZ, cornerX);
    bounds.extend(cornerX);

    if (mSoundPos.x < bounds.i.x) {
        mSoundPos.x = bounds.i.x;
    }

    if (mSoundPos.x > bounds.f.x) {
        mSoundPos.x = bounds.f.x;
    }

    if (mSoundPos.y < bounds.i.y) {
        mSoundPos.y = bounds.i.y;
    }

    if (mSoundPos.y > bounds.f.y) {
        mSoundPos.y = bounds.f.y;
    }

    if (mSoundPos.z < bounds.i.z) {
        mSoundPos.z = bounds.i.z;
    }

    if (mSoundPos.z > bounds.f.z) {
        mSoundPos.z = bounds.f.z;
    }

    mSoundObj->process();
    if (mSoundName != nullptr) {
        MR::startSoundObjectLevel(mSoundObj, mSoundName);
    }
}
