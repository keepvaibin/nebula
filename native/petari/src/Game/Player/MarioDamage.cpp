#include "Game/Player/MarioDamage.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioAbyssDamage.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioBlown.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioDarkDamage.hpp"
#include "Game/Player/MarioFaint.hpp"
#include "Game/Player/MarioFireDamage.hpp"
#include "Game/Player/MarioFireDance.hpp"
#include "Game/Player/MarioFireRun.hpp"
#include "Game/Player/MarioFreeze.hpp"
#include "Game/Player/MarioMapCode.hpp"
#include "Game/Player/MarioParalyze.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"

namespace {
    const char sDamageAirAnimation[] = "中ダメージ空中";
    const char sDamageLandAnimation[] = "中ダメージ着地";
    const char sBackDamageAirAnimation[] = "中後ダメージ空中";
    const char sBackDamageLandAnimation[] = "中後ダメージ着地";
};  // namespace

bool Mario::isDamaging() const {
    if (isAnimationRun("水上ダメージ中")) {
        return true;
    }

    if (mDamageAfterTimer) {
        return true;
    }

    if (mMovementStates.damaged) {
        return true;
    }

    if (mMovementStates.fainted) {
        return true;
    }

    if (mMovementStates.blown) {
        return true;
    }

    if (mActionStates.rushFireDamage) {
        return true;
    }

    if (mActionStates.rushElectricDamage) {
        return true;
    }

    if (mActionStates.crushRequested) {
        return true;
    }

    switch (getCurrentStatus()) {
    case MarioStatus_FireDamage:
    case MarioStatus_FireDance:
    case MarioStatus_FireRun:
    case MarioStatus_Paralyze:
    case MarioStatus_AbyssDamage:
    case MarioStatus_Freeze:
    case MarioStatus_Crush:
        return true;
    case MarioStatus_Damage:
        return mDamage->mDamaging;
    case MarioStatus_Faint:
        return mFaint->mTookDamage;
    default:
        return false;
    }
}

bool Mario::damageLarge(const Vec3f& rDirection) {
    if (damage(rDirection)) {
        if (isStatusActive(MarioStatus_Swim)) {
            mSwim->mLargeDamage = 1;
        } else {
            mDamage->setVecSize(mActor->getConst().getTable()->mJumpDistLargeDamage, mActor->getConst().getTable()->mJumpHeightLargeDamage);
            playSound("投げられ");
        }

        return true;
    }

    return false;
}

void Mario::decDamageAfterTimer() {
    MarioDamage* damage = mDamage;
    if (damage->mAfterTimer) {
        damage->mAfterTimer--;
    }

    MarioFaint* faint = mFaint;
    if (faint->mAfterTimer) {
        faint->mAfterTimer--;
    }

    MarioParalyze* paralyze = mParalyze;
    if (paralyze->mAfterTimer) {
        paralyze->mAfterTimer--;
    }

    MarioFreeze* freeze = mFreeze;
    if (freeze->mAfterTimer) {
        freeze->mAfterTimer--;
    }

    mFireDamage->decAfterTimer();
    if (mDamageAfterTimer) {
        mDamageAfterTimer--;
    }
}

bool Mario::checkDamage() {
    decDamageAfterTimer();
    if (mMovementStates.invincible) {
        return false;
    }

    if (mActor->mInDemo) {
        return false;
    }

    if (MR::isDemoActive()) {
        return false;
    }

    if (isStatusActive(MarioStatus_Talk)) {
        return false;
    }

    if (isStatusActive(MarioStatus_Recovery) || isInvincible()) {
        mMovementStates.damaged = false;
        mMovementStates.fainted = false;
        mMovementStates.blown = false;
        mActionStates.rushFireDamage = false;
        mActionStates.rushElectricDamage = false;
        mActionStates.crushRequested = false;
        mFaint->mNoDamage = false;
        return false;
    }

    if (isStatusActive(MarioStatus_Swim)) {
        checkWaterDamage();
        return false;
    }

    if (mMovementStates.damaged) {
        mMovementStates.damaged = false;
        changeStatus(mDamage);
        mMovementStates.fainted = false;
        mFaint->mNoDamage = false;
        mMovementStates.blown = false;
        return true;
    }

    if (mMovementStates.fainted) {
        mMovementStates.fainted = false;
        mMovementStates.blown = false;
        changeStatus(mFaint);
        return true;
    }

    if (mMovementStates.blown) {
        mMovementStates.blown = false;
        changeStatus(mBlown);
        return true;
    }

    if (mActionStates.rushFireDamage) {
        mActionStates.rushFireDamage = false;
        doFireDanceWithInitialDamage(1);
        return true;
    }

    if (mActionStates.rushElectricDamage) {
        mActionStates.rushElectricDamage = false;
        doParalyze();
        return true;
    }

    if (mActionStates.crushRequested) {
        mActionStates.crushRequested = false;
        if (tryCrush()) {
            return true;
        }
    }

    checkKarikariDamage();
    return false;
}

u16 Mario::getDamageAfterTimer() const {
    u16 timer = mDamage->mAfterTimer;
    if (timer < mFaint->mAfterTimer) {
        timer = mFaint->mAfterTimer;
    }

    if (timer < mParalyze->mAfterTimer) {
        timer = mParalyze->mAfterTimer;
    }

    if (timer < mFreeze->mAfterTimer) {
        timer = mFreeze->mAfterTimer;
    }

    if (timer < mFireDamage->mAfterTimer) {
        timer = mFireDamage->mAfterTimer;
    }

    if (timer < mDamageAfterTimer) {
        timer = mDamageAfterTimer;
    }

    return timer;
}

bool Mario::damageFloorCheck() {
    if (mMovementStates.invincible) {
        return false;
    }

    if (mHitStates.blown) {
        return false;
    }

    switch (mFloorCode) {
    case CollisionFloorCode_Needle:
        if (checkCurrentFloorCodeSevere(CollisionFloorCode_Needle) && doNeedleWithInitialDamage(mGroundPolygon)) {
            return true;
        }

        break;
    case 0x81:
        if (checkCurrentFloorCodeSevere(0x81) && doFireDanceWithInitialDamage(1)) {
            return true;
        }

        break;
    case CollisionFloorCode_Death:
        mActor->forceKill(3);
        return true;
    case CollisionFloorCode_DamageFire:
        if (checkCurrentFloorCodeSevere(CollisionFloorCode_DamageFire) && doFireDanceWithInitialDamage(1)) {
            return true;
        }

        break;
    case CollisionFloorCode_DamageNormal:
        if (isDamaging()) {
            return false;
        }

        if (damage(mGroundNorm * 10.0f)) {
            return true;
        }

        break;
    case CollisionFloorCode_DamageElectric:
        if (doParalyze()) {
            return true;
        }

        break;
    case CollisionFloorCode_PullBack:
        if (doRecovery()) {
            return true;
        }

        break;
    }

    return false;
}

bool Mario::damageWallCheck() {
    if (mMovementStates.invincible) {
        return false;
    }

    Vec3f normal;
    if (checkWallCodeNorm(CollisionWallCode_Rebound, &normal, false)) {
        return doFlipJump(normal * 5.0f);
    }

    if (checkWallFloorCode(CollisionFloorCode_Death)) {
        mActor->forceKill(3);
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_DamageFire) && doFireDanceWithInitialDamage(1)) {
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_Needle) && doNeedleWithInitialDamage(1)) {
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_DamageElectric) && doParalyze()) {
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_PullBack) && doRecovery()) {
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_DamageNormal) && damage(getWallNorm() * 10.0f)) {
        return true;
    }

    return false;
}

bool Mario::damagePolygonCheck(const Triangle* pTriangle) {
    if (mMovementStates.invincible) {
        return false;
    }

    switch (mFloorCodeReader->getCode(pTriangle)) {
    case 0x81:
        if (doFireDanceWithInitialDamage(1)) {
            return true;
        }

        break;
    case CollisionFloorCode_Death:
        mActor->forceKill(3);
        return true;
    case CollisionFloorCode_DamageFire:
        if (doFireDanceWithInitialDamage(1)) {
            return true;
        }

        break;
    case CollisionFloorCode_DamageNormal:
        mSwim->addDamage(*MR::getNormal(pTriangle) * 10.0f);
        return true;
    case CollisionFloorCode_DamageElectric:
        if (doParalyze()) {
            return true;
        }

        break;
    case CollisionFloorCode_PullBack:
        if (doRecovery()) {
            return true;
        }

        break;
    case CollisionFloorCode_Needle:
        doNeedleWithInitialDamage(pTriangle);
        return true;
    case CollisionFloorCode_SinkDeath:
        return true;
    }

    return false;
}

bool Mario::flipLarge(const Vec3f& rDirection) {
    if (damage(rDirection)) {
        if (isStatusActive(MarioStatus_Swim)) {
            mSwim->mLargeDamage = 1;
            mSwim->mDamageType = 1;
        } else {
            mDamage->mFlipLarge = 1;
        }

        mDamage->setVecSize(rDirection.length(), 0.0f);
        return true;
    }

    return false;
}

bool Mario::isEnableAddDamage() const {
    if (getCurrentStatus() == MarioStatus_Talk) {
        return false;
    }

    if (isDamaging()) {
        return false;
    }

    if (mActor->mPressTimer) {
        return false;
    }

    if (isInvincible()) {
        return false;
    }

    return getDamageAfterTimer() == 0;
}

bool Mario::damage(const Vec3f& rDirection) {
    mDamageDir = rDirection;
    if (!isEnableAddDamage()) {
        return false;
    }

    if (mFaint->mAfterTimer || mDamage->mAfterTimer || mMovementStates.damaged) {
        return false;
    }

    if (mMovementStates.tornado) {
        forceStopTornado();
    }

    mDamage->setVec(rDirection);
    stopWalk();
    forceStopTornado();
    mActor->damageDropThrowMemoSensor();
    if (isStatusActive(MarioStatus_Damage)) {
        closeStatus(mDamage);
    }

    mMovementStates.damaged = true;
    return true;
}

MarioDamage::MarioDamage(MarioActor* pActor) : MarioState(pActor, MarioStatus_Damage) {
    mFlipLarge = 0;
    mDamaging = 0;
    mTimer = 0;
    mAfterTimer = 0;
    mStep = 0;
    mVelocity.zero();
    mAirAnimation = nullptr;
    mLandAnimation = nullptr;
}

bool MarioDamage::start() {
    mTimer = 0;
    mStep = 0;
    if (mVelocity.dot(getPlayer()->mFrontVec) > 0.0f) {
        changeAnimationNonStop("中後ダメージ");
        mAirAnimation = ::sBackDamageAirAnimation;
        mLandAnimation = ::sBackDamageLandAnimation;
        getPlayer()->setFrontVecKeepUp(mVelocity);
    } else {
        changeAnimationNonStop("中ダメージ");
        mAirAnimation = ::sDamageAirAnimation;
        mLandAnimation = ::sDamageLandAnimation;
        getPlayer()->setFrontVecKeepUp(-mVelocity);
    }

    if (!mFlipLarge) {
        playEffect("ダメージ");
    }

    startPadVib(3);
    getPlayer()->mMovementStates.onGround = false;
    getPlayer()->mMovementStates.jumping = true;
    getPlayer()->mMovementStates.hipDrop = false;
    getPlayer()->mMovementStates.launchKind = 0;
    mVelocity += -mActor->mGravityVec * mActor->getConst().getTable()->mJumpHeightDamage;
    getPlayer()->mJumpVec = mVelocity;
    addVelocity(mVelocity);

    mDamaging = !mFlipLarge;
    if (mFlipLarge) {
        playSound("声投げられ");
        playSound("投げられ");
        mFlipLarge = 0;
        mActor->resetPlayerModeOnNoDamage();
    } else {
        playSound("声小ダメージ");
        playSound("ダメージ");
        mActor->decLifeMiddle();
        mActor->resetPlayerModeOnDamage();
    }

    return true;
}

void MarioDamage::setVec(const Vec3f& rDirection) {
    MR::vecKillElement(rDirection, mActor->mGravityVec, &mVelocity);
    mVelocity.setLength(mActor->getConst().getTable()->mJumpDistDamage);
}

void MarioDamage::setVecSize(f32 horizontal, f32 vertical) {
    mVelocity.setLength(horizontal);
    mVelocity += -mActor->mGravityVec * vertical;
}

void MarioDamage::stopHead(const Vec3f& rNormal) {
    if (!mStep) {
        Vec3f horizontal;
        f32 verticalSpeed = MR::vecKillElement(mVelocity, mActor->mGravityVec, &horizontal);
        f32 towardWall = MR::vecKillElement(horizontal, rNormal, &mVelocity);
        mVelocity += mActor->mGravityVec * verticalSpeed;
        if (towardWall < 0.0f) {
            mVelocity += rNormal * -towardWall * 0.5f;
        }
    } else {
        Vec3f horizontal;
        MR::vecKillElement(rNormal, getAirGravityVec(), &horizontal);
        if (!MR::normalizeOrZero(&horizontal)) {
            f32 towardWall = MR::vecKillElement(mVelocity, rNormal, &mVelocity);
            if (towardWall < 0.0f) {
                mVelocity += rNormal * -towardWall * 0.5f;
            }
        }
    }
}

bool MarioDamage::update() {
    mTimer++;
    if (mActor->mInDemo) {
        return true;
    }

    switch (mStep) {
    case 0:
        addVelocity(mVelocity);
        mVelocity += mActor->mGravityVec * mActor->getConst().getTable()->mGravityDamage;
        if (mTimer > 20) {
            if (mAirAnimation) {
                changeAnimation(mAirAnimation);
            }

            if (getPlayer()->mHitStates.gravityChangeSeen) {
                f32 vertical = MR::vecKillElement(mVelocity, getAirGravityVec(), &mVelocity);
                mVelocity.x *= 0.95f;
                mVelocity.y *= 0.95f;
                mVelocity.z *= 0.95f;
                mVelocity += getAirGravityVec() * vertical;
            }
        }

        if (getPlayer()->mMovementStates.onGround) {
            MarioActor* actor;
            if (getPlayer()->mVerticalSpeed > 30.0f && (actor = mActor, actor->selectDamagePop(getSensor(getGroundPolygon())))) {
                Mario* player = getPlayer();
                mVelocity += player->mGroundNorm * 20.0f;
                getPlayer()->mMovementStates.onGround = false;
                getPlayer()->mMovementStates.jumping = true;
            } else {
                getPlayer()->mMovementStates.jumping = false;
                playSound("吹っ飛び倒れ");
                changeAnimation(mLandAnimation);
                playEffect("共通ダメージ着地");
                MR::vecKillElement(mVelocity, mActor->mGravityVec, &mVelocity);
                mTimer = 0;
                mStep++;
                if (!mActor->mHealth) {
                    mStep = 2;
                }
            }
        } else if (!mActor->mHealth) {
            if (mTimer > 240) {
                mActor->forceGameOverAbyss();
            }
        } else if (mTimer > 360) {
            mActor->forceGameOverAbyss();
        }

        break;
    case 1:
        if (!getPlayer()->mMovementStates.onGround) {
            getPlayer()->mMovementStates.jumping = true;
            mStep = 0;
            break;
        }

        MR::vecKillElement(mVelocity, getAirGravityVec(), &mVelocity);
        addVelocity(mVelocity);
        mVelocity.x *= 0.95f;
        mVelocity.y *= 0.95f;
        mVelocity.z *= 0.95f;
        if (!isAnimationRun(mLandAnimation)) {
            return false;
        }

        if (mTimer > 15 && checkTrgA()) {
            getPlayer()->tryJump();
            return false;
        }

        break;
    case 2:
        if (!getPlayer()->mMovementStates.onGround) {
            getPlayer()->mMovementStates.jumping = true;
            mStep = 0;
            break;
        }

        if (mTimer == 40) {
            if (!mActor->mHealth) {
                mActor->forceGameOver();
            } else {
                return false;
            }
        }

        break;
    }

    getPlayer()->mJumpVec = mVelocity;
    return true;
}

bool MarioDamage::close() {
    stopAnimation("ダメージ");
    stopAnimation("ダメージ着地", "基本");
    if (mDamaging) {
        mAfterTimer = 120;
    }

    return true;
}

bool MarioDamage::notice() {
    if (!mActor->mHealth) {
        if (getNoticedStatus() == MarioStatus_Swim) {
            mActor->forceGameOver();
        }

        return true;
    }

    return false;
}

MarioFireDamage::MarioFireDamage(MarioActor* pActor) : MarioState(pActor, MarioStatus_FireDamage) {
    mAfterTimer = 0;
}

void MarioFireDamage::decAfterTimer() {
    if (mAfterTimer && !isStatusActiveID(MarioStatus_FireDance) && !isStatusActiveID(MarioStatus_FireRun)) {
        mAfterTimer--;
    }
}

bool Mario::doAbyssDamage() {
    if (getCurrentStatus() == MarioStatus_AbyssDamage) {
        return false;
    }

    stopWalk();
    mActor->damageDropThrowMemoSensor();
    MR::removeAllClingingKarikari();
    mActor->mBlurState = 0;
    changeStatus(mAbyssDamage);
    return true;
}

MarioAbyssDamage::MarioAbyssDamage(MarioActor* pActor) : MarioState(pActor, MarioStatus_AbyssDamage) {
    mTimer = 0;
    mStep = 0;
    mMove.zero();
}

bool MarioAbyssDamage::start() {
    mTimer = 0;
    mStep = 0;
    mActor->forceGameOverAbyss();
    return false;
}

bool MarioAbyssDamage::update() {
    addTrans(mMove);
    switch (mStep) {
    case 0:
        mStep++;
        mTimer = 120;
        MR::requestStartGameOverDemo();
        break;
    case 1:
        if (mTimer) {
            mTimer--;
        }

        if (!mTimer) {
            mActor->forceGameOverAbyss();
            return false;
        }

        break;
    }

    return true;
}

bool MarioAbyssDamage::close() {
    return true;
}

void Mario::connectToFireRun() {
    if (mActor->mHealth) {
        changeStatus(mFireRun);
        stopJump();
        mFireRun->mTimer = 1;
    }
}

MarioFireRun::MarioFireRun(MarioActor* pActor) : MarioState(pActor, MarioStatus_FireRun) {
    mTimer = 0;
    mStep = 0;
    mVerticalSpeed = 0.0f;
}

bool MarioFireRun::start() {
    mTimer = mActor->getConst().getTable()->mFireRunTimer1;
    mStep = 0;
    stopAnimationUpper(nullptr);
    changeAnimation("ファイアラン前兆");
    if (!getPlayer()->mMovementStates.onGround) {
        mVerticalSpeed = -mActor->getConst().getTable()->mFireRunFirstJump;
    } else {
        mVerticalSpeed = 0.0f;
    }

    return true;
}

bool MarioFireRun::move() {
    if (getStickX() != 0.0f || getStickY() != 0.0f) {
        const Vec3f& padDir = getWorldPadDir();
        getPlayer()->setFrontVecKeepUp(padDir, mActor->getConst().getTable()->mFireRunTurnRatio);
    }

    if (getPlayer()->checkTrgA() || mActor->isRequestJump()) {
        Mario* player = getPlayer();
        player->mWalkSpeed = 1.0f;
        getPlayer()->tryJump();
        return false;
    }

    return true;
}

bool MarioFireRun::update() {
    switch (mStep) {
    case 0:
        if (!getPlayer()->mMovementStates.onGround) {
            getPlayer()->mJumpVec = getAirGravityVec() * mVerticalSpeed;
            addVelocity(getAirGravityVec() * mVerticalSpeed);
            mVerticalSpeed += mActor->getConst().getTable()->mFireRunGravity;
            if (mVerticalSpeed > 50.0f) {
                mVerticalSpeed = 50.0f;
            }

            const Vec3f& velocity = mActor->mLastPushMove;
            if (velocity.dot(getAirGravityVec()) < -10.0f) {
                mVerticalSpeed = 0.0f;
                addVelocity(getFrontVec() * 5.0f);
            }
        } else if (mTimer) {
            mTimer--;
        } else {
            mStep++;
            mTimer = mActor->getConst().getTable()->mFireRunTimer2;
            if (!mActor->mHealth) {
                mTimer >>= 1;
            }

            mVerticalSpeed = 0.0f;
            changeAnimation("炎のランナー");
        }

        break;
    case 1:
        playSound("炎ダメージ炎上中");
        if (!getPlayer()->mMovementStates.onGround) {
            mStep = 2;
            mTimer += mActor->getConst().getTable()->mFireRunTimer3;
        }

        addVelocity(getFrontVec() * mActor->getConst().getTable()->mFireRunSpeed);
        if (mTimer) {
            mTimer--;
        }

        if (!mTimer) {
            mTimer = mActor->getConst().getTable()->mFireRunTimer3;
            mStep++;
        }

        return move();
    case 2:
        if (mActor->isEnableNerveChange() && getStickP() > 0.7f) {
            return false;
        }

        if (!getPlayer()->mMovementStates.onGround) {
            getPlayer()->mJumpVec = getAirGravityVec() * mVerticalSpeed;
            addVelocity(getAirGravityVec() * mVerticalSpeed);
            mVerticalSpeed += mActor->getConst().getTable()->mFireRunGravity;
            if (mVerticalSpeed > 50.0f) {
                mVerticalSpeed = 50.0f;
            }

            const Vec3f& velocity = mActor->mLastPushMove;
            if (velocity.dot(getAirGravityVec()) < -10.0f) {
                mVerticalSpeed = 0.0f;
                addVelocity(getFrontVec() * 5.0f);
            }

            if (mTimer) {
                mTimer--;
            }

            break;
        }

        const MarioConstTable* table = mActor->getConst().getTable();
        if (mTimer > table->mFireRunTimer3) {
            addVelocity(getFrontVec() * table->mFireRunSpeed);
        } else {
            addVelocity(getFrontVec() * table->mFireRunSpeed * mTimer / table->mFireRunTimer3);
        }

        if (mActor->isEnableNerveChange()) {
            if (mTimer) {
                mTimer--;
            }

            if (!mTimer) {
                return false;
            }
        }

        return move();
    }

    return true;
}

bool MarioFireRun::close() {
    if (!mActor->mHealth) {
        mActor->forceGameOver();
    }

    if (getPlayer()->mMovementStates.jumping) {
        stopAnimation("炎のランナー", "落下");
    } else {
        stopAnimation("炎のランナー", "基本");
        if (getStickP() < 0.1f) {
            playSound("声炎ダメージ終了");
        }
    }

    stopEffect("炎ダメージ煙");
    stopEffect("炎ダメージ青煙");
    mActor->mBlueFireDamage = 0;
    return true;
}

bool MarioFireRun::notice() {
    return false;
}

bool Mario::doFireDanceWithInitialDamage(u8 amount) {
    if (mMovementStates.invincible) {
        return false;
    }

    bool started = doFireDance();
    if (started) {
        for (u32 i = 0; i < amount; i++) {
            mActor->decLife(0);
        }

        if (!mActor->mHealth) {
            mActor->forceGameOverNonStop();
        }
    }

    return started;
}

bool Mario::doFireObjHitWithInitialDamage() {
    if (isEnableAddDamage() == false) {
        return false;
    }

    return doFireDanceWithInitialDamage(1);
}

bool Mario::doNeedleWithInitialDamage(u8 amount) {
    if (mMovementStates.invincible) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Teresa) {
        doTeresaReflection(getWallNorm(), false);
        return false;
    }

    bool started = doNeedle(nullptr);
    if (started) {
        for (u32 i = 0; i < amount; i++) {
            mActor->decLife(0);
        }

        if (!mActor->mHealth) {
            mActor->forceGameOverNonStop();
        }
    }

    return started;
}

bool Mario::doNeedleWithInitialDamage(const Triangle* pTriangle) {
    if (mMovementStates.invincible) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Teresa) {
        doTeresaReflection(*MR::getNormal(pTriangle), false);
        return false;
    }

    bool started = doNeedle(pTriangle);
    if (started) {
        mActor->decLife(0);
        if (!mActor->mHealth) {
            mActor->forceGameOverNonStop();
        }
    }

    return started;
}

bool Mario::doNeedle(const Triangle* pTriangle) {
    if (getCurrentStatus() == MarioStatus_FireDamage) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireRun) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireDance) {
        return false;
    }

    if (mMovementStates.damaged) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Teresa) {
        if (pTriangle != nullptr) {
            doTeresaReflection(*MR::getNormal(pTriangle), false);
        }

        return false;
    }

    if (isInvincible()) {
        return false;
    }

    mActor->resetPlayerModeOnDamage();
    getPlayer()->mMovementStates.hipDrop = false;
    getPlayer()->mMovementStates.squat = false;
    mActor->damageDropThrowMemoSensor();
    mFireDance->mNeedle = 1;
    changeStatus(mFireDance);
    return true;
}

bool Mario::doFireDance() {
    if (getCurrentStatus() == MarioStatus_Paralyze) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireDamage) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireRun) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireDance) {
        return false;
    }

    if (mMovementStates.damaged) {
        return false;
    }

    if (isInvincible()) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Ice) {
        return false;
    }

    mActor->resetPlayerModeOnDamage();
    getPlayer()->mMovementStates.hipDrop = false;
    getPlayer()->mMovementStates.squat = false;
    mActor->damageDropThrowMemoSensor();
    mFireDance->mNeedle = 0;
    changeStatus(mFireDance);
    mFireDamage->mAfterTimer = 120;
    return true;
}

MarioFireDance::MarioFireDance(MarioActor* pActor) : MarioState(pActor, MarioStatus_FireDance) {
    mMoveVelocity.zero();
    mWallStart = 0;
    mVerticalSpeed = 0.0f;
    mTurnLockTimer = 0;
    mHopCount = 0;
    mNeedle = 0;
}

bool MarioFireDance::start() {
    stopAnimationUpper(nullptr);
    mVerticalSpeed = -mActor->getConst().getTable()->mFireDanceFirstJump;
    Mario* player = getPlayer();
    MR::vecKillElement(player->mJumpVec, getAirGravityVec(), &mMoveVelocity);
    mWallStart = 0;
    mTurnLockTimer = 0;
    if (getPlayer()->mDrawStates.wallDamageCheck) {
        mVerticalSpeed *= 0.25f;
        mMoveVelocity = getPlayer()->getWallNorm() * mActor->getConst().getTable()->mFireDanceFirstJump * 0.5f;
        getPlayer()->setFrontVecKeepUp(getPlayer()->getWallNorm());
        mWallStart = 1;
        mTurnLockTimer = 60;
    }

    mHopCount = 0;
    impact();
    impactEffect();
    startPadVib(3);
    getPlayer()->mJumpVec = getAirGravityVec() * mVerticalSpeed;
    return true;
}

void MarioFireDance::impact() {
    changeAnimation("ファイアダンス");
    if (!getPlayer()->mDrawStates.wallDamageCheck) {
        if (mMoveVelocity.length() > 2.0f * mActor->getConst().getTable()->mFireDanceMoveSpeed) {
            mMoveVelocity.setLength(2.0f * mActor->getConst().getTable()->mFireDanceMoveSpeed);
        }

        mMoveVelocity.setLength(0.5f * mMoveVelocity.length());
    }

    getPlayer()->mMovementStates.onGround = false;
    getPlayer()->mMovementStates.jumping = true;
}

void MarioFireDance::impactEffect() {
    playSound("ダメージ");
    playEffect("ダメージ");
    switch (mNeedle) {
    case 0:
        playSound("声炎ダメージ");
        playSound("炎ダメージ");
        if (mActor->mBlueFireDamage) {
            playEffect("炎ダメージ青煙");
        } else {
            playEffect("炎ダメージ煙");
        }

        break;
    case 1:
        playSound("声針ダメージ");
        playSound("針ダメージ");
        break;
    }
}

bool MarioFireDance::update() {
    if (getPlayer()->mMovementStates.onGround && !getPlayer()->isRising()) {
        if (getPlayer()->mFloorCode != 0x81 && getPlayer()->mFloorCode != CollisionFloorCode_DamageFire && getPlayer()->mFloorCode != CollisionFloorCode_Needle) {
            if (mHopCount == 1) {
                if (MR::getPlayerLeft() == 0 && !mActor->mHealth) {
                    mActor->changeGameOverAnimation();
                    return true;
                }

                getPlayer()->connectToFireRun();
                return false;
            }

            mHopCount++;
            mVerticalSpeed = -mActor->getConst().getTable()->mFireDanceSecondJump;
            impact();
            startPadVib(0u);
            playSound("炎ダメージ復帰バウンド");
            if (mNeedle == 1) {
                playSound("声針ダメージ中");
            } else {
                playSound("声炎ダメージ中");
            }

            changeAnimation("ファイアダンス");
        } else {
            if (!mNeedle) {
                mActor->decLifeLarge();
            } else {
                mActor->decLifeMiddle();
            }

            if (!mActor->mHealth) {
                mActor->forceGameOverNonStop();
            }

            mVerticalSpeed = -mActor->getConst().getTable()->mFireDanceFirstJump;
            impact();
            impactEffect();
            startPadVib(3);
        }
    }

    getPlayer()->mJumpVec = getAirGravityVec() * mVerticalSpeed;
    addVelocity(getAirGravityVec() * mVerticalSpeed);
    if (mVerticalSpeed < 0.0f) {
        mVerticalSpeed += mActor->getConst().getTable()->mFireDanceGravityRise;
    } else {
        mVerticalSpeed += mActor->getConst().getTable()->mFireDanceGravityDrop;
    }

    if (mVerticalSpeed > 50.0f) {
        mVerticalSpeed = 50.0f;
    }

    if (getStickP() != 0.0f) {
        if (mTurnLockTimer) {
            mTurnLockTimer--;
        } else {
            const Vec3f& padDir = getWorldPadDir();
            getPlayer()->setFrontVecKeepUp(padDir, mActor->getConst().getTable()->mFireDanceTurnRatio);
            mMoveVelocity += getFrontVec() * mActor->getConst().getTable()->mFireDanceMoveAcc;
        }
    }

    MR::vecKillElement(mMoveVelocity, getAirGravityVec(), &mMoveVelocity);
    if (!mWallStart && mMoveVelocity.length() > mActor->getConst().getTable()->mFireDanceMoveSpeed) {
        mMoveVelocity.setLength(mActor->getConst().getTable()->mFireDanceMoveSpeed);
    }

    addVelocity(mMoveVelocity);
    return true;
}

bool MarioFireDance::close() {
    stopAnimation("ファイアダンス");
    stopEffect("炎ダメージ煙");
    stopEffect("炎ダメージ青煙");
    return true;
}

void Mario::checkKarikariDamage() {
    if (!mHitStates.karikariClinging || isDamaging()) {
        mKarikariTimer = 120;
        return;
    }

    if (mActor->mInRush) {
        return;
    }

    if (mActor->mInDemo) {
        return;
    }

    if (isStatusActive(MarioStatus_Talk)) {
        return;
    }

    if (mHitStates.karikariClinging && mKarikariTimer) {
        mKarikariTimer--;
        if (!mKarikariTimer) {
            if (mActor->mHealth == 1) {
                faint(mHeadVec);
                mKarikariTimer = 120;
                return;
            }

            startPadVib(2);
            playSound("声小ダメージ");
            playSound("ダメージ");
            mActor->decLifeSmall();
            mActor->mDamageFlashTimer = 16;
            mKarikariTimer = 120;
            if (!mActor->mHealth) {
                mActor->forceGameOver();
            }
        }
    }
}

bool Mario::doDarkDamage() {
    if (getCurrentStatus() == MarioStatus_DarkDamage) {
        return false;
    }

    mActor->mRemoteDemo = true;
    stopWalk();
    mActor->damageDropThrowMemoSensor();
    playSound("声沼沈み");
    playEffect("ダークマター死亡");
    setSeVersion(1);
    changeStatus(mDarkDamage);
    return true;
}

MarioDarkDamage::MarioDarkDamage(MarioActor* pActor) : MarioState(pActor, MarioStatus_DarkDamage) {
    mTimer = 0;
    mStep = 0;
}

bool MarioDarkDamage::start() {
    mTimer = 0;
    mStep = 0;
    return true;
}

bool MarioDarkDamage::update() {
    switch (mStep) {
    case 0:
        mStep++;
        mTimer = 150;
        MR::requestStartGameOverDemo();
        break;
    case 1:
        if (mTimer) {
            mTimer--;
        }

        if (!mTimer) {
            mActor->forceKill(3);
            MarioActor* actor = mActor;
            actor->mHiddenByDemo = 1;
            actor->updateHand();
            actor->updateFace();
        }

        break;
    }

    if (mTimer) {
        playSound("ダークマター沈み");
    }

    return true;
}

bool MarioState::close() {
    return true;
}

bool MarioState::update() {
    return true;
}

bool MarioState::start() {
    return true;
}

bool MarioDarkDamage::notice() {
    return true;
}

bool MarioAbyssDamage::notice() {
    return true;
}
