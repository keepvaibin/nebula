#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioFpView.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/RushEndInfo.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

void MarioActor::forceKill(u32 type) {
    if (mMario->getMovementStates()._1F) {
        return;
    }

    if (!isEnableNerveChange()) {
        return;
    }

    _A6E = 0;

    if (!MR::isDead(_994)) {
        _994->kill();
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        mMario->mFpView->forceClose();
        mMario->closeStatus(nullptr);
    }

    if (_934) {
        if (_924 != nullptr && _924->isType(ACTMES_GROUP_SHOW)) {
            _B91 = true;
            _B90 = true;
        }

        if (_934) {
            if (!_924->receiveMessage(ACTMES_RUSH_CANCEL, getSensor("body"))) {
                _924->receiveMessage(ACTMES_RUSH_FORCE_CANCEL, getSensor("body"));
            }

            if (_934) {
                RushEndInfo endInfo(nullptr, 4, TVec3f(0.0f, 0.0f, 0.0f), false, 0);
                endRush(&endInfo);
            }
        }

        _934 = false;
    }

    if (type == 0) {
        mMario->doAbyssDamage();
        return;
    }

    if (type == 3) {
        mMario->startPadVib(3);
    }

    if (type == 4) {
        mMario->startPadVib(3);
        initDarkMask();
        mMario->doDarkDamage();

        return;
    }

    switch (type) {
    case 1:
    case 2:
    case 5:
        MR::deactivateDefaultGameLayout();

    default:
        _39D = type;
        mHealth = 0;

        setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOver));
        break;
    }
}

void MarioActor::exeGameOver() {
    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 32) {
        _A6E = 0;
    }

    _46C = nullptr;

    PSMTXIdentity(mMarioAnim->_7C);

    damageDropThrowMemoSensor();

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("\203\121\201\133\203\200\203\111\201\133\203\157\201\133" /* ゲームオーバー */);
        changeGameOverAnimation();
    } else {
        switch (_39D) {
        case 2:
            MR::startPlayerEvent("\203\123\201\133\203\130\203\147\203\214\201\133\203\130\225\211\202\257" /* ゴーストレース負け */);
            break;

        case 1:
            MR::startPlayerEvent("\203\214\201\133\203\130\225\211\202\257" /* レース負け */);
            changeGameOverAnimation();
            break;

        case 5:
            MR::startPlayerEvent("\203\214\201\133\203\130\225\211\202\257" /* レース負け */);
            break;

        default:
            MR::startPlayerEvent("\203\175\203\212\203\111\203\137\203\105\203\223" /* マリオダウン */);
        }
    }

    MR::startGlobalEventCameraNoTarget("\217\270\223\126\203\112\203\201\203\211" /* 昇天カメラ */);
    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeGameOverAbyss() {
    mVelocity = getLastMove() * 0.99f + mCamDirZ * 35.0f * 0.01f;
    mLastMove = mVelocity;

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();
    MR::startGlobalEventCameraNoTarget("\223\336\227\216\203\112\203\201\203\211" /* 奈落カメラ */);

    _F44 = false;

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("\203\121\201\133\203\200\203\111\201\133\203\157\201\133" /* ゲームオーバー */);
    } else {
        MR::startPlayerEvent("\203\175\203\212\203\111\223\336\227\216" /* マリオ奈落 */);
    }

    MR::startStarPointerModeDemoMarioDeath(this);
    mMario->changeAnimationNonStop("\223\336\227\216\203\137\203\105\203\223" /* 奈落ダウン */);

    _B90 = true;
}

void MarioActor::exeGameOverFire() {
    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 120) {
        _A6E = 0;
    }

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("\203\121\201\133\203\200\203\111\201\133\203\157\201\133" /* ゲームオーバー */);
        MR::startGlobalEventCameraNoTarget("\217\270\223\126\203\112\203\201\203\211" /* 昇天カメラ */);
    } else {
        MR::startPlayerEvent("\203\175\203\212\203\111\211\212\203\137\203\105\203\223" /* マリオ炎ダウン */);
        MR::startGlobalEventCameraNoTarget("\223\336\227\216\203\112\203\201\203\211" /* 奈落カメラ */);
    }

    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeGameOverSink() {
    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 120) {
        _A6E = 0;
    }

    if (MR::isFirstStep(this)) {
        MR::setCubeBgmChangeInvalid();
        MR::clearBgmQueue();
        changeGameOverAnimation();

        if (MR::getPlayerLeft() == 0) {
            MR::startPlayerEvent("\203\121\201\133\203\200\203\111\201\133\203\157\201\133" /* ゲームオーバー */);
            MR::startGlobalEventCameraNoTarget("\217\270\223\126\203\112\203\201\203\211" /* 昇天カメラ */);
        } else {
            MR::startPlayerEvent("\203\175\203\212\203\111\203\137\203\105\203\223" /* マリオダウン */);
            MR::startGlobalEventCameraNoTarget("\223\336\227\216\203\112\203\201\203\211" /* 奈落カメラ */);
        }

        MR::startStarPointerModeDemoMarioDeath(this);

        if (mMario->_960 == 25 || mMario->_960 == 17) {
            playEffect("\215\273\226\204\202\334\202\350\221\314" /* 砂埋まり体 */);
            playEffect("\215\273\226\204\202\334\202\350\216\350" /* 砂埋まり手 */);
        }

        if (mMario->_960 == 31) {
            playEffect("\223\104\226\204\202\334\202\350\221\314" /* 泥埋まり体 */);
            playEffect("\223\104\226\204\202\334\202\350\216\350" /* 泥埋まり手 */);
        }
    }

    if (mMario->_960 == 31 || mMario->_960 == 18) {
        playSound("\217\300\216\200\226\123" /* 沼死亡 */, -1);

        if (getNerveStep() == 90) {
            playSound("\220\272\217\300\222\276\202\335\216\200\226\123" /* 声沼沈み死亡 */, -1);
        }
    } else {
        playSound("\215\273\216\200\226\123" /* 砂死亡 */, -1);

        if (getNerveStep() == 90) {
            playSound("\220\272\215\273\222\276\202\335\216\200\226\123" /* 声砂沈み死亡 */, -1);
        }
    }
}

void MarioActor::exeGameOverNonStop() {
    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("\203\121\201\133\203\200\203\111\201\133\203\157\201\133" /* ゲームオーバー */);
        MR::startGlobalEventCameraNoTarget("\217\270\223\126\203\112\203\201\203\211" /* 昇天カメラ */);
    } else {
        MR::startPlayerEvent("\203\175\203\212\203\111\211\212\203\137\203\105\203\223" /* マリオ炎ダウン */);
        MR::startGlobalEventCameraNoTarget("\223\336\227\216\203\112\203\201\203\211" /* 奈落カメラ */);
    }

    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeTimeWait() {
    if (_FB8 != getNerveStep()) {
        return;
    }

    setNerve(_FB4);
    _FB4 = nullptr;
}
