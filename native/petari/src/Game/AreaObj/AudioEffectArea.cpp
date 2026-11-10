#include "Game/AreaObj/AudioEffectArea.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

AudioEffectArea::AudioEffectArea(int formType, const char* pName) : AreaObj(formType, pName), mEffectType(), mEffectParam() {
    lumacore::setClass(this, lumacore::kClassId< AudioEffectArea >);
}

void AudioEffectArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);

    mEffectType = mObjArg0;
    mEffectParam = mObjArg1;

    if (mObjArg1 < 0) {
        mEffectParam = 64;
    }
}

void AudioEffectArea::movement() {
    bool isInside;

    switch (mEffectType) {
    case 3:
    case 4:
        isInside = isInVolume(MR::getCamPos()) || isInVolume(*MR::getPlayerPos());
        break;
    default:
        isInside = isInVolume(*MR::getPlayerPos());
        break;
    }

    if (isInside) {
        MR::setAudioEffectType(mEffectType, mEffectParam);
    } else {
        MR::setAudioEffectType(0, 0);
    }
}
