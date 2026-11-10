#include "Game/AreaObj/ChangeBgmCube.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/AudioLib/AudBgmMgr.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/GameAudio/AudStageBgmTable.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // Obj_arg0: what entering the cube does.
    enum Mode {
        Mode_Start,       // start stage BGM obj_arg1 (or the stage's own), with track state obj_arg2
        Mode_StopToNext,  // fade out over obj_arg1 frames into BGM obj_arg2
        Mode_TrackMute,   // track state obj_arg1 over obj_arg2 frames
    };
};  // namespace

ChangeBgmCube::ChangeBgmCube(int formType, const char* pName) : AreaObj(formType, pName), mIsInside() {
    lumacore::setClass(this, lumacore::kClassId< ChangeBgmCube >);
}

void ChangeBgmCube::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);
}

void ChangeBgmCube::movement() {
    if (!isValid()) {
        return;
    }

    if (MR::isCubeBgmChangeInvalid()) {
        return;
    }

    if (MR::isPowerStarGetDemoActive()) {
        mIsValid = false;
        return;
    }

    if (MR::isPlayingStageBgmID(STM_STAR_EXIST)) {
        mIsValid = false;
        return;
    }

    if (MR::isPlayingStageBgmID(STM_STAR_EXIST_2)) {
        mIsValid = false;
        return;
    }

    if (MR::isPlayerDead()) {
        mIsValid = false;
        return;
    }

    if (MR::isStageStateScenarioOpeningCamera()) {
        return;
    }

    if (MR::isPlayingStageBgmID(STM_GALAXY_02_CHASE)) {
        return;
    }

    if (MR::isPlayingStageBgmID(STM_BOSS_MECHA_KOOPA)) {
        return;
    }

    if (MR::isPlayingStageBgmID(STM_BOSS_01_A)) {
        return;
    }

    if (isInVolume(*MR::getPlayerPos())) {
        if (mIsInside) {
            return;
        }

        s32 objArg0 = mObjArg0;
        s32 objArg1 = mObjArg1;
        s32 objArg2 = mObjArg2;

        switch (objArg0) {
        case Mode_Start: {
            if (objArg1 < 0) {
                MR::startCurrentStageBGM();
                break;
            }

            SoundID bgmId = SoundID(AudStageBgmTable::getBgmId(MR::getCurrentStageName(), objArg1));

            if (!bgmId.isAnonymous()) {
                if (AudWrap::getBgmMgr()->mCurrentBGM[0] == bgmId) {
                    if (MR::isPlayingStageBgm()) {
                        break;
                    }
                }

                AudWrap::startStageBgm(bgmId, false);

                if (MR::isEqualStageName("ReverseKingdomGalaxy") && bgmId == MBGM_BOSS_05_A) {
                    MR::setCubeBgmChangeInvalid();
                }

                if (MR::isEqualStageName("CannonFleetGalaxy") && bgmId == MBGM_BOSS_01_A) {
                    MR::setCubeBgmChangeInvalid();
                }

                if (MR::isEqualStageName("BattleShipGalaxy") && bgmId == MBGM_BOSS_01_A) {
                    MR::setCubeBgmChangeInvalid();
                }
            }

            if (objArg2 >= 0) {
                s32 bgmState = AudStageBgmTable::getBgmState(MR::getCurrentStageName(), objArg2);

                if (bgmState >= 0) {
                    AudBgm* stageBgm = AudWrap::getStageBgm();

                    if (stageBgm != nullptr) {
                        stageBgm->changeTrackMuteState(bgmState, 0);
                    }
                }
            }
            break;
        }
        case Mode_StopToNext: {
            if (mObjArg3 != 1) {
                if (MR::isGalaxyRedCometAppearInCurrentStage()) {
                    break;
                }

                if (MR::isGalaxyBlackCometAppearInCurrentStage()) {
                    break;
                }
            }

            s32 fadeTime = objArg1;

            if (fadeTime < 0) {
                fadeTime = NebulaFrameClock::stepsOfMilliseconds(1500);
            }

            if (objArg2 >= 0) {
                SoundID bgmId = SoundID(AudStageBgmTable::getBgmId(MR::getCurrentStageName(), objArg2));

                if (!bgmId.isAnonymous()) {
                    if (AudWrap::getBgmMgr()->mCurrentBGM[0] == bgmId) {
                        if (MR::isPlayingStageBgm()) {
                            break;
                        }
                    }

                    AudWrap::setNextIdStageBgm(bgmId);
                }
            }

            MR::stopStageBGM(fadeTime);
            break;
        }
        case Mode_TrackMute:
            if (objArg1 < 0) {
                AudBgm* stageBgm = AudWrap::getStageBgm();

                if (stageBgm != nullptr) {
                    s32 fadeTime = NebulaFrameClock::stepsOfMilliseconds(500);

                    if (objArg2 >= 0) {
                        fadeTime = objArg2;
                    }

                    stageBgm->changeTrackMuteState(0, fadeTime);
                }
            } else {
                s32 bgmState = AudStageBgmTable::getBgmState(MR::getCurrentStageName(), objArg1);

                if (bgmState >= 0) {
                    AudBgm* stageBgm = AudWrap::getStageBgm();

                    if (stageBgm != nullptr) {
                        s32 fadeTime = NebulaFrameClock::stepsOfMilliseconds(500);

                        if (objArg2 >= 0) {
                            fadeTime = objArg2;
                        }

                        stageBgm->changeTrackMuteState(bgmState, fadeTime);
                    }
                }
            }
            break;
        }

        mIsInside = true;
    } else {
        mIsInside = false;
    }
}
