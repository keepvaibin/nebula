#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Render/Clip.hpp"

static u8 sBlinkStates[] = {0, 1, 2, 2, 2, 2, 1, 1, 0, 0};

void MarioActor::initBlink() {
    mBlinkFrame = 0;
    mBlinkTimer = 0;
    mEyeLidReset = 0;
    mEyeRes = nullptr;
}

void MarioActor::setBlink(const char* pName) {
    if (pName == nullptr) {
        mEyeRes = nullptr;

        return;
    }

    if (MR::getResourceHolder(this)->mBtpResTable->isExistRes(pName)) {
        Clip* pPattern = static_cast< Clip* >(MR::getResourceHolder(this)->mBtpResTable->getRes(pName));
        mEyeRes = pPattern;
        mEyeRes->mFrame = 0.0f;

        return;
    }

    mEyeRes = nullptr;
}

void MarioActor::updateBlink() {
    if (mCurrModel == 1) {
        return;
    }

    Clip* pPattern = mEyeRes;
    if (pPattern != nullptr) {
        f32 frame;

        if (pPattern->mAttribute == 2) {
            const f32 currentFrame = pPattern->mFrame;
            frame = currentFrame;
            frame += 1.0f;

            if (frame >= pPattern->mFrameMax - 1.0f) {
                frame = 0.0f;
            }
        } else {
            frame = mMarioAnim->getFrame();
        }

        mEyeRes->mFrame = frame;

        u32 eyeLidMaterial = MR::getMaterialNo(MR::getModelData(this), "EyeLid_v");

        for (u32 i = 0; i < mEyeRes->getEntryCount(); i++) {
            u32 texNo;
            mEyeRes->getTexture(i, &texNo);

            u32 materialID = mEyeRes->getMaterial(i);
            if (materialID == eyeLidMaterial) {
                mEyeLidFrame = texNo - mEyeLidTexBase;
                mEyeLidReset = 1;
                return;
            }
        }
    }

    if (isNerve(GET_NERVE(MarioActor, MarioActorNrvGameOver)) && mEyeRes == nullptr) {
        if (mMario->isAnimationTerminate(nullptr)) {
            mEyeLidFrame = 2;
        } else {
            mEyeLidFrame = 1;
        }

        return;
    }

    if (mEyeLidReset) {
        mEyeLidReset = 0;
        mBlinkFrame = 0;
        mBlinkTimer = MR::getRandom(60, 360);
        mEyeLidFrame = 0;
    }

    if (mBlinkFrame) {
        mBlinkFrame--;
        mEyeLidFrame = ::sBlinkStates[9 - mBlinkFrame];
        mBlinkTimer = MR::getRandom(60, 360);
        return;
    }

    if (mBlinkTimer != 0) {
        mBlinkTimer--;
    }

    if (mBlinkTimer == 0) {
        mBlinkFrame = 10;
    }
}
