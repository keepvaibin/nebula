#include "Game/Boss/DinoPackunTail.hpp"
#include "Game/Boss/DinoPackunTailNode.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

DinoPackunTail::DinoPackunTail(u32 nodeCount)
    : mNodes(new DinoPackunTailNode*[nodeCount]), mNumNodes(0), mBendRate(1.0f), mDistanceRate(1.0f),
      mHostVelocityRate(0.0f), mIsActive(true) {}

// Appends pNode after the last node.
void DinoPackunTail::addTailNode(DinoPackunTailNode* pNode) {
    if (mNumNodes != 0) {
        DinoPackunTailNode* pPrevNode = mNodes[mNumNodes - 1];
        pPrevNode->mNextNode = pNode;
        pNode->mPrevNode = pPrevNode;
    }

    mNodes[mNumNodes++] = pNode;
}

void DinoPackunTail::activate() {
    mIsActive = true;

    for (u32 i = 0; i < mNumNodes; i++) {
        mNodes[i]->resetJoint();
        mNodes[i]->makeActorAppeared();
    }
}

void DinoPackunTail::deactivate() {
    mIsActive = false;

    for (u32 i = 0; i < mNumNodes; i++) {
        mNodes[i]->makeActorDead();
    }
}

void DinoPackunTail::onMovement() {
    for (u32 i = 0; i < mNumNodes; i++) {
        MR::requestMovementOn(mNodes[i]);
    }
}

void DinoPackunTail::lockNodePosition(u32 idx) {
    mNodes[idx]->lockPosition();
}

void DinoPackunTail::unlockNodePosition(u32 idx) {
    mNodes[idx]->unLockPosition();
}

void DinoPackunTail::lockEndNodePosition() {
    getNode(mNumNodes - 1)->lockPosition();
}

void DinoPackunTail::unlockEndNodePosition() {
    getNode(mNumNodes - 1)->unLockPosition();
}

void DinoPackunTail::registerPreCalcJointCallBack() {
    if (!mIsActive) {
        return;
    }

    for (u32 i = 0; i < mNumNodes; i++) {
        mNodes[i]->registerPreCalcJointCallBack();
    }
}

void DinoPackunTail::registerJointCallBack() {
    if (!mIsActive) {
        return;
    }

    for (u32 i = 0; i < mNumNodes; i++) {
        mNodes[i]->registerJointCallBack();
    }
}

void DinoPackunTail::updateJoint() {
    if (mIsActive) {
        addAccelKeepDistance();
        addAccelKeepBend();
        addAccelToBck();
    }
}

// Straightens each bend: where a node's direction turns from the last non-zero one, node i and node i-2 are pushed
// apart across the bend, harder the sharper it is. A zero direction is skipped.
void DinoPackunTail::addAccelKeepBend() {
    Vec3f prevDir;
    prevDir.set(*mNodes[0]->getNodeDirection());
    MR::normalize(&prevDir);

    for (u32 i = 1; i < mNumNodes; i++) {
        Vec3f dir;
        dir.set(*mNodes[i]->getNodeDirection());

        if (MR::isNearZero(dir)) {
            continue;
        }

        MR::normalize(&dir);
        f32 cosine;
        Vec3f axis;
        if (MR::makeAxisAndCosignVecToVec(&axis, &cosine, prevDir, dir) && cosine < 1.1f) {
            f32 bendPower = mNodes[i]->getKeepBendPower();
            f32 rate = 1.0f - MR::normalize(cosine, -1.0f, 1.1f);
            f32 accel = mBendRate * (rate * bendPower);
            Vec3f push = dir.cross(axis);
            MR::normalize(&push);
            mNodes[i]->addNodeVelocityHost(push * accel);

            if (i >= 2) {
                Vec3f backPush = prevDir.cross(axis);
                MR::normalize(&backPush);
                mNodes[i - 2]->addNodeVelocityHost(backPush * accel);
            }
        }

        prevDir.set(dir);
    }
}

// Springs each link toward its length, half the correction to each end.
void DinoPackunTail::addAccelKeepDistance() {
    Vec3f prevPos;
    prevPos.set(mNodes[0]->mPosition);

    for (u32 i = 1; i < mNumNodes; i++) {
        Vec3f pos;
        pos.set(mNodes[i]->mPosition);
        Vec3f dir;
        dir.set(pos - prevPos);

        if (MR::isNearZero(dir)) {
            MR::getRandomVector(&dir, 0.1f);
        }

        f32 distance;
        MR::separateScalarAndDirection(&distance, &dir, dir);
        f32 linkLength = getNode(i)->getLinkLength();
        f32 accel = (0.9f * (mDistanceRate * (linkLength - distance))) / 2.0f;
        mNodes[i]->addNodeVelocityHost(dir * accel);
        mNodes[i - 1]->addNodeVelocityHost(-dir * accel);

        prevPos = pos;
    }
}

// Pulls every node but the root toward its animated joint.
void DinoPackunTail::addAccelToBck() {
    if (mHostVelocityRate <= 0.0f) {
        return;
    }

    for (u32 i = 1; i < mNumNodes; i++) {
        Vec3f toJoint;
        toJoint.set(mNodes[i]->mJointPos - mNodes[i]->mPosition);
        mNodes[i]->addNodeVelocityHost(toJoint * mHostVelocityRate);
    }
}

void DinoPackunTail::getTailNodePosition(Vec3f* pPos, s32 index) const {
    pPos->set(mNodes[index]->mPosition);
}

void DinoPackunTail::getEndTailPosition(Vec3f* pPos) const {
    pPos->set(mNodes[mNumNodes - 1]->mPosition);
}

DinoPackunTailNode* DinoPackunTail::getNode(u32 index) const {
    return mNodes[index];
}
