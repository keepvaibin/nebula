#include "Game/AreaObj/GlaringLightArea.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

GlaringLightAreaMgr::GlaringLightAreaMgr(s32 maxNum, const char* pName) : AreaObjMgr(maxNum, pName) {
}

GlaringLightArea::GlaringLightArea(int formType, const char* pName) : AreaObj(formType, pName), mSoundObj(), mPos(0.0f, 0.0f, 0.0f) {
}

void GlaringLightArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    Vec3f pos;
    MR::calcCylinderPos(&pos, this);

    mPos.set(pos);

    mSoundObj = new AudSoundObject(&mPos, 4, NebulaArena::current());

    if (isValidSwitchA()) {
        MR::connectToSceneAreaObj(this);
    }
}

void GlaringLightArea::movement() {
    if (isOnSwitchA()) {
        if (!isValid()) {
            MR::startSoundObject(mSoundObj, "SE_OJ_RAY_START");
        }

        mIsValid = true;
    } else {
        mIsValid = false;
    }

    mSoundObj->process();
}
