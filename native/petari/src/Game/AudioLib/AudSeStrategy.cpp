#include "Game/AudioLib/AudSeStrategy.hpp"
#include "Game/AudioLib/AudEffector.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/AudioLib/AudSoundInfo.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Sound/PlayerLink.hpp"
#include "Game/Sound/EffectSound.hpp"

void AudSeStrategy::calc(EffectSound* pSe) {
    SoundID soundID = pSe->mSoundID;

    AudSoundInfo* info = AudWrap::getSoundInfo();
    if (info->isUseFxMix(soundID)) {
        pSe->getAuxiliary().moveFxMix(AudWrap::getSystem()->mAudEffector->mFxSend, 0);
    }

    if (!info->isUseLpf(soundID)) {
        return;
    }

    AudEffector* effector = AudWrap::getSystem()->mAudEffector;
    s32 cutoff = effector->mCutoff;
    if (cutoff < 0 || cutoff > 0x7F) {
        cutoff = 0x7F;
    }

    if (soundID.getGroupID() == AudGroup_VoiceSe && cutoff < 50) {
        cutoff = 50;
    }

    switch (soundID) {
    case SE_AT_LV_WATERFALL_S:
    case SE_AT_LV_WATERFALL_L:
    case SE_AT_LV_WATERFALL_UPPER_S:
    case SE_AT_LV_WATERFALL_UPPER_L:
    case SE_OJ_SEAGULL_CHIRP: {
        if (effector->mSpecialFx == 2) {
            cutoff = 10;
            pSe->getAuxiliary().moveVolume(0.5f, 0);
        }
        break;
    }
    case SE_AT_LV_WIND_RABBIT_MAZE:
    case SE_OJ_LV_FOUNTAIN: {
        if (effector->mSpecialFx == 1) {
            cutoff = 20;
            pSe->getAuxiliary().moveVolume(0.5f, 5);
        } else {
            cutoff = 0x7F;
            pSe->getAuxiliary().moveVolume(1.0f, 5);
        }
        break;
    }
    }

    PlayerLink* player = pSe->getPlayer();
    if (player != nullptr) {
        player->setChildIIR(cutoff);
    }
}
