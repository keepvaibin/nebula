#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudEffector.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Sound/SoundArchive.hpp"

namespace {
    // The groups of the voice bank, a bit each.
    const u32 cMarioVoices = 2;
    const u32 cLuigiVoices = 4;
}  // namespace

AudSceneMgr::AudSceneMgr(SoundArchive* pAudioData) : mAudioData(pAudioData), _4(), mPlayerMode(), _1D() {
}

void AudSceneMgr::startScene() {
    _4 = 0;
    AudWrap::getSystem()->_82A = false;
    AudWrap::getSystem()->_82B = false;
    AudWrap::getSystem()->_82C = false;
    AudWrap::getSystem()->initSceneVolume();
    AudEffector* effector = AudWrap::getSystem()->mAudEffector;
    if (effector != nullptr) {
        effector->initParams();
    }

    _1D = false;
}

void AudSceneMgr::setPlayerLuigi(bool isLuigi) {
    const s32 mode = isLuigi ? PlayerMode_Luigi : PlayerMode_Mario;
    if (mode == mPlayerMode) {
        return;
    }
    mAudioData->setWaveGroups(cPlayerVoiceBank, cMarioVoices | cLuigiVoices, false);
    mAudioData->setWaveGroups(cPlayerVoiceBank, isLuigi ? cLuigiVoices : cMarioVoices, true);
    mPlayerMode = mode;
}
