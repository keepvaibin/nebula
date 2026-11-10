#include "Game/Camera/CameraAnim.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 180.0f);

namespace {
    // The cooked camera tracks store a key's slopes per authored frame of 30 a second (the cooker reads them the same way);
    // key times are in those frames.
    NEBULA_TUNABLE(f32, cKeySlopeUnit, 30.0f);

    // The first key at or before the key (a binary search; one before the first when the key is before it).
    u32 searchKey(const f32* pValues, f32 key, u32 count, u32 stride) {
        u32 low = 0;
        u32 high = count;

        while (low < high) {
            u32 middle = (low + high) / 2;

            if (pValues[middle * stride] <= key) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }

        return low - 1;
    }

    bool isClip(const u8* pFile) {
        const CamAnmHeader* pHeader = reinterpret_cast< const CamAnmHeader* >(pFile);
        return pHeader->mMagic[0] == 'N' && pHeader->mMagic[1] == 'C' && pHeader->mMagic[2] == 'A' && pHeader->mMagic[3] == 'M';
    }
}  // namespace

f32 CameraAnim::getTrack(u32 trackId, f32 key) const {
    const CamAnmTrack& track = mClip->mTracks[trackId];
    const f32* values = mClip->values() + track.mFirst;

    if (track.mCount == 1) {
        return values[0];
    }

    if (track.mStride == 1) {
        u32 keyFloor = key;
        f32 rate = MR::clamp(key - keyFloor, 0.0f, 1.0f);

        if (keyFloor < track.mCount - 1) {
            return (1.0f - rate) * values[keyFloor] + rate * values[keyFloor + 1];
        }

        return values[track.mCount - 1];
    }

    // A key's slope out is its last word; the next key's slope in is its third.
    const u32 stride = track.mStride;
    const f32* pKey = values + searchKey(values, key, track.mCount, stride) * stride;
    return hermiteInterpolation(key, pKey[0], pKey[1], pKey[stride - 1] / cKeySlopeUnit, pKey[stride], pKey[stride + 1], pKey[stride + 2] / cKeySlopeUnit);
}

void CameraAnim::getPos(Vec3f* pPos, f32 key) const {
    pPos->set< f32 >(getTrack(CamAnmTrack_PosX, key), getTrack(CamAnmTrack_PosY, key), getTrack(CamAnmTrack_PosZ, key));
}

void CameraAnim::getWatchPos(Vec3f* pWatchPos, f32 key) const {
    pWatchPos->set< f32 >(getTrack(CamAnmTrack_WatchPosX, key), getTrack(CamAnmTrack_WatchPosY, key), getTrack(CamAnmTrack_WatchPosZ, key));
}

CameraAnim::CameraAnim(const char* pName)
    : Camera(pName), mNrFrames(), mSpeed(1.0f), mCurrentFrame(), mClip(), mIsPaused(), mShownFrame(-1.0f) {
}

void CameraAnim::reset() {
    CameraLocalUtil::setFovy(this, CameraLocalUtil::getFovy(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
    mIsPaused = false;
    mCurrentFrame = 0.0f;
    mShownFrame = -1.0f;
}

bool CameraAnim::isCutTo(f32 key) const {
    if (mShownFrame < 0.0f) {
        return false;
    }
    if (key < mShownFrame) {
        return true;
    }
    // The shot before holds the frames up to a cut's step; the next one starts after it.
    const u32* pCuts = mClip->cuts();
    for (u32 i = 0; i < mClip->cutCount(); i++) {
        const f32 step = static_cast< f32 >(pCuts[i]);
        if (mShownFrame <= step && step < key) {
            return true;
        }
    }
    return false;
}

CameraTargetObj* CameraAnim::calc() {
    Pos3f mtx;
    mtx.identity();
    mtx.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
    mtx.setYDir(CameraLocalUtil::getTarget(this)->getUpVec());
    mtx.setZDir(CameraLocalUtil::getTarget(this)->getFrontVec());
    mtx.setTrans(CameraLocalUtil::getTarget(this)->getPosition());

    if (mNrFrames != 0 && mNrFrames > mCurrentFrame) {
        Vec3f pos;
        getPos(&pos, mCurrentFrame);
        mtx.mult(pos, pos);
        CameraLocalUtil::setPos(this, pos);

        Vec3f watchPos;
        getWatchPos(&watchPos, mCurrentFrame);
        mtx.mult(watchPos, watchPos);
        CameraLocalUtil::setWatchPos(this, watchPos);

        Vec3f up;
        mtx.getYDir(up);
        CameraLocalUtil::setUpVec(this, up);
        CameraLocalUtil::setWatchUpVec(this, up);

        CameraLocalUtil::setRoll(this, getTrack(CamAnmTrack_Twist, mCurrentFrame) * MR::pi() / cNum_calc_f1);
        CameraLocalUtil::setFovy(this, getTrack(CamAnmTrack_Fovy, mCurrentFrame));

        if (isCutTo(mCurrentFrame)) {
            MotionSignal::cameraCut();
        }
        mShownFrame = mCurrentFrame;

        if (!mIsPaused) {
            mCurrentFrame += mSpeed;
        }
    } else {
        const f32 frame = mNrFrames - 1;
        CameraLocalUtil::setRoll(this, getTrack(CamAnmTrack_Twist, frame) * MR::pi() / cNum_calc_f2);
        CameraLocalUtil::setFovy(this, getTrack(CamAnmTrack_Fovy, mNrFrames - 1));
    }

    return nullptr;
}

void CameraAnim::setParam(u8* pFile, f32 speed) {
    loadBin(pFile);
    mSpeed = speed;
}

bool CameraAnim::isAnimEnd() const {
    return mNrFrames == 0 || mCurrentFrame >= mNrFrames;
}

u32 CameraAnim::getAnimFrame(u8* pFile) {
    if (pFile == nullptr || !isClip(pFile)) {
        return 0;
    }

    return reinterpret_cast< const CamAnmHeader* >(pFile)->mNrFrames;
}

void CameraAnim::setParamFromChunk(const CameraParamChunk* pChunk) {
    setParam(pChunk->mGeneralParam->mAnimData, pChunk->mGeneralParam->mDist);
}

bool CameraAnim::loadBin(u8* pFile) {
    if (!isClip(pFile)) {
        return false;
    }

    mClip = reinterpret_cast< const CamAnmHeader* >(pFile);
    mNrFrames = mClip->mNrFrames;
    return true;
}

