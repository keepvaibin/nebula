#include "Game/Util/JointRumbler.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/RumbleCalculator.hpp"
#include "math_types.hpp"


JointRumbler::JointRumbler(LiveActor* pActor, const char* pJointName, f32 frequency, f32 amplitude, u32 duration, bool useExtraScale, s32 startDelay)
    : mRumbleCalculator(), mJointTransform(), mStartDelay(startDelay), mDelayLeft(), mModelScale(1.0f), mFlags() {
    if (pActor->mModelManager->mXanimePlayer->getCore()->mTransformList == nullptr) {
        MR::initJointTransform(pActor);
    }

    mJointTransform = MR::getJointTransform(pActor, pJointName);
    mRumbleCalculator = new RumbleCalculatorCosMultLinear(frequency, HALF_PI, amplitude, duration);

    if (useExtraScale) {
        mFlags |= FLAG_EXTRA_SCALE;
    }
}

void JointRumbler::update() {
    f32 scale;

    if (mDelayLeft == 0) {
        mRumbleCalculator->calc();
        scale = mRumbleCalculator->mValue.y;
    } else {
        if (mDelayLeft > 0) {
            mDelayLeft--;
        }

        scale = 0.0f;
    }

    updateModelScale(scale + 1.0f);
}

void JointRumbler::start() {
    mRumbleCalculator->start(0);
    mDelayLeft = mStartDelay;
}

void JointRumbler::reset() {
    mRumbleCalculator->reset();
    mDelayLeft = -1;
    updateModelScale(1.0f);
}

bool JointRumbler::isRumbling() const {
    return mRumbleCalculator->mFrame < mRumbleCalculator->mDuration;
}

void JointRumbler::updateModelScale(f32 scale) {
    if (testFlag(FLAG_EXTRA_SCALE)) {
        updateScale(&mJointTransform->mExtraScale, scale);
    } else {
        updateScale(&mJointTransform->mScale, scale);
    }

    mModelScale = scale;
}

void JointRumbler::updateScale(Vec3f* pVec, f32 scale) {
    if (!testFlag(FLAG_LOCK_X)) {
        pVec->x = scale;
    }

    if (!testFlag(FLAG_LOCK_Y)) {
        pVec->y = scale;
    }

    if (!testFlag(FLAG_LOCK_Z)) {
        pVec->z = scale;
    }
}
