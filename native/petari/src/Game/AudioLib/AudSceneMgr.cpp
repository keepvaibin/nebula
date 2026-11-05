#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudEffector.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include <JSystem/JAudio2/JAUAudioData.hpp>

namespace {
    // The groups of the voice bank, a bit each.
    const u32 cMarioVoices = 2;
    const u32 cLuigiVoices = 4;
}  // namespace

AudSceneMgr::AudSceneMgr(JAUAudioData* pAudioData) : mAudioData(pAudioData), _4(), mPlayerMode(), _1D() {
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
    SpkSystem::reconnect(-1);
}

void AudSceneMgr::setPlayerLuigi(bool isLuigi) {
    const s32 mode = isLuigi ? PlayerMode_Luigi : PlayerMode_Mario;
    if (mode == mPlayerMode) {
        return;
    }
    mAudioData->eraseWaveArc(cPlayerVoiceBank, cMarioVoices | cLuigiVoices);
    mAudioData->loadWaveArc(cPlayerVoiceBank, isLuigi ? cLuigiVoices : cMarioVoices);
    mPlayerMode = mode;
}
