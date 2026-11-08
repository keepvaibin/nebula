#include "Game/Player/MarioTalk.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/MathUtil.hpp"

void Mario::startTalk(const LiveActor* pActor) {
    stopWalk();

    mTalk->mPartner = pActor;

    changeStatus(mTalk);
}

void Mario::endTalk() {
    closeStatus(mTalk);
}

MarioTalk::MarioTalk(MarioActor* pActor) : MarioState(pActor, MarioStatus_Talk), mPartner(), mIsUpdate() {
}

bool MarioTalk::update() {
    if (mIsUpdate) {
        Vec3f vec;
        MR::vecKillElement(mPartner->mPosition - getTrans(), getAirGravityVec(), &vec);

        if (!MR::normalizeOrZero(&vec)) {
            getPlayer()->setFrontVecKeepUp(vec, static_cast< u32 >(16));

            if (!getPlayer()->getMovementStates().mode2D) {
                getPlayer()->setLastNonFixMoveVec(vec);
            } else if (isPlayerModeTeresa()) {
                MR::vecKillElement(vec, getCamDirZ(), &vec);
                if (!MR::normalizeOrZero(&vec)) {
                    getPlayer()->setLastNonFixMoveVec(vec);
                }
            }
        }

        mIsUpdate = false;
    }

    return true;
}

bool MarioTalk::notice() {
    return true;
}

bool MarioTalk::close() {
    if (getPlayer()->getMovementStates().mode2D) {
        getPlayer()->setFrontVecKeepUp(getPlayer()->mPrevMoveFrontVec);
    }

    return true;
}

bool MarioTalk::start() {
    bool val = mActor->mAnimationFrozen;
    mActor->mAnimationFrozen = false;

    if (mActor->mTakeCount == 0) {
        stopAnimationUpper(nullptr);
    }

    changeAnimationNonStop("見る");

    if (val) {
        mActor->mAnimationFrozen = true;
    }

    mIsUpdate = true;

    return true;
}

void Mario::setLastNonFixMoveVec(const Vec3f& rVec) {
    mPrevMoveFrontVec = rVec;
}
