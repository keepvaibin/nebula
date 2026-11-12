#include "Game/Camera/CameraTowerPos.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sAttenuation = 0.05f;
};  // namespace

CameraTowerPos::CameraTowerPos(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 1.0f, 0.0f), mAngleYMaxSpeed(), mAngleX(), mDist(1000.0f), mAngleXRate(0.5f),
      mPos(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraTowerPos >);
}

void CameraTowerPos::reset() {
    calcIdealPose(false);
}

CameraTargetObj* CameraTowerPos::calc() {
    calcIdealPose(true);

    return CameraLocalUtil::getTarget(this);
}

void CameraTowerPos::calcIdealPose(bool arrange) {
    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);

    Pos3f inv;
    inv.set(mZoneMatrix);
    inv.invert(inv);
    inv.mult(watchPos, watchPos);

    Vec3f pos;
    if (!CameraLocalUtil::calcTowerIdealPos(&pos, &mAxis, mWPoint, watchPos, mAngleX, mDist)) {
        return;
    }

    Vec3f up(0.0f, 1.0f, 0.0f);

    if (arrange) {
        arrangeHorizontalPose(watchPos, pos);
        arrangeWatchPos(watchPos, pos);
    }

    mPos.set(pos);

    Pos3f mtx;
    mtx.set(mZoneMatrix);
    mtx.mult(watchPos, watchPos);
    mtx.mult(pos, pos);
    mtx.mult33(up, up);

    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);
}

void CameraTowerPos::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mWPoint, general->mAxis, general->mUp.x, Vec2f(general->mAngleB, MR::pi() * (1.0f - general->mAngleA) * 0.5f), general->mUp.y);
}

void CameraTowerPos::arrangeHorizontalPose(const Vec3f& rWatchPos, Vec3f& rPos) {
    Vec3f pos = rPos;
    Vec3f idealPos = mPos;
    idealPos.y = rWatchPos.y;
    pos.y = rWatchPos.y;

    f32 idealLength, idealAngleX, idealAngleY;
    MR::crossToPolar(rWatchPos, idealPos, &idealLength, &idealAngleX, &idealAngleY);

    f32 length, angleX, angleY;
    MR::crossToPolar(rWatchPos, pos, &length, &angleX, &angleY);

    f32 roundAngle = MR::diffRadian(angleY, idealAngleY);

    f32 newAngleY;
    if (CameraLocalUtil::tryCameraReset()) {
        roundAngle *= 0.7f;
        newAngleY = angleY + roundAngle;
    } else {
        if (MR::abs(roundAngle) > mAngleYMaxSpeed) {
            if (roundAngle < 0.0f) {
                newAngleY = angleY - mAngleYMaxSpeed;
            } else {
                newAngleY = angleY + mAngleYMaxSpeed;
            }
        } else {
            newAngleY = angleY + roundAngle;
        }
    }

    Vec3f newPos;
    f32 newAngleX = MR::blendAngle(angleX, idealAngleX, ::sAttenuation);
    f32 newLength = idealLength * (1.0f - ::sAttenuation) + length * ::sAttenuation;
    MR::polarToCross(rWatchPos, &newPos, newLength, newAngleX, newAngleY);
    newPos.y = rPos.y;
    rPos.set(newPos);
}

void CameraTowerPos::arrangeWatchPos(Vec3f& rWatchPos, const Vec3f& rPos) {
    Vec3f offset = rWatchPos - rPos;
    Vec3f idealOffset = mWPoint - rPos;

    Vec3f front = offset;
    front.y = 0.0f;
    MR::normalizeOrZero(&front);

    Vec3f idealFront = idealOffset;
    idealFront.y = 0.0f;

    idealFront = front * front.dot(idealFront);
    idealOffset.x = idealFront.x;
    idealOffset.z = idealFront.z;

    f32 angleDiff = offset.angle(idealOffset);
    f32 fovAngle = CameraLocalUtil::getFovy(this) * MR::pi() / 180.0f * 0.5f;

    if (angleDiff > fovAngle * mAngleXRate) {
        f32 length, angleX, angleY;
        MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), offset, &length, &angleX, &angleY);

        f32 idealLength, idealAngleX, idealAngleY;
        MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), idealOffset, &idealLength, &idealAngleX, &idealAngleY);

        if (angleDiff > fovAngle * mAngleXRate * 2.0f) {
            if (angleX > idealAngleX) {
                angleX -= fovAngle * mAngleXRate;
            } else {
                angleX += fovAngle * mAngleXRate;
            }
        } else {
            if (angleX > idealAngleX) {
                angleX += (idealAngleX - angleX) + fovAngle * mAngleXRate;
            } else {
                angleX += (idealAngleX - angleX) - fovAngle * mAngleXRate;
            }
        }

        MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &offset, length, angleX, angleY);
        rWatchPos.set(offset + rPos);
    }
}
