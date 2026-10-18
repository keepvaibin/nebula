#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/ModelHolder.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <revolution/types.h>

void MarioActor::setPlayerMode(u16 playerMode, bool myBool) {
    if (mPlayerMode == playerMode) {
        if (playerMode == 1) {
            _3DC = mConst->getTable()->mMetalHoldTime;
            MR::startSubBGM("BGM_MUTEKI_A", false);
        }

        if (playerMode == 9) {
            _3DC = mConst->getTable()->mTornadoHoldTime;
        }

        if (playerMode == 3) {
            _3DC = mConst->getTable()->mIceModeTime;
            MR::startSubBGM("BGM_ICE_A", false);
        }

        if (playerMode == 2) {
            _3DC = mConst->getTable()->mFireModeTime;
            MR::startSubBGM("BGM_FIRE_A", false);
        }

        if (playerMode == 7) {
            _3DC = 3600;
            MR::startStageBGM("BGM_FLYING_A", false);
        }

        if (playerMode != 0) {
            playEffect("\203\101\203\103\203\145\203\200\215\304\203\121\203\142\203\147" /* アイテム再ゲット */);
        }

        return;
    }

    if (mMario->isStatusActive(MarioStatus_Foo)) {
        mMario->closeStatus(nullptr);
    }

    _3DC = 0;
    _A6E = 0;

    switch (mPlayerMode) {
    case 5:
        mMario->endRabbitMode();

        MR::startBtp(this, "ColorChange");
        MR::setBtpFrameAndStop(this, 0.0f);
        break;

    case 6:
        MR::deleteEffect(_9A4, "Glow");

        if (playerMode == 0) {
            MR::offCalcAnim(_9A4);
        } else {
            _9A4->kill();
        }

        _483 = false;

        mMario->resetTeresaMode();
        break;

    case 2:
        MR::startBtp(this, "ColorChange");
        MR::setBtpFrameAndStop(this, 0.0f);

        MR::stopSubBGM(30);
        break;

    case 4:
        stopAnimation("\203\156\203\140\224\362\215\163\222\206" /* ハチ飛行中 */);

        if (!mMario->isSwimming()) {
            MR::getGameSceneLayoutHolder()->changeLifeMeterModeGround();
        }

        _9E4->kill();
        _9E8->kill();
        break;

    case 3:
        stopEffect("\203\101\203\103\203\130\222\206" /* アイス中 */);

        changeHandMaterial();

        if (mMario->isStatusActive(MarioStatus_Skate)) {
            mMario->closeStatus(nullptr);
        }

        MR::stopSubBGM(30);
        break;

    case 1:
        stopEffect("\226\263\223\107\222\206" /* 無敵中 */);

        _A6E = 0;

        MR::stopSubBGM(30);

        changeHandMaterial();
        break;

    case 7:
        MR::startCurrentStageBGM();
        break;
    }

    _3D6 = mPlayerMode;
    mPlayerMode = playerMode;

    switch (playerMode) {
    case 9:
        _3DC = mConst->getTable()->mTornadoHoldTime;

        MR::startBtp(this, "TornadoElement");

        if (isJumping()) {
            changeAnimation("\203\107\203\214\203\201\203\223\203\147\203\121\203\142\203\147" /* エレメントゲット */);
        } else {
            changeAnimation("\203\107\203\214\203\201\203\223\203\147\203\121\203\142\203\147\220\332\222\156\222\206" /* エレメントゲット接地中 */);
        }
        break;

    case 1:
        _3DC = mConst->getTable()->mMetalHoldTime;

        playEffect("\226\263\223\107\222\206" /* 無敵中 */);

        MR::startSubBGM("BGM_MUTEKI_A", false);

        changeHandMaterial();

        MR::stopBrk(_9C8);
        MR::stopBrk(_A50);
        MR::stopBrk(_A54);

        MR::startBrk(_9C8, "InvincibleMario");
        MR::startBrk(_A50, "InvincibleMarioHandL");
        MR::startBrk(_A54, "InvincibleMarioHandR");

        MR::removeAllClingingKarikari();
        break;

    case 0:
        MR::startBtp(this, "ElementEnd");

        if (myBool) {
            switch (_3D6) {
            case 4:
            case 5:
            case 6:
                playSound("\216\364\202\242\211\360\217\234" /* 呪い解除 */, -1);
                break;

            default:
                playSound("\225\317\220\147\211\360\217\234" /* 変身解除 */, -1);
                break;
            }
        }

        _3DC = 0;
        mMario->_76C = 0;

        if (mMario->isSwimming()) {
            if (_468 == 0) {
                changeAnimation(nullptr, "\220\205\211\152\212\356\226\173" /* 水泳基本 */);
            }
        } else if (isJumping()) {
            changeAnimation(nullptr, "\227\216\211\272" /* 落下 */);
        } else {
            changeAnimation(nullptr, "\212\356\226\173" /* 基本 */);
        }

        if (_4A4 != nullptr) {
            _4A4->mHost->kill();
            _4A4 = nullptr;
        }
        break;

    case 5:
        mMario->startRabbitMode();
        rushDropThrowMemoSensor();
        break;

    case 6:
        _9A4->appear();

        MR::onCalcAnim(_9A4);

        runTeresaBaseAnimation();

        MR::startBrk(_9A4, "Erase");
        MR::setBrkFrameAndStop(_9A4, 0.0f);

        _483 = true;

        _9A8 = 0.0f;
        _9AC = 0.0f;

        rushDropThrowMemoSensor();

        mMario->startTeresaMode();

        _9A4->calcAnim();

        MR::emitEffect(_9A4, "Glow");
        MR::emitEffect(_9A4, "Appear");
        break;

    case 2:
        MR::startSubBGM("BGM_FIRE_A", false);

        _3DC = mConst->getTable()->mFireModeTime;
        break;

    case 3:
        playEffect("\203\101\203\103\203\130\222\206" /* アイス中 */);

        MR::startSubBGM("BGM_ICE_A", false);

        changeHandMaterial();

        _3DC = mConst->getTable()->mIceModeTime;
        break;

    case 4:
        MR::getGameSceneLayoutHolder()->changeLifeMeterModeBee();

        rushDropThrowMemoSensor();

        _9E4->appear();
        MR::showModel(_9E4);

        mMario->_402 = mConst->getTable()->mAirWalkTime;
        mMario->_42A = 0;

        _9E8->appear();
        _9F0 = 0;

        MR::startBck(_9E8, "Wait");
        MR::startBva(_9E8, "Wait");

        MR::stopBtk(_9E8);
        break;

    case 7:
        MR::startStageBGM("BGM_FLYING_A", false);
        _3DC = 3600;
        break;
    }

    if (mPlayerMode == 0) {
        if (mTransforming) {
            MR::endDemo(this, "\203\175\203\212\203\111\225\317\220\147" /* マリオ変身 */);
            mTransforming = false;
        }

        if (myBool) {
            _3D8 = 30;
            mMario->startPadVib("\203\175\203\212\203\111[\225\317\220\147\211\360\217\234]" /* マリオ[変身解除] */);
        } else {
            _3D8 = 2;
        }

        playEffect("\225\317\220\147\211\360\217\234" /* 変身解除 */);
    } else {
        if (_3D8 == 0 || _3D6 == 0) {
            mPowerupCollected = true;
        } else {
            _3D8 = 64;

            MR::stopAnimFrame(this);

            playEffect("\225\317\220\147" /* 変身 */);

            mMario->startPadVib("\203\175\203\212\203\111[\225\317\220\147]" /* マリオ[変身] */);

            mPowerupCollected = false;
        }

        _945 = 0;
        _946 = 0;

        _6D4 = 0.0f;
        _6D8 = 0.0f;

        _94C = 0;
        _94E = 0;

        updateFairyStar();
    }

    updateHandAtMorph();
}

void MarioActor::resetPlayerModeOnDamage() {
    if (isActionOk("\203\137\203\201\201\133\203\127\211\360\217\234" /* ダメージ解除 */)) {
        setPlayerMode(0, true);
    }
}

void MarioActor::resetPlayerModeOnNoDamage() {
    if (isActionOk("\203\155\201\133\203\137\203\201\201\133\203\127\211\360\217\234" /* ノーダメージ解除 */)) {
        setPlayerMode(0, true);
    }
}

void MarioActor::updatePlayerMode() {
    if (_3DC == 0) {
        return;
    }

    _3DC--;

    if (_3DC != 0) {
        return;
    }

    setPlayerMode(0, true);
}

struct myStruct {
    const char* _0[9];
    u32 _24;
};

static myStruct cMorphStringTable[] = {{"DieBlackHole", 0, 0, 0, "DieBlackHoleBee", 0, 0, 0, "DieBlackHoleLuigi", 0}, {}};

void MarioActor::touchSensor(HitSensor* pSensor) {
    switch (pSensor->mType) {
    case ATYPE_WATER_PRESSURE_BULLET_BIND:
        if (MR::isExistMapCollision(_2A0, pSensor->mPosition - _2A0)) {
            return;
        }

        mMario->touchWater();
    }

    if (MR::isSensorEnemy(pSensor) && mMario->isStatusActive(MarioStatus_Bury)) {
        mMario->closeStatus(nullptr);
    }
}

void MarioActor::initMorphStringTable() {
    for (myStruct* item = cMorphStringTable;; item++) {
        if (item->_0[0] == nullptr) {
            break;
        }

        item->_24 = MR::getHashCode(item->_0[0]);
    }
}

const char* MarioActor::changeMorphString(const char* name) const {
    u32 hash = MR::getHashCode(name);
    for (myStruct* item = cMorphStringTable;; item++) {
        if (item->_0[0] == nullptr) {
            break;
        }
        if (item->_24 == hash) {
            const char* morph = item->_0[mPlayerMode];
            if (morph == nullptr || mPlayerMode == PlayerMode_Normal) {
                if (gIsLuigi) {
                    if (item->_0[8] != nullptr) {
                        return item->_0[8];
                    }
                }
                return item->_0[0];
            }
            return morph;
        }
    }
    return name;
}
