#include "Game/Camera/CameraTower.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sAttenuation = 0.05f;
    const f32 sRoundAddition = 0.08f;
    const f32 sRoundInterval = FloatUtil::PI() / 4.0f;

    // The tower works in zone space: the watch point goes in through rInv, the finished pose comes out through rMtx.
    void toZoneWatchPos(Camera* pCamera, const Pos3f& rInv) {
        Vec3f watchPoint = CameraLocalUtil::getWatchPos(pCamera);
        rInv.mult(watchPoint, watchPoint);
        CameraLocalUtil::setWatchPos(pCamera, watchPoint);
    }

    void toWorldPose(Camera* pCamera, const Pos3f& rMtx) {
        Vec3f pos = CameraLocalUtil::getPos(pCamera);
        Vec3f watchPos = CameraLocalUtil::getWatchPos(pCamera);
        Vec3f up = CameraLocalUtil::getUpVec(pCamera);
        Vec3f watchUp = CameraLocalUtil::getWatchUpVec(pCamera);

        rMtx.mult(pos, pos);
        rMtx.mult(watchPos, watchPos);
        rMtx.mult33(up);
        rMtx.mult33(watchUp);

        CameraLocalUtil::setPos(pCamera, pos);
        CameraLocalUtil::setWatchPos(pCamera, watchPos);
        CameraLocalUtil::setUpVec(pCamera, up);
        CameraLocalUtil::setWatchUpVec(pCamera, watchUp);
    }
};  // namespace

CameraTower::CameraTower(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 1.0f, 0.0f), mAngleX(), mAngleYRoundSpeed(), mDist(2000.0f), mPos(0.0f, 0.0f, 0.0f),
      mIsRounding(), mRoundIntervalNum(), mAngleYSpeed(), mIsRoundLeftValid(), mIsRoundRightValid() {
    lumacore::setClass(this, lumacore::kClassId< CameraTower >);
    createVPanObj();
}

void CameraTower::reset() {
    mVPan->resetJump();
    mVPan->calcWatchPos(nullptr);

    Pos3f mtx = mZoneMatrix;
    Pos3f inv = mtx;
    inv.invert(inv);

    ::toZoneWatchPos(this, inv);
    calcIdealPos();
    mPos.set(CameraLocalUtil::getPos(this));
    ::toWorldPose(this, mtx);

    mVPan->reset();
}

CameraTargetObj* CameraTower::calc() {
    mVPan->updateJump();
    mVPan->calcWatchPos(nullptr);

    Pos3f mtx = mZoneMatrix;
    Pos3f inv = mtx;
    inv.invert(inv);

    ::toZoneWatchPos(this, inv);
    calcIdealPos();
    arrangeRound();
    ::toWorldPose(this, mtx);

    mVPan->arrange();

    Vec3f pos = CameraLocalUtil::getPos(this);
    inv.mult(pos, pos);
    mPos.y = pos.y;

    return CameraLocalUtil::getTarget(this);
}

void CameraTower::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, general->mAxis, general->mDist, Vec2f(general->mAngleB, MR::pi() * (1.0f - general->mAngleA) * 0.5f));
}

bool CameraTower::isEnableToRoundLeft() const {
    return isUpsideDownByRoll() ? mIsRoundRightValid : mIsRoundLeftValid;
}

bool CameraTower::isEnableToRoundRight() const {
    return isUpsideDownByRoll() ? mIsRoundLeftValid : mIsRoundRightValid;
}

void CameraTower::calcIdealPos() {
    Vec3f pos;
    if (!CameraLocalUtil::calcTowerIdealPos(&pos, &mAxis, mWPoint, CameraLocalUtil::getWatchPos(this), mAngleX, mDist)) {
        return;
    }

    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, Vec3f(0.0f, 1.0f, 0.0f));
}

void CameraTower::arrangeRoundingPose() {
    Vec3f pos = CameraLocalUtil::getPos(this);
    pos.y = CameraLocalUtil::getWatchPos(this).y;

    f32 length, angleX, angleY;
    MR::crossToPolar(CameraLocalUtil::getWatchPos(this), pos, &length, &angleX, &angleY);

    if (!CameraLocalUtil::approachRoundAngle(&mAngleYSpeed, mRoundIntervalNum * ::sRoundInterval, ::sRoundAddition)) {
        mIsRounding = false;
    }

    Vec3f newPos;
    f32 newAngleY = angleY + mAngleYSpeed;
    MR::polarToCross(CameraLocalUtil::getWatchPos(this), &newPos, length, angleX, newAngleY);
    newPos.y = CameraLocalUtil::getPos(this).y;
    CameraLocalUtil::setPos(this, newPos);
}

void CameraTower::arrangeHorizontalPose() {
    Vec3f pos = CameraLocalUtil::getPos(this);
    Vec3f idealPos = mPos;
    idealPos.y = CameraLocalUtil::getWatchPos(this).y;
    pos.y = CameraLocalUtil::getWatchPos(this).y;

    f32 idealLength, idealAngleX, idealAngleY;
    MR::crossToPolar(CameraLocalUtil::getWatchPos(this), idealPos, &idealLength, &idealAngleX, &idealAngleY);

    f32 length, angleX, angleY;
    MR::crossToPolar(CameraLocalUtil::getWatchPos(this), pos, &length, &angleX, &angleY);

    f32 roundAngle = MR::diffRadian(angleY, idealAngleY);

    s32 divNum = (roundAngle < 0.0f ? -1 : 1) * (s32)(((::sRoundInterval / 2) + MR::abs(roundAngle)) / ::sRoundInterval);
    s32 maxDivNum = (mAngleYRoundSpeed + (::sRoundInterval / 2)) / ::sRoundInterval;

    mRoundIntervalNum = divNum;
    mIsRoundLeftValid = mRoundIntervalNum > -maxDivNum;
    mIsRoundRightValid = mRoundIntervalNum < maxDivNum;

    bool left, right;
    if (isUpsideDownByRoll()) {
        left = CameraLocalUtil::testCameraPadTriggerRoundRight() && mIsRoundLeftValid;
        right = CameraLocalUtil::testCameraPadTriggerRoundLeft() && mIsRoundRightValid;
    } else {
        left = CameraLocalUtil::testCameraPadTriggerRoundLeft() && mIsRoundLeftValid;
        right = CameraLocalUtil::testCameraPadTriggerRoundRight() && mIsRoundRightValid;
    }

    f32 newAngleY;
    if (CameraLocalUtil::tryCameraReset()) {
        roundAngle *= 0.7f;
        newAngleY = angleY + roundAngle;
    } else {
        if (left) {
            mIsRounding = true;
            mRoundIntervalNum--;
            mAngleYSpeed = roundAngle;
            newAngleY = angleY + roundAngle;
        } else if (right) {
            mIsRounding = true;
            mRoundIntervalNum++;
            mAngleYSpeed = roundAngle;
            newAngleY = angleY + roundAngle;
        } else {
            if (MR::abs(roundAngle) > mAngleYRoundSpeed) {
                if (roundAngle < 0.0f) {
                    newAngleY = angleY - mAngleYRoundSpeed;
                } else {
                    newAngleY = angleY + mAngleYRoundSpeed;
                }
            } else {
                newAngleY = angleY + roundAngle;
            }
        }
    }

    Vec3f newPos;
    f32 newAngleX = MR::blendAngle(angleX, idealAngleX, ::sAttenuation);
    f32 newLength = idealLength * (1.0f - ::sAttenuation) + length * ::sAttenuation;
    MR::polarToCross(CameraLocalUtil::getWatchPos(this), &newPos, newLength, newAngleX, newAngleY);
    newPos.y = CameraLocalUtil::getPos(this).y;
    CameraLocalUtil::setPos(this, newPos);
}

bool CameraTower::isUpsideDownByRoll() const {
    return MR::isOppositeDirectionRadian(CameraLocalUtil::getRoll(this), -MR::pi());
}

void CameraTower::arrangeRound() {
    if (mIsRounding) {
        arrangeRoundingPose();
    } else {
        arrangeHorizontalPose();
    }

    mPos.set(CameraLocalUtil::getPos(this));
}
