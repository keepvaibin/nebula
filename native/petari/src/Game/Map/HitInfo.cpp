#include "Game/Map/HitInfo.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/KCollision.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

Triangle::Triangle() {
    mParts = nullptr;
    mIdx = 0xFFFFFFFF;
    mSensor = nullptr;
    mNormals[0].zero();
    mNormals[1].zero();
    mNormals[2].zero();
    mNormals[3].zero();
    mPos[0].zero();
    mPos[1].zero();
    mPos[2].zero();
}

void Triangle::fillData(CollisionParts* pParts, u32 index, HitSensor* pSensor) {
    mParts = pParts;
    mIdx = index;
    mSensor = pSensor;

    KCollisionServer* pServer = pParts->mServer;
    Pos3f* pMatrix;
    KC_PrismData* pPrism = pServer->getPrismData(index);

    mNormals[0].set(*pServer->getFaceNormal(pPrism));
    mNormals[1].set(*pServer->getEdgeNormal1(pPrism));
    mNormals[2].set(*pServer->getEdgeNormal2(pPrism));
    mNormals[3].set(*pServer->getEdgeNormal3(pPrism));

    pMatrix = &mParts->mBaseMatrix;

    NebulaMtxMultVecSR(pMatrix->mMtx, &mNormals[0], &mNormals[0]);
    NebulaMtxMultVecSR(pMatrix->mMtx, &mNormals[1], &mNormals[1]);
    NebulaMtxMultVecSR(pMatrix->mMtx, &mNormals[2], &mNormals[2]);
    NebulaMtxMultVecSR(pMatrix->mMtx, &mNormals[3], &mNormals[3]);

    MR::normalize(&mNormals[0]);
    MR::normalize(&mNormals[1]);
    MR::normalize(&mNormals[2]);
    MR::normalize(&mNormals[3]);

    mPos[0].set(pServer->getPos(pPrism, 0));
    mPos[1].set(pServer->getPos(pPrism, 1));
    mPos[2].set(pServer->getPos(pPrism, 2));

    pMatrix->mult(mPos[0], mPos[0]);
    pMatrix->mult(mPos[1], mPos[1]);
    pMatrix->mult(mPos[2], mPos[2]);
}

const char* Triangle::getHostName() const {
    return mParts->getHostName();
}

s32 Triangle::getHostPlacementZoneID() const {
    return mParts->getPlacementZoneID();
}

bool Triangle::isHostMoved() const {
    return mParts->mStillCount == 0;
}

bool Triangle::isValid() const {
    return mIdx != 0xFFFFFFFF;
}

const Vec3f* Triangle::getNormal(int index) const {
    return &mNormals[index];
}

const Vec3f* Triangle::getFaceNormal() const {
    return &mNormals[0];
}

const Vec3f* Triangle::getEdgeNormal(int index) const {
    return &mNormals[index + 1];
}

const Vec3f* Triangle::getPos(int index) const {
    return &mPos[index];
}

// Normal 0 the face's, 1-3 edge plane 0-2's, recomputed from the parts' current matrix.
const Vec3f* Triangle::calcAndGetNormal(int index) {
    if (0 <= index && index <= 3) {
        KCollisionServer* server = mParts->mServer;
        KC_PrismData* prism = server->getPrismData(mIdx);
        mNormals[index].set(*server->getNormal(index == 0 ? prism->mNormalIndex : prism->mEdgeIndices[index - 1]));
        NebulaMtxMultVecSR(mParts->mBaseMatrix.mMtx, &mNormals[index], &mNormals[index]);
        MR::normalize(&mNormals[index]);
    }

    return &mNormals[index];
}

const Vec3f* Triangle::calcAndGetEdgeNormal(int index) {
    if (0 <= index && index <= 2) {
        return calcAndGetNormal(index + 1);
    }

    return &mNormals[index + 1];
}

const Vec3f* Triangle::calcAndGetPos(int index) {
    KCollisionServer* server = mParts->mServer;
    KC_PrismData* prism = server->getPrismData(mIdx);

    Vec3f* pos = &mPos[index];

    pos->set(server->getPos(prism, index));

    mParts->mBaseMatrix.mult(*pos, *pos);

    return pos;
}

void Triangle::calcForceMovePower(Vec3f* pMovement, const Vec3f& rPos) const {
    mParts->calcForceMovePower(pMovement, rPos);
}

TableIter Triangle::getAttributes() const {
    if (mIdx == -1) {
        return TableIter();
    }

    return mParts->mServer->getAttributes(mIdx);
}

Pos3f* Triangle::getBaseMtx() const {
    return &mParts->mBaseMatrix;
}

Pos3f* Triangle::getBaseInvMtx() const {
    return &mParts->mInvBaseMatrix;
}

Pos3f* Triangle::getPrevBaseMtx() const {
    return &mParts->mPrevBaseMatrix;
}

HitInfo::HitInfo() : mParentTriangle(), mDepth(0.0f), mHitPos(0, 0, 0), mNormalMove(0, 0, 0) {
    mHitPart = 0;
}

bool HitInfo::isCollisionAtFace() const {
    return mHitPart == 1;
}

bool HitInfo::isCollisionAtEdge() const {
    return mHitPart == 2 || mHitPart == 3 || mHitPart == 4;
}

bool HitInfo::isCollisionAtCorner() const {
    return mHitPart == 5 || mHitPart == 6 || mHitPart == 7;
}
