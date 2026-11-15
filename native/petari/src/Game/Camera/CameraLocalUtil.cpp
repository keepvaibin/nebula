#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Camera/CameraRegisterHolder.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/MapUtil.hpp"
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

    MtxPtr getMtxReg(const char* pRegName) {
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

    Vec3f getNormalizedOr(const Vec3f& rVec, const Vec3f& rDefault) {
        Vec3f vec = rVec;
        if (MR::isNearZero(vec)) {
            vec.set(rDefault);
        } else {
            MR::normalize(&vec);
        }

        return vec;
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
        return getCameraDirector()->isEnableToReset() && testCameraPadButtonReset();
    }

    bool tryCameraResetTrigger() {
        return getCameraDirector()->isEnableToReset() && testCameraPadTriggerReset();
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

    void makeGroundWatchPoint(Vec3f* pDst, Camera* pCamera, Vec3f* pLastWatchPoint, bool* pIsFirstFrame) {
        CameraTargetObj* pTarget = getTarget(pCamera);
        Vec3f watchPoint;
        makeWatchPoint(&watchPoint, pCamera, pTarget, 0.1f / 15.0f);

        watchPoint = watchPoint - pTarget->getPosition() + pTarget->getGroundPos();

        if (*pIsFirstFrame) {
            *pIsFirstFrame = false;
        } else {
            Vec3f prevDir = *pLastWatchPoint - pTarget->getPosition();
            f32 prevUp = pTarget->getUpVec().dot(prevDir);

            Vec3f watchDir = watchPoint - pTarget->getPosition();
            f32 newUp = pTarget->getUpVec().dot(watchDir);

            f32 length = newUp * 0.05f + prevUp * 0.95f;

            watchPoint.set(pTarget->getPosition() + pTarget->getUpVec() * length);
        }

        pLastWatchPoint->set(watchPoint);
        pDst->set(watchPoint);
    }

    bool approachRoundAngle(f32* pAngle, f32 target, f32 step) {
        if (target > *pAngle) {
            *pAngle += step;
            if (*pAngle >= target) {
                *pAngle = target;
                return false;
            }
        } else {
            *pAngle -= step;
            if (*pAngle <= target) {
                *pAngle = target;
                return false;
            }
        }

        return true;
    }

    f32 calcRoundTarget(f32* pAngle, bool isRight, f32 roundDiv) {
        f32 roundInterval = MR::toRadian(360.0f / roundDiv);
        s32 divNum = (*pAngle < 0.0f ? -1 : 1) * (s32)(((roundInterval / 2) + MR::abs(*pAngle)) / roundInterval);

        if (isRight) {
            divNum += 1;
            if (divNum > roundDiv / 2) {
                divNum -= roundDiv;
                *pAngle -= MR::pi() * 2.0f;
            }
        } else {
            divNum -= 1;
            if (divNum < -roundDiv / 2) {
                divNum += roundDiv;
                *pAngle += MR::pi() * 2.0f;
            }
        }

        return divNum * roundInterval;
    }

    bool calcTowerIdealPos(Vec3f* pPos, Vec3f* pAxis, const Vec3f& rWPoint, const Vec3f& rWatchPos, f32 angleX, f32 dist) {
        if (MR::isNearZero(*pAxis)) {
            pAxis->set(0.0f, 1.0f, 0.0f);
        } else {
            pAxis->normalize();
        }

        Vec3f front = rWatchPos - rWPoint;
        front.set(front - *pAxis * pAxis->dot(front));
        if (MR::isNearZero(front)) {
            return false;
        }

        MR::normalize(&front);

        Vec3f up(0.0f, 1.0f, 0.0f);
        Vec3f side = front.cross(up);

        Quat4f rot;
        rot.setRotate(side, angleX);
        rot.transform(front);

        pPos->set(front * dist + rWatchPos);
        return true;
    }

    f32 calcFollowFrame(const Camera* pCamera, const Vec3f& rWatchPos, const Vec3f& rGravUp, f32 distMin, f32 distMax, bool isSideReorthogonalized,
                        Vec3f* pFront, Vec3f* pUp, Vec3f* pSide) {
        Vec3f watchFront = rWatchPos - getPos(pCamera);
        f32 dist = watchFront.length();

        if (MR::isNearZero(watchFront)) {
            watchFront.cross(getTarget(pCamera)->getSideVec(), rGravUp);
        } else if (dist < distMin) {
            dist = distMin;
        } else if (dist > distMax) {
            dist = distMax;
        }
        MR::normalize(&watchFront);

        Vec3f camFront = getWatchPos(pCamera) - getPos(pCamera);
        MR::normalize(&camFront);
        Quat4f rotFront;
        rotFront.setRotate(camFront, watchFront);

        pFront->set(watchFront);
        pUp->set(getUpVec(pCamera));
        rotFront.transform(*pUp);
        pSide->cross(*pUp, *pFront);
        *pSide -= rGravUp * rGravUp.dot(*pSide);
        MR::normalize(pSide);

        pUp->cross(*pFront, *pSide);
        if (isSideReorthogonalized) {
            pSide->cross(*pUp, *pFront);
        }

        if (rGravUp.dot(*pUp) < 0.0f) {
            Quat4f upsideDownRot;
            if (pFront->dot(rGravUp) < 0.0f) {
                upsideDownRot.setRotate(*pFront, -rGravUp);
            } else {
                upsideDownRot.setRotate(*pFront, rGravUp);
            }
            upsideDownRot.transform(*pFront);
            upsideDownRot.transform(*pSide);
            upsideDownRot.transform(*pUp);
        }

        return dist;
    }

    f32 updateMoveCollision(s32* pCount, Vec3f* pLastMoveDir, const CameraTargetObj* pTarget, const Vec3f& rPos, const Vec3f& rLine) {
        const f32 cCollideRate = 0.03f;
        const s32 cCollideCountMax = 60;
        const f32 cLastMoveLen = 5.0f;

        f32 lastMoveLen = pTarget->getLastMove().length();
        if (lastMoveLen > cLastMoveLen) {
            pLastMoveDir->set(pTarget->getLastMove());
            MR::normalize(pLastMoveDir);
        }

        if (lastMoveLen > cLastMoveLen && Collision::checkStrikeLineToMap(rPos, rLine, 0, nullptr, nullptr) != 0) {
            (*pCount)++;
            if (*pCount > cCollideCountMax) {
                *pCount = cCollideCountMax;
            }
        } else {
            (*pCount)--;
            if (*pCount < 0) {
                *pCount = 0;
            }
        }

        return *pCount * cCollideRate / cCollideCountMax;
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

    namespace {
        const f32 cSafeWatchDistance = 300.0f;

        // A watch point closer than cSafeWatchDistance is pushed out to it; one on the camera keeps the man's view vector.
        void keepAwayWatchPos(const CameraMan* pCameraMan, Vec3f* pWatchPos, const Vec3f& rPos) {
            Vec3f dir = *pWatchPos - rPos;
            f32 length = dir.length();

            if (length < cSafeWatchDistance) {
                if (length < 1.0f) {
                    pWatchPos->set(rPos + getWatchPos(pCameraMan) - getPos(pCameraMan));
                } else {
                    dir.normalize();
                    pWatchPos->set(rPos + dir * cSafeWatchDistance);
                }
            }
        }

        // An up vector that is zero or near the view direction is the man's up turned onto the new view direction.
        void calcSafeUpVec(const CameraMan* pCameraMan, Vec3f* pUp, const Vec3f& rWatchPos, const Vec3f& rPos) {
            Vec3f camWatchDir = rWatchPos - rPos;
            MR::normalize(&camWatchDir);
            MR::normalizeOrZero(pUp);

            if (MR::isNearZero(*pUp) || MR::abs(camWatchDir.dot(*pUp)) > 0.98f) {
                Vec3f watchDir = getWatchPos(pCameraMan) - getPos(pCameraMan);
                MR::normalize(&watchDir);
                if (MR::abs(camWatchDir.dot(watchDir)) > 0.98f) {
                    pUp->set(getUpVec(pCameraMan));
                } else {
                    Quat4f rot;
                    rot.setRotate(watchDir, camWatchDir);
                    rot.transform(getUpVec(pCameraMan), *pUp);
                }
                recalcUpVec(pUp, camWatchDir);
            }
        }
    }  // namespace

    void calcSafePose(CameraMan* pCameraMan, Camera* pCamera) {
        Vec3f pos = getPos(pCamera);
        Vec3f watchPos = getWatchPos(pCamera);
        Vec3f up = getUpVec(pCamera);

        if (MR::isNan(pos) || MR::isNan(watchPos) || MR::isNan(up)) {
            return;
        }

        keepAwayWatchPos(pCameraMan, &watchPos, pos);
        calcSafeUpVec(pCameraMan, &up, watchPos, pos);

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

