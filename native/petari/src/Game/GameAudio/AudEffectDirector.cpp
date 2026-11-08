#include "Game/GameAudio/AudEffectDirector.hpp"
#include "Game/AudioLib/AudEffector.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

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
}

void AudEffectDirector::init(const TableIter& rIter) {
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

void AudEffectDirector::setEffectType(s32 a1, s32 a2) {
    if (a2 < 0) {
        a2 = 0;
    }

    if (a2 > 127) {
        a2 = 127;
    }

    if (a2 >= mPriority) {
        mEffectType = a1;
        mPriority = a2;
    }
}

void AudEffectDirector::setAudioEffectParam(s32 a1) {
    AudEffector* pAudEffector = AudWrap::getSystem()->getAudEffector();

    pAudEffector->setFxSend(::cAudEffectDataList[a1].mFxSend);
    pAudEffector->setCutoff(::cAudEffectDataList[a1].mCutoff);

    if (!MR::isStageStateScenarioOpeningCamera()) {
        MR::moveVolumeStageBGM(::cAudEffectDataList[a1].mVolume, ::cAudEffectDataList[a1].mFadeFrames);
    }

    switch (a1) {
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

void AudEffectDirector::playEffectSound(s32 a1) {
    switch (a1) {
    case 3:
        MR::startAtmosphereLevelSE("SE_AT_LV_WATERFALL_CAVE");
        break;
    case 4:
        MR::startAtmosphereLevelSE("SE_AT_LV_PHNCV_TERESA_ROOM");
        break;
    }
}
