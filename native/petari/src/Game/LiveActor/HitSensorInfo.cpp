#include "Game/LiveActor/HitSensorInfo.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"

namespace {
    void addRotated(Vec3f* pPos, MtxPtr pMtx, const Vec3f& rOffset) {
        pPos->x += pMtx[0][0] * rOffset.x + pMtx[0][1] * rOffset.y + pMtx[0][2] * rOffset.z;
        pPos->y += pMtx[1][0] * rOffset.x + pMtx[1][1] * rOffset.y + pMtx[1][2] * rOffset.z;
        pPos->z += pMtx[2][0] * rOffset.x + pMtx[2][1] * rOffset.y + pMtx[2][2] * rOffset.z;
    }
};  // namespace

HitSensorInfo::HitSensorInfo(const char* pName, HitSensor* pSensor, const Vec3f* pFollowPos, MtxPtr pFollowMtx, const Vec3f& rOffset,
                             bool isHostCallback)
    : mName(pName), mHashCode(MR::getHashCode(pName)), mSensor(pSensor), mOffset(rOffset), mFollowPos(pFollowPos), mFollowMtx(pFollowMtx),
      mIsHostCallback(isHostCallback) {
}

void HitSensorInfo::update() {
    if (mIsHostCallback) {
        mSensor->mHost->updateHitSensor(mSensor);
        return;
    }

    Vec3f position;
    if (mFollowMtx) {
        position.set< f32 >(mFollowMtx[0][3], mFollowMtx[1][3], mFollowMtx[2][3]);
        ::addRotated(&position, mFollowMtx, mOffset);
    } else {
        position.set(mFollowPos != nullptr ? *mFollowPos : mSensor->mHost->mPosition);

        MtxPtr baseMtx = mSensor->mHost->getBaseMtx();
        if (baseMtx) {
            ::addRotated(&position, baseMtx, mOffset);
        } else {
            position.add(mOffset);
        }
    }

    mSensor->mPosition.set(position);
}

void HitSensorInfo::doObjCol() {
    for (s32 i = 0; i < mSensor->mSensorCount; i++) {
        if (!MR::isDead(mSensor->mSensors[i]->mHost)) {
            mSensor->mHost->attackSensor(mSensor, mSensor->mSensors[i]);
        }
    }
}
