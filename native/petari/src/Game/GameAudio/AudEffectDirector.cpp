#include "Game/GameAudio/AudEffectDirector.hpp"
#include "Game/AudioLib/AudEffector.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

struct AudEffectData {
    f32 mFxSend;
    s32 mCutoff;
    f32 mVolume;
    u32 mFadeFrames;
};

namespace {
    const AudEffectData cAudEffectDataList[] = {
        {0.0f, 127, 1.0f, 90}, {1.0f, 127, 1.0f, 90}, {0.0f, 10, 1.0f, 90}, {1.0f, 127, 0.0f, 90}, {0.8f, 127, 1.0f, 90},
    };
};  // namespace

AudEffectDirector::AudEffectDirector() : NameObj("オーディオエフェクトディレクター"), mEffectType(0), mAppliedType(0), mPriority(0) {
    lumacore::setClass(this, lumacore::kClassId< AudEffectDirector >);
}

void AudEffectDirector::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_AudEffectDirector, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void AudEffectDirector::movement() {
    if (mAppliedType != mEffectType) {
        setAudioEffectParam(mEffectType);
        mAppliedType = mEffectType;
    }

    playEffectSound(mEffectType);
    mPriority = 0;
}

void AudEffectDirector::setEffectType(s32 effectType, s32 priority) {
    if (priority < 0) {
        priority = 0;
    }

    if (priority > 127) {
        priority = 127;
    }

    if (priority >= mPriority) {
        mEffectType = effectType;
        mPriority = priority;
    }
}

void AudEffectDirector::setAudioEffectParam(s32 effectType) {
    AudEffector* pAudEffector = AudWrap::getSystem()->getAudEffector();

    pAudEffector->setFxSend(::cAudEffectDataList[effectType].mFxSend);
    pAudEffector->setCutoff(::cAudEffectDataList[effectType].mCutoff);

    if (!MR::isStageStateScenarioOpeningCamera()) {
        MR::moveVolumeStageBGM(::cAudEffectDataList[effectType].mVolume, ::cAudEffectDataList[effectType].mFadeFrames);
    }

    switch (effectType) {
    case 1:
        pAudEffector->setSpecialFx(1);
        break;
    case 3:
        pAudEffector->setSpecialFx(2);
        break;
    default:
        pAudEffector->setSpecialFx(0);
        break;
    }
}

void AudEffectDirector::playEffectSound(s32 effectType) {
    switch (effectType) {
    case 3:
        MR::startAtmosphereLevelSE("SE_AT_LV_WATERFALL_CAVE");
        break;
    case 4:
        MR::startAtmosphereLevelSE("SE_AT_LV_PHNCV_TERESA_ROOM");
        break;
    }
}
