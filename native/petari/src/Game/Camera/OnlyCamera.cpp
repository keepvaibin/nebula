#include "Game/Camera/OnlyCamera.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcStartPose_f1, "calcStartPose.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSafePose_f1, "calcSafePose.f1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSafePose_f2, "calcSafePose.f2", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveToIdealPosition_f1, "moveToIdealPosition.f1", 0.5f);

namespace {
    NEBULA_TUNABLE(f32, sMinDistance, 1.0f);
    NEBULA_TUNABLE(f32, sBaseLength, 300.0f);
    NEBULA_TUNABLE(f32, sAccel, 1.0f);
    NEBULA_TUNABLE(f32, sSpeedMax, 100.0f);
};  // namespace

OnlyCamera::OnlyCamera(const char* pName)
    : NameObj(pName), mPoseParam(new CameraPoseParam()), mStartPose(true), mCalcIdeal(), mPos(0.0f, 0.0f, 0.0f), mSpeed(), mIsResetting() {
}

void OnlyCamera::calcPose(CameraMan* pCameraMan) {
    if (mIsResetting) {
        mCalcIdeal = false;
        mIsResetting = false;
        mStartPose = true;
    }

    if (mStartPose) {
        calcStartPose(pCameraMan);
        mStartPose = false;
    } else {
        calcSafePose(pCameraMan);
    }
}

void OnlyCamera::calcStartPose(CameraMan* pCameraMan) {
    Vec3f pos = CameraLocalUtil::getPos(pCameraMan);
    Vec3f watchPos = CameraLocalUtil::getWatchPos(pCameraMan);
    Vec3f up = CameraLocalUtil::getUpVec(pCameraMan);

    Vec3f offset = watchPos - pos;
    if (offset.length() < ::sBaseLength) {
        if (MR::isNearZero(offset)) {
            watchPos.set(pos + Vec3f(0.0f, 0.0f, -1.0f));
        } else {
            MR::normalize(&offset);
            watchPos.set(pos + offset * ::sBaseLength);
        }
    }

    Vec3f front = watchPos - pos;
    MR::normalize(&front);
    MR::normalizeOrZero(&up);

    if (MR::isNearZero(up) || MR::abs(front.dot(up)) > cNum_calcStartPose_f1) {
        Quat4f rot;
        rot.setRotate(Vec3f(0.0f, 0.0f, -1.0f), front);
        up.set< f32 >(0.0f, 1.0f, 0.0f);
        rot.transform(up, up);
    }

    mPoseParam->mPos.set(pos);
    mPoseParam->mUpVec.set(up);
    mPoseParam->mWatchPos.set(watchPos);
    mPoseParam->mWatchUpVec.set(CameraLocalUtil::getWatchUpVec(pCameraMan));
    mPoseParam->mGlobalOffset.set(CameraLocalUtil::getGlobalOffset(pCameraMan));
    mPoseParam->mLocalOffset.set(CameraLocalUtil::getLocalOffset(pCameraMan));
    mPoseParam->mRoll = CameraLocalUtil::getRoll(pCameraMan);

    mPos.set(pos);
}

void OnlyCamera::calcSafePose(CameraMan* pCameraMan) {
    Vec3f pos = CameraLocalUtil::getPos(pCameraMan);
    Vec3f watchPos = CameraLocalUtil::getWatchPos(pCameraMan);
    Vec3f up = CameraLocalUtil::getUpVec(pCameraMan);

    Vec3f offset = watchPos - pos;
    f32 dist = offset.length();
    if (dist < ::sBaseLength) {
        if (dist < ::sMinDistance) {
            watchPos.set(pos + mPoseParam->mWatchPos - mPoseParam->mPos);
        } else {
            MR::normalize(&offset);
            watchPos.set(pos + offset * ::sBaseLength);
        }
    }

    moveToIdealPosition(&pos);

    Vec3f front = watchPos - pos;
    MR::normalize(&front);
    MR::normalizeOrZero(&up);

    if (MR::isNearZero(up) || MR::abs(front.dot(up)) > cNum_calcSafePose_f1) {
        Vec3f poseFront = mPoseParam->mWatchPos - mPoseParam->mPos;
        MR::normalize(&poseFront);
        if (MR::abs(front.dot(poseFront)) > cNum_calcSafePose_f2) {
            up.set(mPoseParam->mUpVec);
        } else {
            Quat4f rot;
            rot.setRotate(poseFront, front);
            rot.transform(mPoseParam->mUpVec, up);
        }
    }
    CameraLocalUtil::recalcUpVec(&up, front);

    mPoseParam->mPos.set(pos);
    mPoseParam->mUpVec.set(up);
    mPoseParam->mWatchPos.set(watchPos);
    mPoseParam->mWatchUpVec.set(CameraLocalUtil::getWatchUpVec(pCameraMan));
    mPoseParam->mGlobalOffset.set(CameraLocalUtil::getGlobalOffset(pCameraMan));
    mPoseParam->mLocalOffset.set(CameraLocalUtil::getLocalOffset(pCameraMan));
    mPoseParam->mRoll = CameraLocalUtil::getRoll(pCameraMan);
}

void OnlyCamera::moveToIdealPosition(Vec3f* pPos) {
    Vec3f pos = *pPos;
    if (mCalcIdeal) {
        Vec3f diff = pos - mPos;
        pPos->set(mPoseParam->mPos + diff);

        Vec3f offset = pos - *pPos;
        f32 dist = offset.length();

        if (dist < mSpeed * cNum_moveToIdealPosition_f1 * (s32)mSpeed) {
            mSpeed -= ::sAccel;
            if (mSpeed < 0.0f) {
                mSpeed = 0.0f;
            }
        } else {
            mSpeed += ::sAccel;
            if (mSpeed > ::sSpeedMax) {
                mSpeed = ::sSpeedMax;
            }
        }

        if (dist < mSpeed || mSpeed == 0.0f) {
            mCalcIdeal = false;
            mSpeed = 0.0f;
            pPos->set(pos);
        } else {
            MR::normalize(&offset);
            offset.scale(mSpeed);
            pPos->add(offset);
        }
    } else {
        pPos->set(pos);
    }
    mPos.set(pos);
}
