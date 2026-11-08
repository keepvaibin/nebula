#include "Game/Player/MarioSlider.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/MathUtil.hpp"

bool Mario::isSliderFloor() const {
    return mFloorCode == 9;
}

bool Mario::checkSliderMode() const {
    if (!mMovementStates.jumping && mMovementStates.onGround) {
        if (isSliderFloor()) {
            return true;
        }
    }

    return false;
}

MarioSlider::MarioSlider(MarioActor* pActor) : MarioState(pActor, MarioStatus_Slider) {
    mVelocity.zero();
    mSlopeDir.zero();
    mAirTimer = 0;
    mBrake = 0.0f;
    mLean = 0.0f;
}

void MarioSlider::calcGroundAccel() {
    Vec3f* val = &getPlayer()->mGroundNorm;
    Vec3f v6 = val->cross(*getPlayer()->getGravityVec());
    MR::normalizeOrZero(&v6);
    mSlopeDir.cross(v6, *val);
    MR::normalizeOrZero(&mSlopeDir);
}

bool MarioSlider::postureCtrl(MtxPtr) {
    Mtx rotMtx;
    Vec3f& frontVec = getFrontVec();
    NebulaMtxRotAxisRad(rotMtx, &frontVec, 0.2f * (MR::pi() * mLean / 2.0f));

    Vec3f newHeadVec;
    NebulaMtxMultVec(rotMtx, &getPlayer()->mGroundNorm, &newHeadVec);

    MarioConst* pConst = mActor->mConst;
    Mario* pPlayer = getPlayer();
    MR::vecBlendSphere(getPlayer()->mPostureUpVec, newHeadVec, &pPlayer->mPostureUpVec, pConst->getTable()->mSliderHeadRotateRatio);
    return false;
}

void MarioSlider::calcWallHit() {
    if (getPlayer()->mMovementStates.sideWall) {
        Vec3f alongSideWall(mVelocity);
        f32 sideDot = MR::vecKillElement(alongSideWall, *getPlayer()->mSideWallTriangle->getNormal(0), &alongSideWall);

        if (sideDot < 0.0f) {
            if (-sideDot < 10.0f) {
                sideDot = -10.0f;
            }

            mVelocity = alongSideWall - *getPlayer()->mSideWallTriangle->getNormal(0) * sideDot;
        }
    }

    if (getPlayer()->mMovementStates.frontWall) {
        Vec3f alongFrontWall(mVelocity);
        f32 frontDot = MR::vecKillElement(alongFrontWall, *getPlayer()->mFrontWallTriangle->getNormal(0), &alongFrontWall);

        if (frontDot < 0.0f) {
            if (-frontDot < 10.0f) {
                frontDot = -10.0f;
            }

            f32 frontScale = 0.5f * frontDot;
            mVelocity = alongFrontWall - *getPlayer()->mFrontWallTriangle->getNormal(0) * frontScale;

            Vec3f newFront(mVelocity);
            MR::normalize(&newFront);
            getPlayer()->setFrontVecKeepUp(newFront);
        }
    }
}

void Mario::startSlider() {
    if (!isStatusActive(mSlider->mStatusId)) {
        if (getPlayer()->mMovementStates.hipDrop) {
            if (getPlayer()->mMovementStates.jumping) {
                mDrawStates.hipDropLand = true;
                playSound("尻ドロップ着地");
                playSound("声尻ドロップ着地");
                playEffectRT("属性尻ドロップ", mGroundNorm, mPosition);
                startPadVib("最強");
                startCamVib(0);
                MR::removeAllClingingKarikari();
            }
        }

        stopWalk();
        stopJump();
        changeStatus(mSlider);
    }
}

bool MarioSlider::start() {
    mVelocity = getPlayer()->mPrevVelocity;
    MR::vecKillElement(mVelocity, getPlayer()->mGroundNorm, &mVelocity);
    getPlayer()->cancelSquatMode();
    changeAnimation("スライダー尻", "スライダー尻");
    mAirTimer = 10;
    mBrake = 0.0f;
    mLean = 0.0f;
    return true;
}

bool MarioSlider::update() {
    if (!getPlayer()->checkSliderMode()) {
        if (getPlayer()->mMovementStates.onGround != 0) {
            if (isSlipPolygon(getGroundPolygon())) {
                Mario* player = getPlayer();
                player->mSlopeMoveVec = mVelocity;
                player->mSlipPower = 10.0f;
                return false;
            }
        }

        mAirTimer--;
        if (mAirTimer == 0) {
            getPlayer()->tryDrop();
            getPlayer()->mJumpVec = mActor->getLastMove();
            return false;
        }
    } else {
        mAirTimer = 10;
    }

    calcGroundAccel();

    if (checkTrgA()) {
        getPlayer()->mWalkSpeed = (mVelocity.length() / mActor->mConst->getTable()->mJumpFrontSpeed);
        getPlayer()->tryJump();
        return false;
    } else {
        f32 stickY = -getStickY();
        f32 stickX = getStickX();
        mBrake = (stickY * (1.0f - mActor->getConst().getTable()->mSliderBrakeIne)) + (mBrake * mActor->mConst->getTable()->mSliderBrakeIne);
        MarioConstTable* tbl = mActor->mConst->getTable();
        mLean = (stickX * (1.0f - tbl->mSliderWeightIne)) + (mLean * tbl->mSliderWeightIne);
        f32 v18 = mVelocity.length();
        v18 -= mActor->mConst->getTable()->mSliderBrakePow;

        if (v18 > 20.0f) {
            v18 = (v18 * (1.0f - (0.1f * mBrake)));
        }

        Vec3f v48(mVelocity);

        if (MR::isNearZero(v48)) {
            v48 = mSlopeDir * mActor->mConst->getTable()->mSliderSlopePow;
        }

        if (MR::isNearZero(v48)) {
            return false;
        } else {
            MR::normalize(&v48);
            Vec3f v47(getPlayer()->mGroundNorm);
            Vec3f v46 = v48.cross(v47);
            MR::vecKillElement(v46, getPlayer()->mGroundNorm, &v46);
            MR::normalize(&v46);
            mVelocity.setLength(v18);
            mVelocity += mSlopeDir * mActor->mConst->getTable()->mSliderSlopePow;

            mVelocity += v46 * mLean * mActor->mConst->getTable()->mSliderWeightPow;
            MR::vecKillElement(mVelocity, getPlayer()->mGroundNorm, &mVelocity);
            if (mVelocity.length() > mActor->mConst->getTable()->mSliderMaxSpeed) {
                mVelocity.setLength(mActor->mConst->getTable()->mSliderMaxSpeed);
            }

            addVelocity(mVelocity);

            Vec3f v45(-getPlayer()->mGroundNorm * 15.0f);

            MR::vecKillElement(v45, getFrontVec(), &v45);
            addVelocity(v45);

            Vec3f v44(getPlayer()->mFrontVec);
            Vec3f v43;
            MR::vecKillElement(mVelocity, *getPlayer()->getGravityVec(), &v43);

            if (!MR::isNearZero(v43)) {
                MR::normalize(&v43);
                getPlayer()->setFrontVecKeepUp(v43, mActor->mConst->getTable()->mSliderFrontTurnRatio);
            }

            calcWallHit();
            playSound("坂滑り");
            return true;
        }
    }

    return true;
}

bool MarioSlider::close() {
    if (getPlayer()->isSwimming()) {
        stopAnimation("スライダー尻");
    } else if (getPlayer()->mMovementStates.jumping) {
        stopAnimation("スライダー尻", "落下");
    } else {
        stopAnimation("スライダー尻", "基本");
    }

    return true;
}
