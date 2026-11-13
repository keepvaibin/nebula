#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/System/NebulaMods.hpp"
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

bool MarioActor::canTakePowerUp(s32 playerMode) const {
    // A mod's: the game's form it acts like.
    if (playerMode >= NebulaMods::cFirstPowerUp) {
        const u16 mode = static_cast< u16 >(playerMode);
        return playerMode <= 0xFFFF && NebulaMods::isPowerUp(mode) && canTakePowerUp(NebulaMods::powerUpActsLike(mode));
    }
    switch (playerMode) {
    case PlayerMode_Invincible: return mModels[4] != nullptr;
    case PlayerMode_Ice: return mModels[3] != nullptr;
    case PlayerMode_Bee: return mModels[2] != nullptr;
    case PlayerMode_Hopper: return mModels[5] != nullptr;
    case PlayerMode_Teresa: return mTeresaModel != nullptr;
    case PlayerMode_Tornado: return mTornadoMario != nullptr;
    default: return true;
    }
}

void MarioActor::setPlayerMode(u16 playerMode, bool withMorph) {
    if (!mExtra) {
        playerMode = static_cast< u16 >(NebulaMods::fire(NEBULA_MOD_EVENT_POWER_UP, playerMode));
    }

    // A mod's power-up: the player is in the game's form it acts like, so every table read by his form sees one of the
    // game's; the mod's own form is kept beside it.
    u16 modForm = 0;
    if (playerMode >= NebulaMods::cFirstPowerUp) {
        if (!NebulaMods::isPowerUp(playerMode)) {
            return;
        }
        modForm = playerMode;
        playerMode = NebulaMods::powerUpActsLike(modForm);
    }

    if (mPlayerMode == playerMode && mModForm == modForm) {
        // A mod's power-up again: its time starts over (the form's under it when it gives none).
        if (modForm != 0) {
            const s32 frames = NebulaMods::powerUpStart(modForm, this);
            if (frames > 0) {
                mPlayerModeTimer = static_cast< u16 >(frames);
                return;
            }
        }

        if (playerMode == 1) {
            mPlayerModeTimer = mConst->getTable()->mMetalHoldTime;
            MR::startSubBGM("BGM_MUTEKI_A", false);
        }

        if (playerMode == 9) {
            mPlayerModeTimer = mConst->getTable()->mTornadoHoldTime;
        }

        if (playerMode == 3) {
            mPlayerModeTimer = mConst->getTable()->mIceModeTime;
            MR::startSubBGM("BGM_ICE_A", false);
        }

        if (playerMode == 2) {
            mPlayerModeTimer = mConst->getTable()->mFireModeTime;
            MR::startSubBGM("BGM_FIRE_A", false);
        }

        if (playerMode == 7) {
            mPlayerModeTimer = 3600;
            MR::startStageBGM("BGM_FLYING_A", false);
        }

        if (playerMode != 0) {
            playEffect("アイテム再ゲット");
        }

        return;
    }

    if (mMario->isStatusActive(MarioStatus_Foo)) {
        mMario->closeStatus(nullptr);
    }

    mPlayerModeTimer = 0;
    mBlurState = 0;

    switch (mPlayerMode) {
    case 5:
        mMario->endRabbitMode();

        MR::startBtp(this, "ColorChange");
        MR::setBtpFrameAndStop(this, 0.0f);
        break;

    case 6:
        MR::deleteEffect(mTeresaModel, "Glow");

        if (playerMode == 0) {
            MR::offCalcAnim(mTeresaModel);
        } else {
            mTeresaModel->kill();
        }

        mMorphHidden = false;

        mMario->resetTeresaMode();
        break;

    case 2:
        MR::startBtp(this, "ColorChange");
        MR::setBtpFrameAndStop(this, 0.0f);

        MR::stopSubBGM(30);
        break;

    case 4:
        stopAnimation("ハチ飛行中");

        if (!mMario->isSwimming()) {
            if (!mExtra) {
            MR::getGameSceneLayoutHolder()->changeLifeMeterModeGround();
        }
        }

        mBeeModel->kill();
        mBeeWing->kill();
        break;

    case 3:
        stopEffect("アイス中");

        changeHandMaterial();

        if (mMario->isStatusActive(MarioStatus_Skate)) {
            mMario->closeStatus(nullptr);
        }

        MR::stopSubBGM(30);
        break;

    case 1:
        stopEffect("無敵中");

        mBlurState = 0;

        MR::stopSubBGM(30);

        changeHandMaterial();
        break;

    case 7:
        MR::startCurrentStageBGM();
        break;
    }

    mPrevPlayerMode = mPlayerMode;
    mPrevModForm = mModForm;
    NebulaMods::powerUpEnd(mModForm, this);
    mPlayerMode = playerMode;
    mModForm = modForm;

    // The game's form starts (a mod's power-up on the normal player: none, so no "back to normal").
    switch (modForm != 0 && playerMode == 0 ? 0xFFFF : playerMode) {
    case 9:
        mPlayerModeTimer = mConst->getTable()->mTornadoHoldTime;

        MR::startBtp(this, "TornadoElement");

        if (isJumping()) {
            changeAnimation("エレメントゲット");
        } else {
            changeAnimation("エレメントゲット接地中");
        }
        break;

    case 1:
        mPlayerModeTimer = mConst->getTable()->mMetalHoldTime;

        playEffect("無敵中");

        MR::startSubBGM("BGM_MUTEKI_A", false);

        changeHandMaterial();

        MR::stopBrk(mInvincibleModel);
        MR::stopBrk(mInvincibleHandL);
        MR::stopBrk(mInvincibleHandR);

        MR::startBrk(mInvincibleModel, "InvincibleMario");
        MR::startBrk(mInvincibleHandL, "InvincibleMarioHandL");
        MR::startBrk(mInvincibleHandR, "InvincibleMarioHandR");

        MR::removeAllClingingKarikari();
        break;

    case 0:
        MR::startBtp(this, "ElementEnd");

        if (withMorph) {
            switch (mPrevPlayerMode) {
            case 4:
            case 5:
            case 6:
                playSound("呪い解除", -1);
                break;

            default:
                playSound("変身解除", -1);
                break;
            }
        }

        mPlayerModeTimer = 0;
        mMario->mBeeGravityReviveTimer = 0;

        if (mMario->isSwimming()) {
            if (mTakeCount == 0) {
                changeAnimation(nullptr, "水泳基本");
            }
        } else if (isJumping()) {
            changeAnimation(nullptr, "落下");
        } else {
            changeAnimation(nullptr, "基本");
        }

        if (mMorphItemSensor != nullptr) {
            mMorphItemSensor->mHost->kill();
            mMorphItemSensor = nullptr;
        }
        break;

    case 5:
        mMario->startRabbitMode();
        rushDropThrowMemoSensor();
        break;

    case 6:
        mTeresaModel->appear();

        MR::onCalcAnim(mTeresaModel);

        runTeresaBaseAnimation();

        MR::startBrk(mTeresaModel, "Erase");
        MR::setBrkFrameAndStop(mTeresaModel, 0.0f);

        mMorphHidden = true;

        mTeresaAlpha = 0.0f;
        mTeresaTilt = 0.0f;

        rushDropThrowMemoSensor();

        mMario->startTeresaMode();

        mTeresaModel->calcAnim();

        MR::emitEffect(mTeresaModel, "Glow");
        MR::emitEffect(mTeresaModel, "Appear");
        break;

    case 2:
        MR::startSubBGM("BGM_FIRE_A", false);

        mPlayerModeTimer = mConst->getTable()->mFireModeTime;
        break;

    case 3:
        playEffect("アイス中");

        MR::startSubBGM("BGM_ICE_A", false);

        changeHandMaterial();

        mPlayerModeTimer = mConst->getTable()->mIceModeTime;
        break;

    case 4:
        if (!mExtra) {
            MR::getGameSceneLayoutHolder()->changeLifeMeterModeBee();
        }

        rushDropThrowMemoSensor();

        mBeeModel->appear();
        MR::showModel(mBeeModel);

        mMario->mAirWalkTimer = mConst->getTable()->mAirWalkTime;
        mMario->mInvincibleJump = 0;

        mBeeWing->appear();
        mBeeWingAnim = 0;

        MR::startBck(mBeeWing, "Wait");
        MR::startBva(mBeeWing, "Wait");

        MR::stopBtk(mBeeWing);
        break;

    case 7:
        MR::startStageBGM("BGM_FLYING_A", false);
        mPlayerModeTimer = 3600;
        break;
    }

    if (mPlayerMode == 0) {
        if (mTransforming) {
            MR::endDemo(this, "マリオ変身");
            mTransforming = false;
        }

        if (withMorph) {
            mMorphTimer = 30;
            mMario->startPadVib("マリオ[変身解除]");
        } else {
            mMorphTimer = 2;
        }

        playEffect("変身解除");
    } else {
        if (mMorphTimer == 0 || mPrevPlayerMode == 0) {
            mPowerupCollected = true;
        } else {
            mMorphTimer = 64;

            MR::stopAnimFrame(this);

            playEffect("変身");

            mMario->startPadVib("マリオ[変身]");

            mPowerupCollected = false;
        }

        mPunchFrame = 0;
        mSpinIntervalTimer = 0;

        mSpinEffectRadius = 0.0f;
        mSpinEffectHeight = 0.0f;

        mSpinStarTimer = 0;
        mSpinStarEndTimer = 0;

        updateFairyStar();
    }

    if (modForm != 0) {
        const s32 frames = NebulaMods::powerUpStart(modForm, this);
        if (frames > 0) {
            mPlayerModeTimer = static_cast< u16 >(frames);
        }
    }

    updateHandAtMorph();
}

void MarioActor::resetPlayerModeOnDamage() {
    if (isActionOk("ダメージ解除")) {
        setPlayerMode(0, true);
    }
}

void MarioActor::resetPlayerModeOnNoDamage() {
    if (isActionOk("ノーダメージ解除")) {
        setPlayerMode(0, true);
    }
}

void MarioActor::updatePlayerMode() {
    if (mPlayerModeTimer == 0) {
        return;
    }

    mPlayerModeTimer--;

    if (mPlayerModeTimer != 0) {
        return;
    }

    setPlayerMode(0, true);
}

struct MorphString {
    const char* mNames[9];  // by player mode; [0] the normal one, [8] Luigi's
    u32 mHash;  // of mNames[0]
};

static MorphString cMorphStringTable[] = {{{"DieBlackHole", 0, 0, 0, "DieBlackHoleBee", 0, 0, 0, "DieBlackHoleLuigi"}, 0}, {}};

void MarioActor::touchSensor(HitSensor* pSensor) {
    switch (pSensor->mType) {
    case ATYPE_WATER_PRESSURE_BULLET_BIND:
        if (MR::isExistMapCollision(mCenterPos, pSensor->mPosition - mCenterPos)) {
            return;
        }

        mMario->touchWater();
    }

    if (MR::isSensorEnemy(pSensor) && mMario->isStatusActive(MarioStatus_Bury)) {
        mMario->closeStatus(nullptr);
    }
}

void MarioActor::initMorphStringTable() {
    for (MorphString* item = cMorphStringTable;; item++) {
        if (item->mNames[0] == nullptr) {
            break;
        }

        item->mHash = MR::getHashCode(item->mNames[0]);
    }
}

const char* MarioActor::changeMorphString(const char* name) const {
    u32 hash = MR::getHashCode(name);
    for (MorphString* item = cMorphStringTable;; item++) {
        if (item->mNames[0] == nullptr) {
            break;
        }
        if (item->mHash == hash) {
            // By the game's eight forms (Tornado, 9, has no column: the normal one's).
            const char* morph = mPlayerMode < 8 ? item->mNames[mPlayerMode] : nullptr;
            if (morph == nullptr || mPlayerMode == PlayerMode_Normal) {
                if (gIsLuigi) {
                    if (item->mNames[8] != nullptr) {
                        return item->mNames[8];
                    }
                }
                return item->mNames[0];
            }
            return morph;
        }
    }
    return name;
}
