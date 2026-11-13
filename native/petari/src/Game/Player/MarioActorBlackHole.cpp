#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/MapObj/BlackHole.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

void MarioActor::initBlackHoleOut() {
    mPosRelativeToBlackHole = mPosition - mBlackHolePosition;

    Vec3f normalisedRelativePos(mPosRelativeToBlackHole);
    Vec3f normalisedRelativeCameraPos = mCamPos - mBlackHolePosition;

    MR::normalizeOrZero(&normalisedRelativeCameraPos);
    MR::normalizeOrZero(&normalisedRelativePos);

    Mtx rotation;
    Vec3f rotateAxis;

    rotateAxis.cross(normalisedRelativeCameraPos, normalisedRelativePos);

    f32 mag = mPosRelativeToBlackHole.length();
    Vec3f killed;
    f32 flt = MR::vecKillElement(mCamPos - mPosition, mCamDirZ, &killed);
    flt *= mConst->getTable()->mBlackHoleFirstRadius;

    NebulaMtxRotAxisRad(rotation, &rotateAxis, atan(flt / mag));
    NebulaMtxMultVec(rotation, &mPosRelativeToBlackHole, &mBlackHoleRotateAxis);
    MR::normalizeOrZero(&mBlackHoleRotateAxis);

    damageDropThrowMemoSensor();
    MR::removeAllClingingKarikari();
}

void MarioActor::exeGameOverBlackHole2() {
    if (respawnExtra()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::setCubeBgmChangeInvalid();
        MR::clearBgmQueue();

        if (!(mBlackHole->tryStartDemoCamera()) && !mMario->getMovementStates().mode2D) {
            MR::startBlackHoleCamera("ブラックホール", mBlackHolePosition, mPosition);
        }

        mBindOn = false;

        changeAnimationNonStop("ブラックホール落下");
        playEffect("共通ブラックホール");
        playSound("ブラックホール吸い込まれ", -1);
        playEffect(changeMorphString("DieBlackHole"));
        initBlackHoleOut();

        mMario->mMovementStates.scaleBody = true;

        MR::startStarPointerModeDemoMarioDeath(this);
        MR::deactivateDefaultGameLayout();
    }

    if (getNerveStep() == 60) {
        if (!MR::getPlayerLeft()) {
            MR::startPlayerEvent("ゲームオーバー");
        } else {
            MR::startPlayerEvent("マリオ奈落");
        }
    }

    if (getNerveStep() == mConst->getTable()->mBlackHoleHideTime) {
        mHidden = true;

        MR::hidePlayer();
        MR::emitEffect(this, "ブラックホール消滅");
    }

    f32 nervestepfloat = getNerveStep();
    f32 flt = 1.0f;

    if (nervestepfloat < 180.0f) {
        flt = MR::sqrt(nervestepfloat / 180.0f);
    }

    f32 angle = mConst->getTable()->mBlackHoleRotateSpeed;
    angle = nervestepfloat * angle;
    angle = flt * angle;

    if (angle > mConst->getTable()->mBlackHoleRotateLimit) {
        angle = mConst->getTable()->mBlackHoleRotateLimit;
    }

    Mtx rotationMatrix;
    NebulaMtxRotAxisRad(rotationMatrix, &mBlackHoleRotateAxis, angle);
    NebulaMtxMultVec(rotationMatrix, &mPosRelativeToBlackHole, &mPosRelativeToBlackHole);

    MR::vecBlendSphere(mBlackHoleRotateAxis, -mCamDirZ, &mBlackHoleRotateAxis, 0.01f);

    f32 distChangeFactor = 180 - getNerveStep();

    if (distChangeFactor < 0.0f) {
        distChangeFactor = 0.0f;
    }

    f32 newDistToBlackHole = mPosRelativeToBlackHole.length() * distChangeFactor / (1 + distChangeFactor);

    mPosRelativeToBlackHole.setLength(newDistToBlackHole);

    f32 scale = getNerveStep() * mConst->getTable()->mBlackHoleScaleSpeed;
    scale = 1 - scale;

    if (scale < mConst->getTable()->mBlackHoleScaleLimit) {
        scale = mConst->getTable()->mBlackHoleScaleLimit;
    }

    mScale.set(scale);

    mPosition = mBlackHolePosition + mPosRelativeToBlackHole;

    mVelocity.zero();
}
