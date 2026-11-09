#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/AudioLib/AudEffector.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/AudioLib/AudSoundInfo.hpp"
#include "Game/AudioLib/AudSoundNameConverter.hpp"
#include "Game/AudioLib/AudSoundObjHolder.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Sound/Sound.hpp"

namespace {
    // Sequence ports the player foot and land sounds and the low-pass filter read.
    const u32 cPortLpfCutoff = 10;
    const u32 cPortMapCodeOffset = 11;
    const u32 cPortMapCodeBank = 12;
    // Map codes come in banks of seven; each code within a bank is twelve steps apart.
    const s32 cMapCodeBankSize = 7;
    const s32 cMapCodeStep = 12;
    const s32 cLpfOpen = 0x7F;
    // Special effect cutoffs: waterfalls and gulls under effect 2, fountains and the maze wind under effect 1.
    const s32 cLpfCutoffSpecialFx2 = 10;
    const s32 cLpfCutoffSpecialFx1 = 20;
}  // namespace

AudSoundObject::AudSoundObject(Vec3f* pPos, u8 numHandles, NebulaArena* pArena)
    : SoundObject(pPos, numHandles, pArena), ArenaOwned(), mSeVersion(0), mMapCode(0), mMapCodeExtra(0) {
}

AudSoundObject::~AudSoundObject() {
    AudWrap::getSoundObjHolder()->remove(this);
}

void AudSoundObject::addToSoundObjHolder() {
    AudWrap::getSoundObjHolder()->add(this);
}

void AudSoundObject::setTrans(Vec3f* pTrans) {
    mPos = pTrans;
}

bool AudSoundObject::writePort(SoundHandle* pHandle, u32 port, u16 value) {
    if (pHandle == nullptr) {
        return false;
    }

    if (!pHandle->isSoundAttached()) {
        return false;
    }

    PlayerLink* player = pHandle->getSound()->getPlayer();
    if (player != nullptr) {
        player->writePort(PlayerLink::root(), port, value);
        return true;
    }
    return false;
}

bool AudSoundObject::isLimitedSound(SoundID soundID) {
    if (soundID == -1) {
        return true;
    }

    if (AudWrap::getSystem()->isRegisteredLimitedSound(soundID)) {
        return true;
    }

    if (modifyLimitedSound(soundID)) {
        return true;
    }

    if (!isPlayingID(soundID)) {
        AudSoundInfo* info = AudWrap::getSoundInfo();
        if (info->isLimitCountPlaying(soundID)) {
            return true;
        }

        if (info->isLimitNumPlaying(soundID)) {
            return true;
        }
    }

    return false;
}

SoundHandle* AudSoundObject::startSound(SoundID soundID) {
    if (!isEnableStartSound(soundID)) {
        return nullptr;
    }

    if (soundID == -1) {
        return nullptr;
    }

    if (AudWrap::getSystem()->mIsTrigSeBlocked && soundID.getGroupID() != SE_HOME_BUTTON_MENU && soundID.getGroupID() != SE_SYSTEM) {
        return nullptr;
    }

    if (isLimitedSound(soundID)) {
        return nullptr;
    }

    limitVoiceOne(soundID);
    SoundHandle* handle = SoundObject::startSound(soundID);
    setMapCodeToPort(handle, soundID);
    setCutoffToPort(handle, soundID);
    return handle;
}

SoundHandle* AudSoundObject::startLevelSound(SoundID soundID) {
    if (!isEnableStartSound(soundID)) {
        return nullptr;
    }

    if (soundID == -1) {
        return nullptr;
    }

    if (AudWrap::getSystem()->mIsLevelSeBlocked && soundID.getGroupID() != SE_HOME_BUTTON_MENU && soundID.getGroupID() != SE_SYSTEM) {
        return nullptr;
    }

    if (isLimitedSound(soundID)) {
        return nullptr;
    }

    SoundHandle* handle = SoundObject::startLevelSound(soundID);
    setMapCodeToPort(handle, soundID);
    setCutoffToPort(handle, soundID);
    return handle;
}

SoundHandle* AudSoundObject::startLevelSound(const char* pName) {
    SoundID id = convertNameToLevelSEID(pName);
    return startLevelSound(id);
}

SoundHandle* AudSoundObject::startSoundParam(SoundID soundID, s32 level, s32 subLevel) {
    SoundHandle* handle = startSound(soundID);
    if (handle != nullptr && handle->isSoundAttached()) {
        modifySe(handle, level, subLevel);
    }

    return handle;
}

SoundHandle* AudSoundObject::startLevelSoundParam(SoundID soundID, s32 level, s32 subLevel) {
    SoundHandle* handle = startLevelSound(soundID);
    if (handle != nullptr && handle->isSoundAttached()) {
        modifySe(handle, level, subLevel);
    }

    return handle;
}

SoundHandle* AudSoundObject::startLevelSoundParam(const char* pName, s32 level, s32 subLevel) {
    SoundID id = convertNameToLevelSEID(pName);
    return startLevelSoundParam(id, level, subLevel);
}

bool AudSoundObject::isPlayingID(SoundID soundID) {
    return getHandleSoundID(soundID) != nullptr;
}

void AudSoundObject::releaseHandle(SoundID soundID) {
    SoundHandle* handle = getHandleSoundID(soundID);
    if (handle != nullptr) {
        handle->getSound()->releaseHandle();
    }
}

void AudSoundObject::setMapCode(s32 code) {
    if (code < 0) {
        mMapCode = 0;
    } else {
        mMapCode = code;
    }
}

void AudSoundObject::setMapCodeExtra(s32 code) {
    if (code == 0) {
        code = 1;
    }

    if (code < 0) {
        mMapCodeExtra = 0;
    } else {
        mMapCodeExtra = code;
    }
}

s32 AudSoundObject::getMapCode() const {
    if (mMapCodeExtra > 0) {
        return mMapCodeExtra;
    } else {
        return mMapCode;
    }
}

void AudSoundObject::clearMapCode() {
    mMapCode = 0;
    mMapCodeExtra = 0;
}

SoundID AudSoundObject::convertNameToLevelSEID(const char* pName) {
    return AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);
}

void AudSoundObject::limitVoiceOne(SoundID soundID) {
    u8 group = soundID.getGroupID();

    if (group == SE_ENEMY_VOICE) {
        stopCategorySound(SE_ENEMY_VOICE, 0);
        return;
    }

    if (group == SE_BOSS_VOICE) {
        stopCategorySound(SE_BOSS_VOICE, 0);
        return;
    }

    if (group == SE_PLAYER_VOICE) {
        stopCategorySound(SE_PLAYER_VOICE, 0);
        return;
    }

    if (group == SE_SUPPORTER_VOICE) {
        stopCategorySound(SE_SUPPORTER_VOICE, 0);
        return;
    }
}

void AudSoundObject::stopCategorySound(u32 groupID, u32 fadeTime) {
    for (s32 idx = 0; idx < SoundObject::mNumHandles; idx++) {
        Sound* sound = mHandles[idx].getSound();
        if (sound != nullptr) {
            SoundID id = sound->mSoundID;
            u32 group = id.getGroupID();
            if (group == groupID) {
                stopSound(id, fadeTime);
            }
        }
    }
}

void AudSoundObject::setMapCodeToPort(SoundHandle* pHandle, SoundID soundID) {
    if (pHandle == nullptr) {
        return;
    }

    if (!pHandle->isSoundAttached()) {
        return;
    }

    s32 mapCode = getMapCode();
    if (mapCode <= 0) {
        return;
    }

    // only specific soundIDs can modify based on mapcode
    switch (soundID) {
    case SE_PM_HIPDROP:
    case SE_PM_FALLDOWN_S:
    case SE_PM_FALLDOWN_M:
    case SE_PM_FOOTNOTE_L:
    case SE_PM_FOOTNOTE_R:
    case SE_PM_FOOTNOTE_SUB_L:
    case SE_PM_FOOTNOTE_SUB_R:
    case SE_PM_JUMP:
    case SE_PM_LAND:
    case SE_PM_HAND:
    case SE_PM_LAND_HEAVY:
        writePort(pHandle, ::cPortMapCodeBank, (mapCode - 1) / ::cMapCodeBankSize);
        writePort(pHandle, ::cPortMapCodeOffset, ((mapCode - 1) % ::cMapCodeBankSize) * ::cMapCodeStep);
        break;
    }
}

void AudSoundObject::setCutoffToPort(SoundHandle* pHandle, SoundID soundID) {
    if (pHandle == nullptr) {
        return;
    }

    if (!pHandle->isSoundAttached()) {
        return;
    }

    AudSoundInfo* info = AudWrap::getSoundInfo();
    if (info->isUseLpf(soundID)) {
        AudEffector* effector = AudWrap::getSystem()->mAudEffector;
        s32 value = effector->mCutoff;
        if (value < 0 || value > ::cLpfOpen) {
            value = ::cLpfOpen;
        }

        switch (soundID) {
        case SE_AT_LV_WATERFALL_S:
        case SE_AT_LV_WATERFALL_L:
        case SE_AT_LV_WATERFALL_UPPER_S:
        case SE_AT_LV_WATERFALL_UPPER_L:
        case SE_OJ_SEAGULL_CHIRP:
            if (effector->mSpecialFx == 2) {
                value = ::cLpfCutoffSpecialFx2;
                pHandle->getSound()->mParams.mMove.moveVolume(0.5f, 0);
            }
            break;
        case SE_OJ_LV_FOUNTAIN:
        case SE_AT_LV_WIND_RABBIT_MAZE:
            if (effector->mSpecialFx == 1) {
                value = ::cLpfCutoffSpecialFx1;
                pHandle->getSound()->mParams.mMove.moveVolume(0.5f, 5);
            } else {
                value = ::cLpfOpen;
                pHandle->getSound()->mParams.mMove.moveVolume(1.0f, 5);
            }
            break;
        }
        writePort(pHandle, ::cPortLpfCutoff, value);
    } else {
        writePort(pHandle, ::cPortLpfCutoff, ::cLpfOpen);
    }
}

SoundID AudSoundObject::convertSoundIdFromSeVersion(SoundID soundID) const {
    // Kuribo::SeVersion_2D and Metbo::SeVersion_2D; no other actor's sounds have a case below.
    if (mSeVersion == 1) {
        switch (soundID) {
        case SE_EM_KURIBO_LAND:
            return SE_EM_KURIBO_LAND_2D;
        case SE_EM_KURIBO_STEP1:
            return SE_EM_KURIBO_STEP1_2D;
        case SE_EM_KURIBO_STEP2:
            return SE_EM_KURIBO_STEP2_2D;
        case SE_EM_KURIBO_FIND:
            return SE_EM_KURIBO_FIND_2D;
        case SE_EM_LV_KURIBO_DASH1:
            return SE_EM_LV_KURIBO_DASH1_2D;
        case SE_EM_KURIBO_JUMP:
            return SE_EM_KURIBO_JUMP_2D;
        case SE_EM_KURIBO_SWOON_RECOVER:
            return SE_EM_KURIBO_SWOON_RECOVER_2D;
        case SE_EM_METBO_TRAMPLE:
            return SE_EM_METBO_TRAMPLE_2D;
        case SE_EM_METBO_SPIN_HIT:
            return SE_EM_METBO_SPIN_HIT_2D;
        case SE_EM_METBO_FIND:
            return SE_EM_METBO_FIND_2D;
        case SE_EM_LV_METBO_STAGGER:
            return SE_EM_LV_METBO_STAGGER_2D;
        case SE_EM_METBO_SWOON_RECOVER:
            return SE_EM_METBO_SWOON_RECOVER_2D;
        case SE_EM_LV_METBO_SWOON:
            return SE_EM_LV_METBO_SWOON_2D;
        case SE_EM_EXPLODE_S:
            return SE_EM_EXPLODE_S_2D;
        case SE_EM_LV_SWOON_S:
            return SE_EM_LV_SWOON_S_2D;
        }
    }

    return soundID;
}
