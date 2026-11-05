#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Camera/CameraRegisterHolder.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

namespace CameraLocalUtil {
    const Vec3f& getWatchPos(const CameraMan* pCameraMan) {
        return pCameraMan->mPoseParam->mWatchPos;
    }

    const Vec3f& getPos(const CameraMan* pCameraMan) {
        return pCameraMan->mPoseParam->mPos;
    }

    const Vec3f& getUpVec(const CameraMan* pCameraMan) {
        return pCameraMan->mPoseParam->mUpVec;
    }

    const Vec3f& getWatchUpVec(const CameraMan* pCameraMan) {
        return pCameraMan->mPoseParam->mWatchUpVec;
    }

    f32 getFovy(const CameraMan* pCameraMan) {
        return pCameraMan->mPoseParam->mFovy;
    }

    const Vec3f& getGlobalOffset(const CameraMan* pCameraMan) {
        return pCameraMan->mPoseParam->mGlobalOffset;
    }

    const Vec3f& getLocalOffset(const CameraMan* pCameraMan) {
        return pCameraMan->mPoseParam->mLocalOffset;
    }

    f32 getRoll(const CameraMan* pCameraMan) {
        return pCameraMan->mPoseParam->mRoll;
    }

    void setWatchPos(CameraMan* pCameraMan, const Vec3f& rWatchPos) {
        pCameraMan->mPoseParam->mWatchPos.set(rWatchPos);
    }

    void setPos(CameraMan* pCameraMan, const Vec3f& rPos) {
        pCameraMan->mPoseParam->mPos.set(rPos);
    }

    void setUpVec(CameraMan* pCameraMan, const Vec3f& rUpVec) {
        pCameraMan->mPoseParam->mUpVec.set(rUpVec);
    }

    void setWatchUpVec(CameraMan* pCameraMan, const Vec3f& rWatchUpVec) {
        pCameraMan->mPoseParam->mWatchUpVec.set(rWatchUpVec);
    }

    void setFovy(CameraMan* pCameraMan, f32 fovy) {
        pCameraMan->mPoseParam->mFovy = fovy;
    }

    void setGlobalOffset(CameraMan* pCameraMan, const Vec3f& rGlobalOffset) {
        pCameraMan->mPoseParam->mGlobalOffset.set(rGlobalOffset);
    }

    void setLocalOffset(CameraMan* pCameraMan, const Vec3f& rLocalOffset) {
        pCameraMan->mPoseParam->mLocalOffset.set(rLocalOffset);
    }

    void setFrontOffset(CameraMan* pCameraMan, f32 frontOffset) {
        pCameraMan->mPoseParam->mFrontOffset = frontOffset;
    }

    void setUpperOffset(CameraMan* pCameraMan, f32 upperOffset) {
        pCameraMan->mPoseParam->mUpperOffset = upperOffset;
    }

    void setRoll(CameraMan* pCameraMan, f32 roll) {
        pCameraMan->mPoseParam->mRoll = roll;
    }

    CameraTargetObj* getTarget(const CameraMan* pCameraMan) {
        return pCameraMan->mDirector->getTarget();
    }

    const MtxPtr getMtxReg(const char* pRegName) {
        return getCameraDirector()->mRegisterHolder->getMtx(pRegName);
    }

    const Vec3f& getVecReg(const char* pRegName) {
        return *getCameraDirector()->mRegisterHolder->getVec(pRegName);
    }

    const char* getDummyVecRegName() {
        return getCameraDirector()->mRegisterHolder->getDummyVecRegName();
    }

    bool isForceCameraChange() {
        return getCameraDirector()->isForceCameraChange();
    }

    CameraDirector* getCameraDirector() {
        return MR::getCameraDirector();
    }

    void setUsedTarget(const CameraMan* pCameraMan, CameraTargetObj* pUsedTarget) {
        pCameraMan->mDirector->mTargetObj = pUsedTarget;
    }

    CameraTargetObj* getTarget(const Camera* pCamera) {
        return pCamera->mCameraMan->mDirector->getTarget();
    }

    const Vec3f& getWatchPos(const Camera* pCamera) {
        return pCamera->mPoseParam->mWatchPos;
    }

    const Vec3f& getPos(const Camera* pCamera) {
        return pCamera->mPoseParam->mPos;
    }

    const Vec3f& getUpVec(const Camera* pCamera) {
        return pCamera->mPoseParam->mUpVec;
    }

    const Vec3f& getWatchUpVec(const Camera* pCamera) {
        return pCamera->mPoseParam->mWatchUpVec;
    }

    f32 getFovy(const Camera* pCamera) {
        return pCamera->mPoseParam->mFovy;
    }

    const Vec3f& getGlobalOffset(const Camera* pCamera) {
        return pCamera->mPoseParam->mGlobalOffset;
    }

    const Vec3f& getLocalOffset(const Camera* pCamera) {
        return pCamera->mPoseParam->mLocalOffset;
    }

    f32 getFrontOffset(const Camera* pCamera) {
        return pCamera->mPoseParam->mFrontOffset;
    }

    f32 getUpperOffset(const Camera* pCamera) {
        return pCamera->mPoseParam->mUpperOffset;
    }

    f32 getRoll(const Camera* pCamera) {
        return pCamera->mPoseParam->mRoll;
    }

    void setWatchPos(Camera* pCamera, const Vec3f& rWatchPos) {
        pCamera->mPoseParam->mWatchPos.set(rWatchPos);
    }

    void setPos(Camera* pCamera, const Vec3f& rPos) {
        pCamera->mPoseParam->mPos.set(rPos);
    }

    void setUpVec(Camera* pCamera, const Vec3f& rUpVec) {
        pCamera->mPoseParam->mUpVec.set(rUpVec);
    }

    void setWatchUpVec(Camera* pCamera, const Vec3f& rWatchUpVec) {
        pCamera->mPoseParam->mWatchUpVec.set(rWatchUpVec);
    }

    void setUpVecAndWatchUpVec(Camera* pCamera, const Vec3f& rUpVec) {
        CameraPoseParam* pPoseParam = pCamera->mPoseParam;

        pPoseParam->mUpVec.set(rUpVec);
        pPoseParam->mWatchUpVec.set(rUpVec);
    }

    void setFovy(Camera* pCamera, f32 fovy) {
        pCamera->mPoseParam->mFovy = fovy;
    }

    void setGlobalOffset(Camera* pCamera, const Vec3f& rGlobalOffset) {
        pCamera->mPoseParam->mGlobalOffset.set(rGlobalOffset);
    }

    void setLocalOffset(Camera* pCamera, const Vec3f& rLocalOffset) {
        pCamera->mPoseParam->mLocalOffset.set(rLocalOffset);
    }

    void setFrontOffset(Camera* pCamera, f32 frontOffset) {
        pCamera->mPoseParam->mFrontOffset = frontOffset;
    }

    void setUpperOffset(Camera* pCamera, f32 upperOffset) {
        pCamera->mPoseParam->mUpperOffset = upperOffset;
    }

    void setRoll(Camera* pCamera, f32 roll) {
        pCamera->mPoseParam->mRoll = roll;
    }

    void recalcUpVec(Vec3f* pUp, const Vec3f& rFront) {
        Vec3f side = pUp->cross(rFront);
        MR::normalize(&side);
        pUp->cross(rFront, side);
        MR::normalize(pUp);
    }

    void makeWatchOffset(Vec3f* pDst, Camera* pCamera, CameraTargetObj* pTarget, f32 scale) {
        f32 offset;
        if (pCamera->mIsLOfsErpOff || pCamera->mCameraMan->mRequestLOfsReset) {
            offset = 1.0f;
        } else {
            offset = pTarget->getLastMove().length() * scale;
            if (offset > 1.0f) {
                offset = 1.0f;
            }
        }

        Vec3f localOffs = getLocalOffset(pCamera);
        localOffs += (pTarget->getFrontVec() * getFrontOffset(pCamera) + pTarget->getUpVec() * getUpperOffset(pCamera) - localOffs) * offset;
        setLocalOffset(pCamera, localOffs);

        Vec3f globalOffs = getGlobalOffset(pCamera);
        pCamera->mZoneMatrix.mult33(globalOffs);
        pDst->set(globalOffs + localOffs);
    }

    void makeWatchPoint(Vec3f* pDst, Camera* pCamera, CameraTargetObj* pTarget, f32 scale) {
        makeWatchOffset(pDst, pCamera, pTarget, scale);
        pDst->add(pTarget->getPosition());
    }

    void makeWatchOffsetImm(Vec3f* pDst, Camera* pCamera, CameraTargetObj* pTarget) {
        Vec3f offs = pTarget->getFrontVec() * getFrontOffset(pCamera) + pTarget->getUpVec() * getUpperOffset(pCamera);
        setLocalOffset(pCamera, offs);

        Vec3f globOffs = getGlobalOffset(pCamera);
        pCamera->mZoneMatrix.mult33(globOffs);
        pDst->set(globOffs + offs);
    }

    void makeWatchPointImm(Vec3f* pDst, Camera* pCamera, CameraTargetObj* pTarget) {
        makeWatchOffsetImm(pDst, pCamera, pTarget);
        pDst->add(pTarget->getPosition());
    }

    bool tryCameraReset() {
        if (getCameraDirector()->isEnableToReset()) {
            return testCameraPadButtonReset();
        }

        return false;
    }

    bool tryCameraResetTrigger() {
        if (!getCameraDirector()->isEnableToReset()) {
            return false;
        }

        if (getCameraDirector()->isSubjectiveCamera()) {
            return false;
        }

        return testCameraPadTriggerReset();
    }

    bool testCameraPadButtonReset() {
        if (getCameraDirector()->isSubjectiveCamera()) {
            return false;
        }

        return MR::isHeldCamera(PLAYER_1);
    }

    bool testCameraPadTriggerReset() {
        if (getCameraDirector()->isSubjectiveCamera()) {
            return false;
        }

        return MR::isPressedCamera(PLAYER_1);
    }

    bool testCameraPadTriggerRoundLeft() {
        if (MR::isDemoActive()) {
            return false;
        }

        if (MR::isFirstPersonCamera()) {
            return false;
        }

        return MR::isPressedLeft(PLAYER_1);
    }

    bool testCameraPadTriggerRoundRight() {
        if (MR::isDemoActive()) {
            return false;
        }

        if (MR::isFirstPersonCamera()) {
            return false;
        }

        return MR::isPressedRight(PLAYER_1);
    }

    void slerpCamera(Quat4f* pDst, const Quat4f& rA, const Quat4f& rB, f32 ratio, bool reverse) {
        Quat4f rotA, rotB;
        rotA.normalize(rA);
        rotB.normalize(rB);
        f32 cos = rotA.dot(rotB);
        bool neg;
        if (cos < 0.0f) {
            neg = true;
            cos = -cos;
        } else {
            neg = false;
        }

        f32 sratio;
        if (1.0f - cos <= 0.00001f) {
            sratio = 1.0f - ratio;
        } else {
            f32 angle = MR::acos(cos);
            if (reverse) {
                angle -= MR::pi() * 2;
            }
            f32 sin = MR::sin(angle);
            sratio = MR::sin((1.0f - ratio) * angle) / sin;
            ratio = MR::sin(ratio * angle) / sin;
        }

        if (neg) {
            ratio = -ratio;
        }

        pDst->set(sratio * rotA.x + ratio * rotB.x, sratio * rotA.y + ratio * rotB.y, sratio * rotA.z + ratio * rotB.z,
                  sratio * rotA.w + ratio * rotB.w);
    }

    bool makeTowerCameraMtx(Pos3f* pMtx, const Pos3f& rMtx, const Vec3f& rPos, const Vec3f& rUp, const Vec3f& rWatchPos) {
        Vec3f pos = rPos;
        Vec3f up = rUp;
        rMtx.mult(pos, pos);
        rMtx.mult33(up);

        Vec3f diff = rWatchPos - pos;
        Vec3f front = diff.killElement(up);

        if (MR::isNearZero(front)) {
            return false;
        }

        MR::normalize(&front);
        Vec3f side = up.cross(front);
        pMtx->setXDir(side);
        pMtx->setYDir(up);
        pMtx->setZDir(front);
        pMtx->setTrans(rWatchPos);
        return true;
    }


    void arrangeDistanceByFovy(Camera* pCamera, Vec3f pos, f32 offset) {
        Vec3f viewDir = getPos(pCamera) - getWatchPos(pCamera);
        if (MR::isNearZero(viewDir)) {
            return;
        }

        f32 viewDist = viewDir.length();
        MR::normalize(&viewDir);
        Vec3f posDiff = pos - getWatchPos(pCamera);
        f32 projDist = posDiff.dot(viewDir);
        Vec3f projZ = viewDir * projDist;

        f32 dist = projDist + (offset + posDiff.distance(projZ)) / MR::tanDegree(MR::getFovy() * 0.5f);
        if (dist > viewDist) {
            setPos(pCamera, viewDir * dist + getWatchPos(pCamera));
        }
    }

    void arrangeDistanceByPushAndPull(Camera* pCamera, f32 push, f32 pull) {
        Vec3f viewDir = getPos(pCamera) - getWatchPos(pCamera);
        f32 viewDist = viewDir.length();

        if (viewDist < push) {
            if (MR::isNearZero(viewDir)) {
                MR::getCameraInvViewMtx().getZDir(viewDir);
                viewDir.negate();
            }
            viewDir.setLength(push);
        } else if (viewDist > pull) {
            viewDir.setLength(pull);
        }

        setPos(pCamera, getWatchPos(pCamera) + viewDir);
    }

    inline void keepAwayWatchPos(CameraMan* pCameraMan, Camera* pCamera, Vec3f* watchPos, const Vec3f& pos) {
        Vec3f dir = *watchPos - pos;
        f32 length = dir.length();

        if (length < 300.0f) {
            if (length < 1.0f) {
                watchPos->set(pos + CameraLocalUtil::getWatchPos(pCameraMan) - CameraLocalUtil::getPos(pCameraMan));
            } else {
                dir.normalize();
                watchPos->set(pos + dir * 300.0f);
            }
        }
    }

    inline void calcSafeUpVec(CameraMan* pCameraMan, Camera* pCamera, Vec3f* up, const Vec3f& watchPos, const Vec3f& pos) {
        Vec3f camWatchDir = watchPos - pos;
        MR::normalize(&camWatchDir);
        MR::normalizeOrZero(up);

        if (MR::isNearZero(*up) || MR::abs(camWatchDir.dot(*up)) > 0.98f) {
            Vec3f watchDir = getWatchPos(pCameraMan) - getPos(pCameraMan);
            MR::normalize(&watchDir);
            if (MR::abs(camWatchDir.dot(watchDir)) > 0.98f) {
                up->set(getUpVec(pCameraMan));
            } else {
                Quat4f rot;
                rot.setRotate(watchDir, camWatchDir);
                rot.transform(getUpVec(pCameraMan), *up);
            }
            recalcUpVec(up, camWatchDir);
        }
    }

    void calcSafePose(CameraMan* pCameraMan, Camera* pCamera) {
        Vec3f pos = getPos(pCamera);
        Vec3f watchPos = getWatchPos(pCamera);
        Vec3f up = getUpVec(pCamera);

        if (MR::isNan(pos) || MR::isNan(watchPos) || MR::isNan(up)) {
            return;
        }

        keepAwayWatchPos(pCameraMan, pCamera, &watchPos, pos);
        calcSafeUpVec(pCameraMan, pCamera, &up, watchPos, pos);

        setPos(pCameraMan, pos);
        setUpVec(pCameraMan, up);
        setWatchPos(pCameraMan, watchPos);
        setWatchUpVec(pCameraMan, getWatchUpVec(pCamera));
        setGlobalOffset(pCameraMan, getGlobalOffset(pCamera));
        setLocalOffset(pCameraMan, getLocalOffset(pCamera));
        setFovy(pCameraMan, getFovy(pCamera));
        setRoll(pCameraMan, getRoll(pCamera));
    }
};  // namespace CameraLocalUtil

