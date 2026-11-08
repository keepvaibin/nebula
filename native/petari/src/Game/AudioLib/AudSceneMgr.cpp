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

AudSceneMgr::AudSceneMgr(SoundArchive* pAudioData) : mAudioData(pAudioData), mPlayerMode(), mIsCubeBgmChangeInvalid() {
}

void AudSceneMgr::startScene() {
    AudWrap::getSystem()->mIsTrigSeBlocked = false;
    AudWrap::getSystem()->mIsLevelSeBlocked = false;
    AudWrap::getSystem()->initSceneVolume();
    AudEffector* effector = AudWrap::getSystem()->mAudEffector;
    if (effector != nullptr) {
        effector->initParams();
    }

    mIsCubeBgmChangeInvalid = false;
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
