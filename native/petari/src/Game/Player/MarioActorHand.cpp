#include "Game/Player/PlayerModel.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/ModelHolder.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateHand_f1, "updateHand.f1", 0.49f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHand_f2, "updateHand.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHand_f3, "updateHand.f3", 0.49f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHand_f4, "updateHand.f4", 10.0f);

void MarioActor::initHand() {
    MR::hideJoint(mModels[0], "HandR0");
    MR::hideJoint(mModels[0], "HandL0");

    if (mBoneModel != nullptr) {
        MR::hideJoint(mModels[1], "HandR0");
        MR::hideJoint(mModels[1], "HandL0");
    }

    if (mBeeModel != nullptr) {
        MR::hideJoint(mModels[2], "HandR0");
        MR::hideJoint(mModels[2], "HandL0");
    }

    if (mIceModel != nullptr) {
        MR::hideJoint(mModels[3], "HandR0");
        MR::hideJoint(mModels[3], "HandL0");
    }

    if (mInvincibleModel != nullptr) {
        MR::hideJoint(mModels[4], "HandR0");
        MR::hideJoint(mModels[4], "HandL0");
    }

    mHandPoseL = 0;
    mHandPoseR = 0;
    mIceHandL = nullptr;
    mIceHandR = nullptr;

    if (mIceModel != nullptr) {
        mIceHandL = new ModelHolder("IceMarioHandL", true);
        MR::initMaterialFog(mIceHandL, true);
        MR::prepareMaterials(mIceHandL);

        mIceHandR = new ModelHolder("IceMarioHandR", true);
        MR::initMaterialFog(mIceHandR, true);
        MR::prepareMaterials(mIceHandR);
    }

    mHandL = new ModelHolder("MarioHandL", true);
    MR::initMaterialFog(mHandL, true);
    MR::prepareMaterials(mHandL);

    mHandR = new ModelHolder("MarioHandR", true);
    MR::initMaterialFog(mHandR, true);
    MR::prepareMaterials(mHandR);

    mInvincibleHandL = nullptr;
    mInvincibleHandR = nullptr;

    if (mInvincibleModel != nullptr) {
        mInvincibleHandL = new ModelHolder("InvincibleMarioHandL", true);
        MR::initMaterialFog(mInvincibleHandL, true);
        MR::prepareMaterials(mInvincibleHandL);

        mInvincibleHandR = new ModelHolder("InvincibleMarioHandR", true);
        MR::initMaterialFog(mInvincibleHandR, true);
        MR::prepareMaterials(mInvincibleHandR);
    }

    for (u32 idx = 0; idx < MR::getJointNum(mHandL) - 1; idx++) {
        copyMaterial(MR::getModel(mHandL), MR::getJointIndex(this, "HandL0"), idx);
        copyMaterial(MR::getModel(mHandR), MR::getJointIndex(this, "HandR0"), idx);
    }

    updateHandAtMorph();
}

void MarioActor::updateHand() {
    bool hideHands = false;
    switch (mPlayerMode) {
    case PlayerMode_Hopper:
        hideHands = true;
        break;
    }

    if (mCurrModel == 1) {
        hideHands = true;
    }
    if (getMovementStates().tornado) {
        hideHands = true;
    }
    if (mMario->isStatusActive(MarioStatus_FpView)) {
        hideHands = true;
    }
    if (mDarkMaskOn) {
        hideHands = true;
    }

    if (MR::isHiddenModel(this) || isHidden() || hideHands) {
        mHandsHidden = true;
        return;
    }
    mHandsHidden = false;

    Vec3f control;
    mMarioAnim->getXanimePlayer()->getMainAnimationTrans(MR::getJointIndex(this, "PartsControl"), &control);
    f32 leftControl = control.y;
    f32 rightControl = control.z;
    s32 leftPose = cNum_updateHand_f1 + leftControl / cNum_updateHand_f2;
    s32 rightPose = cNum_updateHand_f3 + rightControl / cNum_updateHand_f4;
    s32 left = static_cast< u8 >(MR::clamp(leftPose, 0, 7));
    s32 right = static_cast< u8 >(MR::clamp(rightPose, 0, 7));

    ModelHolder* leftHand = mHandL;
    ModelHolder* rightHand = mHandR;
    if (mPlayerMode == PlayerMode_Ice) {
        leftHand = mIceHandL;
        rightHand = mIceHandR;
    } else if (mPlayerMode == PlayerMode_Invincible) {
        leftHand = mInvincibleHandL;
        rightHand = mInvincibleHandR;
    }

    if (left != mHandPoseL) {
        if (mHandPoseL != 0xFF) {
            MR::hideJoint(MR::getJoint(leftHand, mHandPoseL + 1));
        }
        MR::showJoint(MR::getJoint(leftHand, left + 1));
    }
    if (right != mHandPoseR) {
        if (mHandPoseR != 0xFF) {
            MR::hideJoint(MR::getJoint(rightHand, mHandPoseR + 1));
        }
        MR::showJoint(MR::getJoint(rightHand, right + 1));
    }

    mHandPoseL = left;
    mHandPoseR = right;
}

void MarioActor::changeHandMaterial() {
}

void MarioActor::updateHandAtMorph() {
    ModelHolder* holder1 = mHandL;
    ModelHolder* holder2 = mHandR;

    if (mPlayerMode == 3) {
        holder1 = mIceHandL;
        holder2 = mIceHandR;
    } else if (mPlayerMode == 1) {
        holder1 = mInvincibleHandL;
        holder2 = mInvincibleHandR;
    }

    for (u32 idx = 0; idx <= 7; idx++) {
        MR::hideJoint(MR::getJoint(holder1, idx + 1));
        MR::hideJoint(MR::getJoint(holder2, idx + 1));
    }

    mHandPoseL = -1;
    mHandPoseR = -1;
}

void MarioActor::drawHand() const {
    if (mHandsHidden) {
        return;
    }

    if (mPlayerMode == 1) {
        static_cast< PlayerModel* >(MR::getModel(mInvincibleHandL))->directDraw();
        static_cast< PlayerModel* >(MR::getModel(mInvincibleHandR))->directDraw();

        return;
    }

    if (MR::isHiddenModel(mHandL)) {
        return;
    }

    PlayerModel* model = static_cast< PlayerModel* >(MR::getModel(mHandL));
    model->setDynamicList(&mLightTint);
    model->directDraw();

    model = static_cast< PlayerModel* >(MR::getModel(mHandR));
    model->setDynamicList(&mLightTint);
    model->directDraw();
}

void MarioActor::drawIndirectHand() const {
    if (mHandsHidden) {
        return;
    }

    if (MR::isHiddenModel(mIceHandL)) {
        return;
    }

    PlayerModel* model = static_cast< PlayerModel* >(MR::getModel(mIceHandL));

    if (mPlayerMode != 3) {
        return;
    }

    model->setDynamicList(&mLightTint);
    model->directDraw();

    model = static_cast< PlayerModel* >(MR::getModel(mIceHandR));
    model->setDynamicList(&mLightTint);
    model->directDraw();
}
