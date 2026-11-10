#include "Game/AreaObj/RestartCube.hpp"
#include "Game/AudioLib/AudBgmMgr.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/GameAudio/AudStageBgmTable.hpp"
#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/LumaCore.hpp"

RestartCube::RestartCube(int formType, const char* pName) : AreaObj(formType, pName), mIdInfo(), mBgmIndex(-1), mBgmStateIndex(-1), mIsBgmChanged() {
    lumacore::setClass(this, lumacore::kClassId< RestartCube >);
}

void RestartCube::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    mIdInfo = new PlacementId(mObjArg0, rIter);
    mBgmIndex = mObjArg1;
    mBgmStateIndex = mObjArg2;
    mIsBgmChanged = false;
}

void RestartCube::updatePlayerRestartIdInfo() {
    if (mObjArg3 == -1 || MR::isOnGroundPlayer()) {
        MR::setPlayerRestartIdInfo(*mIdInfo);

        if (!mIsBgmChanged && !MR::isCubeBgmChangeInvalid() && mBgmIndex >= 0) {
            changeBgm();
        }
    }
}

void RestartCube::changeBgm() {
    if (MR::isPowerStarGetDemoActive()) {
        mIsBgmChanged = true;
        return;
    }

    if (MR::isPlayingStageBgmID(STM_STAR_EXIST)) {
        mIsBgmChanged = true;
        return;
    }

    if (MR::isPlayingStageBgmID(STM_STAR_EXIST_2)) {
        mIsBgmChanged = true;
        return;
    }

    if (MR::isPlayerDead()) {
        mIsBgmChanged = true;
        return;
    }

    if (MR::isPlayingStageBgmID(STM_BOSS_MECHA_KOOPA)) {
        return;
    }

    u32 bgmId = AudStageBgmTable::getBgmId(MR::getCurrentStageName(), mBgmIndex);

    if (bgmId != -1) {
        u32 currBGM = AudWrap::getBgmMgr()->mCurrentBGM[AudBgmMgr::BgmType_Stage];

        if (currBGM == bgmId && MR::isPlayingStageBgm()) {
            return;
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

    if (mBgmStateIndex >= 0) {
        s32 state = AudStageBgmTable::getBgmState(MR::getCurrentStageName(), mBgmStateIndex);

        if (state >= 0) {
            AudBgm* stageBgm = AudWrap::getStageBgm();

            if (stageBgm != nullptr) {
                stageBgm->changeTrackMuteState(state, 0);
            }
        }
    }
}
