#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/AudioLib/AudMeNameConverter.hpp"
#include "Game/AudioLib/AudMicWrap.hpp"
#include "Game/AudioLib/AudRemixMgr.hpp"
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudSoundNameConverter.hpp"
#include "Game/AudioLib/AudSpeakerWrap.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/AudioLib/CSSoundNameConverter.hpp"
#include "Game/GameAudio/AudBgmConductor.hpp"
#include "Game/GameAudio/AudEffectDirector.hpp"
#include "Game/GameAudio/AudSeKeeper.hpp"
#include "Game/GameAudio/AudStageBgmWrap.hpp"
#include "Game/GameAudio/AudTalkSoundData.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/RhythmLib/AudMeObject.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Sound/Sound.hpp"

namespace {
    AudBgmConductor* getAudBgmConductor() {
        return MR::getSceneObj< AudBgmConductor >(SceneObj_AudBgmConductor);
    }
};  // namespace

namespace MR {
    SoundHandle* startSystemSE(SoundID id, s32 level, s32 subLevel) {
        return AudWrap::getSystemSeObject()->startSoundParam(id, level, subLevel);
    }

    SoundHandle* startSystemLevelSE(const char* pName, s32 level, s32 subLevel) {
        return AudWrap::getSystemSeObject()->startLevelSoundParam(pName, level, subLevel);
    }

    SoundHandle* startSystemLevelSE(SoundID id, s32 level, s32 subLevel) {
        return AudWrap::getSystemSeObject()->startLevelSoundParam(id, level, subLevel);
    }

    void stopSystemSE(const char* pName, u32 fadeTime) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        stopSystemSE(id, fadeTime);
    }

    void stopSystemSE(SoundID id, u32 fadeTime) {
        AudWrap::getSystemSeObject()->stopSound(id, fadeTime);
    }

    SoundHandle* startAtmosphereSE(const char* pName, s32 level, s32 subLevel) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        return AudWrap::getAtmosphereSeObject()->startSoundParam(id, level, subLevel);
    }

    SoundHandle* startAtmosphereLevelSE(const char* pName, s32 level, s32 subLevel) {
        return AudWrap::getAtmosphereSeObject()->startLevelSoundParam(pName, level, subLevel);
    }

    SoundHandle* startSoundObject(AudSoundObject* pSoundObject, const char* pName) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        return pSoundObject->startSound(id);
    }

    SoundHandle* startSoundObjectLevel(AudSoundObject* pSoundObject, const char* pName, s32 lifeTime) {
        SoundHandle* pSoundHandle = pSoundObject->startLevelSound(pName);

        if (lifeTime > 0 && pSoundHandle != nullptr) {
            (*pSoundHandle)->updateLifeTime(lifeTime);
        }

        return pSoundHandle;
    }

    SoundHandle* startSoundObjectLevel(AudSoundObject* pSoundObject, SoundID id, s32 lifeTime) {
        SoundHandle* pSoundHandle = pSoundObject->startLevelSound(id);

        if (lifeTime > 0 && pSoundHandle != nullptr) {
            (*pSoundHandle)->updateLifeTime(lifeTime);
        }

        return pSoundHandle;
    }

    SoundHandle* startSoundObjectLevelParam(AudSoundObject* pSoundObject, const char* pName, s32 level, s32 subLevel, s32 lifeTime) {
        SoundHandle* pSoundHandle = pSoundObject->startLevelSoundParam(pName, level, subLevel);

        if (lifeTime > 0 && pSoundHandle != nullptr) {
            (*pSoundHandle)->updateLifeTime(lifeTime);
        }

        return pSoundHandle;
    }

    SoundHandle* startSoundObjectLevelParam(AudSoundObject* pSoundObject, SoundID id, s32 level, s32 subLevel, s32 lifeTime) {
        SoundHandle* pSoundHandle = pSoundObject->startLevelSoundParam(id, level, subLevel);

        if (lifeTime > 0 && pSoundHandle != nullptr) {
            (*pSoundHandle)->updateLifeTime(lifeTime);
        }

        return pSoundHandle;
    }

    SoundHandle* startSound(const LiveActor* pActor, const char* pName, s32 level, s32 subLevel) {
        pActor->mSoundObject->setMapCode(getMapSoundCodeFoot(pActor));

        if (level != -1) {
            AudAnmSoundObject* pSound = pActor->mSoundObject;

            return pSound->startSoundParam(static_cast< u32 >(AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName)), level, subLevel);
        } else {
            AudAnmSoundObject* pSound = pActor->mSoundObject;

            return pSound->startSound(static_cast< u32 >(AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName)));
        }
    }

    SoundHandle* startSound(const LiveActor* pActor, SoundID id, s32 level, s32 subLevel) {
        pActor->mSoundObject->setMapCode(getMapSoundCodeFoot(pActor));

        if (level != -1) {
            return pActor->mSoundObject->startSoundParam(id, level, subLevel);
        } else {
            return pActor->mSoundObject->startSound(id);
        }
    }

    SoundHandle* startSoundSeVer(const LiveActor* pActor, const char* pName, s32 level, s32 subLevel) {
        return startSound(pActor,
                          pActor->mSoundObject->convertSoundIdFromSeVersion(
                              static_cast< u32 >(AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName))),
                          level, subLevel);
    }

    SoundHandle* startLevelSound(const LiveActor* pActor, const char* pName, s32 level, s32 subLevel, s32 lifeTime) {
        pActor->mSoundObject->setMapCode(getMapSoundCodeFoot(pActor));

        if (level != -1) {
            return startSoundObjectLevelParam(pActor->mSoundObject, pName, level, subLevel, lifeTime);
        } else {
            return startSoundObjectLevel(pActor->mSoundObject, pName, lifeTime);
        }
    }

    SoundHandle* startLevelSound(const LiveActor* pActor, SoundID id, s32 level, s32 subLevel, s32 lifeTime) {
        pActor->mSoundObject->setMapCode(getMapSoundCodeFoot(pActor));

        if (level != -1) {
            return startSoundObjectLevelParam(pActor->mSoundObject, id, level, subLevel, lifeTime);
        } else {
            return startSoundObjectLevel(pActor->mSoundObject, id, lifeTime);
        }
    }

    SoundHandle* startLevelSoundSeVer(const LiveActor* pActor, const char* pName, s32 level, s32 subLevel, s32 lifeTime) {
        return startLevelSound(pActor,
                               pActor->mSoundObject->convertSoundIdFromSeVersion(
                                   static_cast< u32 >(AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName))),
                               level, subLevel, lifeTime);
    }

    void stopSound(const LiveActor* pActor, const char* pName, u32 fadeTime) {
        AudAnmSoundObject* pSound = pActor->mSoundObject;

        pSound->stopSound(static_cast< u32 >(AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName)), fadeTime);
    }

    void stopSound(const LiveActor* pActor, SoundID id, u32 fadeTime) {
        pActor->mSoundObject->stopSound(id, fadeTime);
    }

    void setSeVersion(const LiveActor* pActor, u32 version) {
        pActor->mSoundObject->mSeVersion = version;
    }

    void releaseSoundHandle(const LiveActor* pActor, const char* pName) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        pActor->mSoundObject->releaseHandle(id);
    }

    void startBas(const LiveActor* pActor, const char* pName, bool reversed, f32 loopStartFrame, f32 loopEndFrame) {
        if (pActor->mSoundObject == nullptr) {
            return;
        }

        ResourceHolder* pResourceHolder = getResourceHolder(pActor);
        SoundAnimation* pSoundAnimation = nullptr;

        pSoundAnimation = static_cast< SoundAnimation* >(pResourceHolder->findSoundAnimation(pName));

        pActor->mSoundObject->setMapCode(getMapSoundCodeFoot(pActor));
        pActor->mSoundObject->startAnimation(pSoundAnimation, reversed, loopStartFrame, loopEndFrame);
    }

    void actorSoundMovement(LiveActor* pActor) {
        AudAnmSoundObject* pSoundObject = pActor->mSoundObject;

        if (pSoundObject == nullptr) {
            return;
        }

        pSoundObject->setMapCode(getMapSoundCodeFoot(pActor));

        if (pActor->mModelManager != nullptr) {
            ResourceHolder* pResourceHolder = getResourceHolder(pActor);

            if (pResourceHolder->mMotionResTable->mCount != 0) {
                f32 frame = getBckCtrl(pActor)->mFrame;

                pSoundObject->update(frame);
            }
        }

        pSoundObject->process();
    }

    void addJointToSeKeeper(AudSeKeeper* pSeKeeper, const char* pJointName, const char* pJointName2) {
        pSeKeeper->makeBox(pJointName, pJointName2);
    }

    void startLevelSoundSeKeeper(AudSeKeeper* pSeKeeper, const char* pJointName, const char* pSoundName) {
        pSeKeeper->playLevel(pJointName, pSoundName);
    }

    bool hasME() {
        return AudWrap::getSystem()->getChordInfo()->isAvailable();
    }

    void startSystemME(const char* pName) {
        if (!hasME()) {
            return;
        }

        u32 id = AudSingletonHolder< AudMeNameConverter >::get()->getMeID(pName);

        AudWrap::getSystemMeObject()->startMe(id);
    }

    s32 getMapSoundCodeFoot(const LiveActor* pActor) {
        const Binder* pBinder = pActor->mBinder;
        if (pBinder == nullptr) {
            return -1;
        }

        return getSoundCodeIndex(pBinder->mGroundInfo.mParentTriangle.getAttributes());
    }

    void setMapSondCodeGravity(const LiveActor* pActor, s32 code) {
        if (pActor->mSoundObject == nullptr) {
            return;
        }

        pActor->mSoundObject->setMapCodeExtra(code);
    }

    void startTalkSound(u8 soundNo, const LiveActor* pActor) {
        SoundID id = AudTalkSoundData::getSoundIDFromTalkSoundNo(soundNo);
        if (!id.isAnonymous()) {
            if (pActor == nullptr) {
                startSystemSE(id);
            } else {
                startSound(pActor, id);
            }
        }
    }

    // beatLength: player travel per beat; the tempo follows the player's speed (3 at least).
    void startRemixSound(s32 melodyNo, s32 noteNo, f32 beatLength) {
        RemixNoteGroupData* pRemixNoteGroupData = AudWrap::getRemixMgr()->getRemixNoteGroupDataFromMelodyNo(melodyNo);

        // A melody the remix data does not hold, a note past its end, or a rail with no length: no sound.
        if (pRemixNoteGroupData == nullptr || noteNo < 0 || noteNo >= pRemixNoteGroupData->mNoteCount || !(beatLength > 0.0f)) {
            return;
        }

        AudRemixSequencer* pRemixSequencer = AudWrap::getRemixSequencer();
        f32 playerSpeed = getPlayerVelocity()->length();

        if (playerSpeed < 3.0f) {
            playerSpeed = 3.0f;
        }

        pRemixSequencer->setTempo((playerSpeed * 3600.0f) / beatLength);

        for (s32 i = 0; i < pRemixNoteGroupData->mTrackCount; i++) {
            RemixNoteTrackData* pRemixNoteTrackData = &pRemixNoteGroupData->mRemixTracks[i];

            pRemixSequencer->addNoteData(pRemixNoteTrackData, &pRemixNoteTrackData->mNotes[noteNo]);
        }
    }

    // A melody the remix data does not hold counts one note (callers divide by it).
    s32 getRemixMelodyNoteNum(s32 melodyNo) {
        const RemixNoteGroupData* pRemixNoteGroupData = AudWrap::getRemixMgr()->getRemixNoteGroupDataFromMelodyNo(melodyNo);

        return pRemixNoteGroupData != nullptr && pRemixNoteGroupData->mNoteCount > 0 ? pRemixNoteGroupData->mNoteCount : 1;
    }

    void limitedSound(const char* pName, s32 delay) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        AudWrap::getSystem()->registerLimitedSound(id, delay);
    }

    SoundHandle* startStageBGM(const char* pName, bool lock) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        return AudWrap::startStageBgm(id, lock);
    }

    SoundHandle* startStageBGMFromStageName(const char* pSceneName, const char* pStageName, s32 scenarioNo) {
        SoundID id = AudStageBgmWrap::changeStageNameToSoundID(pSceneName, pStageName, scenarioNo);

        if (id.isAnonymous()) {
            return nullptr;
        }

        return AudWrap::startStageBgm(id, false);
    }

    void stopStageBGM(u32 fadeTime) {
        AudBgm* pStageBgm = AudWrap::getStageBgm();

        if (pStageBgm != nullptr) {
            pStageBgm->stop(fadeTime);
        }
    }

    void unlockStageBGM() {
        AudBgm* pStageBgm = AudWrap::getStageBgm();

        if (pStageBgm != nullptr) {
            pStageBgm->playAfterPrepared();
        }
    }

    void setNextStageBGM(const char* pName) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        AudWrap::setNextIdStageBgm(id);
    }

    void moveVolumeStageBGM(f32 volume, u32 fadeTime) {
        AudBgm* pStageBgm = AudWrap::getStageBgm();

        if (pStageBgm != nullptr) {
            pStageBgm->moveVolume(volume, fadeTime);
        }
    }

    void moveVolumeStageBGMForNoteFairy(f32 volume, u32 fadeTime) {
        AudBgm* pStageBgm = AudWrap::getStageBgm();

        if (pStageBgm != nullptr) {
            pStageBgm->moveVolumeForNoteFairy(volume, fadeTime);
        }
    }

    SoundHandle* startLastStageBGM() {
        return AudWrap::startLastStageBgm();
    }

    bool isPlayingStageBgm() {
        AudBgm* pStageBgm = AudWrap::getStageBgm();

        if (pStageBgm != nullptr) {
            return !pStageBgm->isStopping();
        }

        return false;
    }

    bool isPlayingStageBgmID(u32 id) {
        AudBgm* pStageBgm = AudWrap::getStageBgm();

        if (pStageBgm != nullptr && !pStageBgm->isStopping() && pStageBgm->getSoundID() == id) {
            return true;
        }

        return false;
    }

    bool isPlayingStageBgmName(const char* pName) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        return isPlayingStageBgmID(id);
    }

    bool isStopOrFadeoutStageBgmID(u32 id) {
        AudBgm* pBgm = AudWrap::getStageBgm();
        if (pBgm == nullptr) {
            return true;
        }

        if (pBgm != nullptr) {
            if (id != pBgm->getSoundID()) {
                return true;
            }

            SoundHandle* pHandle = pBgm->getHandle();
            if (pHandle == nullptr) {
                return true;
            }

            if (pHandle->getSound()->getFader()->mTransition.mRemainingSteps != 0) {
                return true;
            }

            return pBgm->isStopping();
        }

        return false;
    }

    bool isStopOrFadeoutBgmName(const char* pName) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        return isStopOrFadeoutStageBgmID(id);
    }

    bool isPreparedStageBgm() {
        AudBgm* pStageBgm = AudWrap::getStageBgm();

        if (pStageBgm != nullptr) {
            return pStageBgm->isPreparedPlay();
        }

        return false;
    }

    void setStageBGMState(s32 state, u32 time) {
        AudBgm* pStageBgm = AudWrap::getStageBgm();

        if (pStageBgm != nullptr) {
            pStageBgm->changeTrackMuteState(state, time);
        }
    }

    void setStageBGMStateBit(u32 bit) {
        AudBgmConductor* pBgmConductor = ::getAudBgmConductor();

        if (pBgmConductor != nullptr) {
            pBgmConductor->setStateBit(bit);
        }
    }

    SoundHandle* startSubBGM(const char* pName, bool lock) {
        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        return AudWrap::startSubBgm(id, lock);
    }

    void stopSubBGM(u32 fadeTime) {
        AudBgm* pSubBgm = AudWrap::getSubBgm();

        if (pSubBgm != nullptr) {
            pSubBgm->stop(fadeTime);
        }
    }

    void moveVolumeSubBGM(f32 volume, u32 fadeTime) {
        AudBgm* pSubBgm = AudWrap::getSubBgm();

        if (pSubBgm != nullptr) {
            pSubBgm->moveVolume(volume, fadeTime);
        }
    }

    bool isPlayingSubBgmID(u32 id) {
        AudBgm* pSubBgm = AudWrap::getSubBgm();

        if (pSubBgm != nullptr && !pSubBgm->isStopping() && pSubBgm->getSoundID() == id) {
            return true;
        }

        return false;
    }

    void playLevelMarioPinchBGM(bool secondPinch) {
        if (isPlayerDead()) {
            return;
        }

        AudBgmConductor* pBgmConductor = ::getAudBgmConductor();

        if (pBgmConductor != nullptr) {
            pBgmConductor->playPinchEventBgmLevel(secondPinch);
        }
    }

    void stopMarioPinchBGMSoon() {
        AudBgmConductor* pBgmConductor = ::getAudBgmConductor();

        if (pBgmConductor != nullptr) {
            pBgmConductor->stopPinchEventBgmSoon();
        }
    }

    void submitTrigSE() {
        AudSystem* pSystem = AudWrap::getSystem();

        pSystem->mIsTrigSeBlocked = 1;
    }

    void permitTrigSE() {
        AudSystem* pSystem = AudWrap::getSystem();

        pSystem->mIsTrigSeBlocked = 0;
    }

    void submitLevelSE() {
        AudSystem* pSystem = AudWrap::getSystem();

        pSystem->mIsLevelSeBlocked = 1;
    }

    void permitLevelSE() {
        AudSystem* pSystem = AudWrap::getSystem();

        pSystem->mIsLevelSeBlocked = 0;
    }

    void submitSE() {
        submitTrigSE();
        submitLevelSE();
    }

    void permitSE() {
        permitTrigSE();
        permitLevelSE();
    }

    bool isPermitSE() {
        return !AudWrap::getSystem()->mIsTrigSeBlocked && !AudWrap::getSystem()->mIsLevelSeBlocked;
    }

    void setCubeBgmChangeInvalid() {
        AudSceneMgr* pSceneMgr = AudWrap::getSceneMgr();

        pSceneMgr->mIsCubeBgmChangeInvalid = 1;
    }

    bool isCubeBgmChangeInvalid() {
        return AudWrap::getSceneMgr()->mIsCubeBgmChangeInvalid;
    }

    void clearBgmQueue() {
        AudWrap::getBgmMgr()->clearLastBGM(0);
        AudWrap::getBgmMgr()->clearLastBGM(1);
        AudWrap::getBgmMgr()->clearNextBGM(0);
        AudWrap::getBgmMgr()->clearNextBGM(1);
    }

    void setMicMtx(MtxPtr pMtx, s32 mic) {
        AudMicWrap::setMicMtx(pMtx, mic);
    }

    void setSoundVolumeSetting(s32 volumeSet, u32 maxSteps) {
        AudWrap::getSystem()->setSeVolumeSet(volumeSet, maxSteps);
    }

    void recoverSoundVolumeSetting(u32 maxSteps) {
        AudWrap::getSystem()->recoverSeVolumeSet(maxSteps);
    }

    void setAudioEffectType(s32 effectType, s32 priority) {
        MR::getSceneObj< AudEffectDirector >(SceneObj_AudEffectDirector)->setEffectType(effectType, priority);
    }

    void startCSSound(const char* pCSName, const char* pSEName, s32 padChannel) {
        if (AudSpeakerWrap::isPlayable(-1)) {
            s32 id = AudSingletonHolder< CSSoundNameConverter >::get()->getSoundID(pCSName);

            if (id != -1) {
                AudSpeakerWrap::start(id, padChannel);
            }
        } else if (pSEName != nullptr) {
            startSystemSE(pSEName);
        }
    }

    void startCSSound2P(const char* pCSName, const char* pSEName) {
        if (isConnectedInput(1)) {
            startCSSound(pCSName, pSEName, 1);
        }
    }

    SoundHandle* startSystemSE(const char* pName, s32 level, s32 subLevel) {
        if (MR::isEqualString(pName, "SE_SY_READ_RIDDLE_S") && MR::isPlayerDead()) {
            return nullptr;
        }

        SoundID id = AudSingletonHolder< AudSoundNameConverter >::get()->getSoundID(pName);

        return startSystemSE(id, level, subLevel);
    }

    void startSpinHitSound(const LiveActor* /*pActor*/) {
        startCSSound("CS_SPIN_HIT", nullptr, 0);
    }

    void startBlowHitSound(const LiveActor* pActor) {
        startSound(pActor, "SE_PM_SPIN_HIT");
    }

    void startPointerHitSound() {
        if (hasME()) {
            startSystemME("ME_DPD_HIT");
        } else {
            startSystemSE("SE_SY_DPD_HIT");
        }

        startCSSound2P("CS_DPD_HIT", nullptr);
    }

    void startPointerFreezeLevelSound(const LiveActor* pActor) {
        startLevelSound(pActor, "SE_EM_LV_DPD_POINT");
    }

    void start2PJumpAssistSound() {
        if (hasME()) {
            startSystemME("ME_2P_ASSIST_JUMP");
        } else {
            startSystemSE("SE_SY_2P_ASSIST_JUMP");
        }

        startCSSound2P("CS_DPD_JUMP", nullptr);
    }

    void start2PJumpAssistJustSound() {
        if (hasME()) {
            startSystemME("ME_2P_ASSIST_JUMP_L");
        } else {
            startSystemSE("SE_SY_2P_ASSIST_JUMP_L");
        }

        startCSSound2P("CS_DPD_JUMP_HIGH", nullptr);
    }

    void start2PAttackAssistSound() {
        if (hasME()) {
            startSystemME("ME_2P_ASSIST_ATTACK");
        } else {
            startSystemSE("SE_SY_2P_ASSIST_ATTACK");
        }

        startCSSound2P("CS_DPD_PUNCH", nullptr);
    }

    void limitedStarPieceHitSound() {
        limitedSound("SE_OJ_STAR_PIECE_HIT_ENEMY", 1);
    }

    void startCurrentStageBGM() {
        startStageBGMFromStageName("Game", getCurrentStageName(), getCurrentScenarioNo());
    }

    SoundHandle* startBossBGM(s32 id) {
        SoundHandle* pSoundHandle = nullptr;

        switch (id) {
        case BossBgmID_01_A:
            pSoundHandle = startStageBGM("MBGM_BOSS_01_A", 0);
            break;
        case BossBgmID_01_B:
            pSoundHandle = startStageBGM("MBGM_BOSS_01_B", 0);
            break;
        case BossBgmID_02_A:
            pSoundHandle = startStageBGM("MBGM_BOSS_02_A", 0);
            break;
        case BossBgmID_02_B:
            pSoundHandle = startStageBGM("MBGM_BOSS_02_B", 0);
            break;
        case BossBgmID_03_A:
            pSoundHandle = startStageBGM("MBGM_BOSS_03_A", 0);
            break;
        case BossBgmID_03_B:
            pSoundHandle = startStageBGM("MBGM_BOSS_03_B", 0);
            break;
        case BossBgmID_05_A:
            pSoundHandle = startStageBGM("MBGM_BOSS_05_A", 0);
            break;
        case BossBgmID_05_B:
            pSoundHandle = startStageBGM("MBGM_BOSS_05_B", 0);
            break;
        }

        return pSoundHandle;
    }

    SoundHandle* startEventBGM(s32 id) {
        SoundHandle* pSoundHandle = nullptr;

        switch (id) {
        case EventBgmID_Hurry:
            pSoundHandle = startStageBGM("MBGM_GALAXY_02_HURRY", 0);
            break;
        case EventBgmID_Chase:
            pSoundHandle = startStageBGM("MBGM_GALAXY_02_CHASE", 0);
            break;
        case EventBgmID_Race_01:
            pSoundHandle = startStageBGM("MBGM_RACE_01", 0);
            break;
        case EventBgmID_Race_02:
            pSoundHandle = startStageBGM("MBGM_RACE_02", 0);
            break;
        }

        return pSoundHandle;
    }

    SoundHandle* startAfterBossBGM() {
        setCubeBgmChangeInvalid();
        clearBgmQueue();

        if (isPlayerDead()) {
            return nullptr;
        }

        if (!isGalaxyDarkCometAppearInCurrentStage()) {
            SoundID id = AudStageBgmWrap::getCometEventBgm(getCurrentStageName());

            if (!id.isAnonymous()) {
                return AudWrap::startStageBgm(id, false);
            }
        }

        return startStageBGM("MBGM_STAR_EXIST", 0);
    }
};  // namespace MR
