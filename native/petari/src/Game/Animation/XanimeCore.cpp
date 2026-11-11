#include "Game/Animation/XanimeCore.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Render/Model.hpp"
#include <nebula_geometry.hpp>
#include <nebula_module.h>

namespace {
    // Divides each rotation row of pMtx by the matching component of rScale where it is not 1.
    void divideRowsByScale(MtxPtr pMtx, const Vec& rScale) {
        if (rScale.x != 1.0f) {
            f32 inverseScale = 1.0f / rScale.x;
            pMtx[0][0] *= inverseScale;
            pMtx[0][1] *= inverseScale;
            pMtx[0][2] *= inverseScale;
        }

        if (rScale.y != 1.0f) {
            f32 inverseScale = 1.0f / rScale.y;
            pMtx[1][0] *= inverseScale;
            pMtx[1][1] *= inverseScale;
            pMtx[1][2] *= inverseScale;
        }

        if (rScale.z != 1.0f) {
            f32 inverseScale = 1.0f / rScale.z;
            pMtx[2][0] *= inverseScale;
            pMtx[2][1] *= inverseScale;
            pMtx[2][2] *= inverseScale;
        }
    }

    void addTranslation(MtxPtr pMtx, const Vec3f& rTranslate) {
        pMtx[0][3] += rTranslate.x;
        pMtx[1][3] += rTranslate.y;
        pMtx[2][3] += rTranslate.z;
    }

    // Adds +0 to the translation, turning a -0 into +0 (bit identity of the matrices).
    void addZeroTranslation(MtxPtr pMtx) {
        pMtx[0][3] += 0.0f;
        pMtx[1][3] += 0.0f;
        pMtx[2][3] += 0.0f;
    }

    void setTranslation(MtxPtr pMtx, const Vec3f& rTranslate) {
        pMtx[0][3] = rTranslate.x;
        pMtx[1][3] = rTranslate.y;
        pMtx[2][3] = rTranslate.z;
    }

    // Sets the model's scale flag for `joint` and applies rScale to pMtx unless it is 1.
    void applyScale(Model* pModel, u32 joint, MtxPtr pMtx, const Vec3f& rScale) {
        if (rScale.x == 1.0f && rScale.y == 1.0f && rScale.z == 1.0f) {
            pModel->setScaleFlag(joint, 1);
        } else {
            pModel->setScaleFlag(joint, 0);
            NebulaMtxApplyScale(pMtx, pMtx, rScale.x, rScale.y, rScale.z);
        }
    }

    void setIdentityRotation(Quaternion* pRotation) {
        pRotation->z = 0.0f;
        pRotation->y = 0.0f;
        pRotation->x = 0.0f;
        pRotation->w = 1.0f;
    }
}  // namespace

void XanimeTrack::init() {
    mClip = nullptr;
    mWeight = 0.0f;
}

XjointInfo::XjointInfo() {
    mFrozen.mScale.set< f32 >(1.0f, 1.0f, 1.0f);
    mFrozen.mTranslate.x = 0.0f;
    mFrozen.mTranslate.y = 0.0f;
    mFrozen.mTranslate.z = 0.0f;
    setIdentityRotation(&mFrozen.mRotation);
    mLast = mFrozen;
    mSpecialBlend = 1.0f;
    mSpecialBlendStep = 1.0f;
}

XjointTransform::XjointTransform() {
    mJoint = nullptr;
    mParentIndex = NEBULA_NONE32;
    mScale.set(1.0f);
    mExtraScale.set(1.0f);
    mBaseMtx = nullptr;
    mMirrorMtx = nullptr;
    mLocalMtx = nullptr;
    mTranslateOffset.x = 0.0f;
    mTranslateOffset.y = 0.0f;
    mTranslateOffset.z = 0.0f;
    mWorldTranslate.x = 0.0f;
    mWorldTranslate.y = 0.0f;
    mWorldTranslate.z = 0.0f;
}

void XanimeCore::shareJointTransform(const XanimeCore* pOther) {
    mTransformList = pOther->mTransformList;
}

void XanimeCore::enableJointTransform(ModelData* pModelData) {
    mTransformList = new XjointTransform[mJointCount];

    for (u32 i = 0; i < mJointCount; i++) {
        Joint* pJoint = pModelData->getJointAt(i);
        mTransformList[i].mJoint = pJoint;

        if (mTransformList[i].mParentIndex != NEBULA_NONE32) {
            continue;
        }

        // The parent is the earlier joint whose first child this is, or the parent of an earlier sibling.
        for (u32 j = 0; j < mJointCount; j++) {
            if (mTransformList[j].mJoint == nullptr) {
                continue;
            }

            if (mTransformList[j].mJoint->getChild() == pJoint) {
                mTransformList[i].mParentIndex = j;
                break;
            }

            if (mTransformList[j].mJoint->getYounger() == pJoint && mTransformList[j].mParentIndex != NEBULA_NONE32) {
                mTransformList[i].mParentIndex = mTransformList[j].mParentIndex;
                break;
            }
        }
    }
}

void XanimeCore::reconfigJointTransform(ModelData* pModelData) {
    for (u32 i = 0; i < mJointCount; i++) {
        mTransformList[i].mJoint = pModelData->getJointAt(i);
    }
}

void XanimeCore::initMember(u32 trackCount) {
    mInterpoleRatio = 1.0f;
    mPoseBlendRatio = 1.0f;
    mFrameRatio = 0.0f;
    mIsFreezeRequested = false;
    mIsCaptureFrozen = false;
    mCalcMode = CalcMode_Normal;
    mTrackList = new XanimeTrack[trackCount];

    for (u32 i = 0; i < trackCount; i++) {
        mTrackList[i].init();
    }

    setWeight(0, 1.0f);
}

XanimeCore::XanimeCore(u32 trackCount, u32 jointCount, u8 scaleMode) {
    mScaleMode = scaleMode;
    mTrackCount = trackCount;
    mJointCount = jointCount;
    mJointList = new XjointInfo[jointCount];
    mTransformList = nullptr;
    initMember(trackCount);
}

XanimeCore::XanimeCore(u32 trackCount, XanimeCore* pOtherCore) {
    mTrackCount = trackCount;
    mJointCount = pOtherCore->mJointCount;
    mScaleMode = pOtherCore->mScaleMode;
    mJointList = pOtherCore->mJointList;
    mTransformList = pOtherCore->mTransformList;
    initMember(trackCount);
}

void XanimeCore::doFreeze() {
    mIsFreezeRequested = true;
    mInterpoleRatio = 0.0f;
}

void XanimeCore::setBck(u32 index, Clip* pAnm) {
    // A clip made for another skeleton: joint j reads tracks j*9 on, with nothing marking the clip's end, so a short
    // clip walked what lies past it as keys (a model shaking, or standing off the ground, by junk translations).
    // Such a clip does not play: the model keeps its pose and the log names the mismatch.
    if (pAnm != nullptr && pAnm->jointCapacity() != mJointCount) {
        nebula_log("Xanime: a clip of %u joints set on a model of %u; it does not play\n",
                   static_cast< unsigned >(pAnm->jointCapacity()), static_cast< unsigned >(mJointCount));
        mTrackList[index].mClip = nullptr;
        return;
    }
    mTrackList[index].mClip = pAnm;
}

void XanimeCore::setWeight(u8 idx, f32 weight) {
    mTrackList[idx].mWeight = weight;
}

f32 XanimeCore::sumTrackWeight() const {
    f32 totalWeight = 0.0f;

    for (s32 i = 0; i < mTrackCount; i++) {
        if (mTrackList[i].mClip != nullptr) {
            totalWeight += mTrackList[i].mWeight;
        }
    }

    return totalWeight;
}

// Adds each weighted track's scale and translation into the outputs and slerps the rotation by running weight.
void XanimeCore::blendTracks(u32 joint, f32 totalWeight, Vec3f* pScale, Vec3f* pTranslate, Quaternion* pRotation) const {
    TransformInfo transform;
    f32 accumulatedWeight = 0.0f;
    f32 inverseWeight = 1.0f / totalWeight;

    for (s32 i = 0; i < mTrackCount; i++) {
        const XanimeTrack* pTrack = &mTrackList[i];

        if (pTrack->mClip != nullptr && pTrack->mWeight != 0.0f) {
            pTrack->mClip->getTransform(joint, &transform);
            Quaternion trackRotation;
            eulerRadiansToQuat(transform.mRotation.x, transform.mRotation.y, transform.mRotation.z, &trackRotation);

            f32 weight = inverseWeight * pTrack->mWeight;
            *pScale += reinterpret_cast< const Vec3f& >(transform.mScale) * weight;
            *pTranslate += reinterpret_cast< const Vec3f& >(transform.mTranslate) * weight;
            accumulatedWeight += weight;
            NebulaQuatLerp(pRotation, &trackRotation, weight / accumulatedWeight, pRotation);
        }
    }
}

// Blends from the frozen pose by `blend` (when below 1), then from last frame's pose by mPoseBlendRatio, and stores the result as last.
void XanimeCore::blendWithFrozenAndLast(u32 joint, f32 blend, Vec3f* pScale, Vec3f* pTranslate, Quaternion* pRotation) {
    XjointInfo& rJoint = mJointList[joint];

    if (blend < 1.0f) {
        *pScale = rJoint.mFrozen.mScale * (1.0f - blend) + *pScale * blend;
        *pTranslate = rJoint.mFrozen.mTranslate * (1.0f - blend) + *pTranslate * blend;
        NebulaQuatLerp(&rJoint.mFrozen.mRotation, pRotation, blend, pRotation);
    }

    if (mPoseBlendRatio != 1.0f) {
        *pScale = rJoint.mLast.mScale * (1.0f - mPoseBlendRatio) + *pScale * mPoseBlendRatio;
        *pTranslate = rJoint.mLast.mTranslate * (1.0f - mPoseBlendRatio) + *pTranslate * mPoseBlendRatio;
        NebulaQuatLerp(&rJoint.mLast.mRotation, pRotation, mPoseBlendRatio, pRotation);
    }

    rJoint.mLast.mScale = *pScale;
    rJoint.mLast.mTranslate = *pTranslate;
    rJoint.mLast.mRotation = *pRotation;
}

void XanimeCore::captureFrozen(u32 joint) {
    if (mIsCaptureFrozen) {
        mJointList[joint].mFrozen = mJointList[joint].mLast;
    }
}

void XanimeCore::calcBlend(Vec3f* pScale, Vec3f* pTranslate) {
    u32 joint = Skeleton::getJoint()->getJntNo();
    MtxPtr pMtx = Skeleton::mModel->getAnmMtx(joint);
    Quaternion rotation;
    pScale->z = 0.0f;
    pScale->y = 0.0f;
    pScale->x = 0.0f;
    pTranslate->z = 0.0f;
    pTranslate->y = 0.0f;
    pTranslate->x = 0.0f;
    setIdentityRotation(&rotation);

    f32 totalWeight = sumTrackWeight();

    if (totalWeight == 0.0f) {
        *pScale = mJointList[joint].mLast.mScale;
        *pTranslate = mJointList[joint].mLast.mTranslate;
        NebulaMtxQuat(pMtx, &mJointList[joint].mLast.mRotation);
        return;
    }

    blendTracks(joint, totalWeight, pScale, pTranslate, &rotation);
    captureFrozen(joint);
    blendWithFrozenAndLast(joint, mInterpoleRatio, pScale, pTranslate, &rotation);
    NebulaMtxQuat(pMtx, &rotation);
}

void XanimeCore::calcSingle(Vec3f* pScale, Vec3f* pTranslate) {
    u32 joint = Skeleton::getJoint()->getJntNo();
    MtxPtr pMtx = Skeleton::mModel->getAnmMtx(joint);
    XjointInfo& rJoint = mJointList[joint];
    TransformInfo transform;
    Quaternion rotation;

    if (mTrackList[0].mClip == nullptr) {
        *pScale = rJoint.mLast.mScale;
        *pTranslate = rJoint.mLast.mTranslate;
        NebulaMtxQuat(pMtx, &rJoint.mLast.mRotation);
        return;
    }

    mTrackList[0].mClip->getTransform(joint, &transform);
    eulerRadiansToQuat(transform.mRotation.x, transform.mRotation.y, transform.mRotation.z, &rotation);
    *pScale = reinterpret_cast< const Vec3f& >(transform.mScale);
    *pTranslate = reinterpret_cast< const Vec3f& >(transform.mTranslate);

    captureFrozen(joint);

    f32 blend = mInterpoleRatio;
    if (blend < 1.0f) {
        MR::vecBlend(rJoint.mFrozen.mScale, *pScale, pScale, blend);
        MR::vecBlend(rJoint.mFrozen.mTranslate, *pTranslate, pTranslate, blend);
        NebulaQuatLerp(&rJoint.mFrozen.mRotation, &rotation, blend, &rotation);
    }

    rJoint.mLast.mScale = *pScale;
    rJoint.mLast.mTranslate = *pTranslate;
    rJoint.mLast.mRotation = rotation;
    NebulaMtxQuat(pMtx, &rotation);
}

// Updates mLast only; the joint's matrix is left for calcScaleBlendSpecial.
void XanimeCore::calcBlendSpecial() {
    u32 joint = Skeleton::getJoint()->getJntNo();
    Quaternion rotation;
    Vec3f scale(0.0f, 0.0f, 0.0f);
    Vec3f translate(0.0f, 0.0f, 0.0f);
    setIdentityRotation(&rotation);

    f32 totalWeight = sumTrackWeight();

    if (totalWeight == 0.0f) {
        return;
    }

    blendTracks(joint, totalWeight, &scale, &translate, &rotation);
    captureFrozen(joint);

    XjointInfo& rJoint = mJointList[joint];
    f32 blend = mInterpoleRatio;

    if (rJoint.mSpecialBlend != 1.0f) {
        rJoint.mSpecialBlend += rJoint.mSpecialBlendStep;
        rJoint.mSpecialBlend = MR::clamp(rJoint.mSpecialBlend, 0.0f, 1.0f);
        blend = rJoint.mSpecialBlend;
    }

    blendWithFrozenAndLast(joint, blend, &scale, &translate, &rotation);
}

void XanimeCore::updateFrame() {
    for (u32 i = 0; i < mTrackCount; i++) {
        Clip* pClip = mTrackList[i].mClip;
        if (pClip != nullptr) {
            pClip->setFrame(mFrameRatio * pClip->getFrameMax());
        }
    }

    mIsCaptureFrozen = mIsFreezeRequested;
    mIsFreezeRequested = false;
}

// The parent's actor scale is divided back out so it does not carry to this joint's rotation.
void XanimeCore::divideParentTransformScale(MtxPtr pMtx, u32 joint) const {
    if (mTransformList != nullptr && mTransformList[joint].mParentIndex != NEBULA_NONE32) {
        divideRowsByScale(pMtx, mTransformList[mTransformList[joint].mParentIndex].mScale);
    }
}

void XanimeCore::applyLocalMatrices(MtxPtr pMtx, u32 joint) const {
    if (mTransformList == nullptr) {
        return;
    }

    if (mTransformList[joint].mBaseMtx != nullptr) {
        NebulaMtxConcat(pMtx, mTransformList[joint].mBaseMtx, pMtx);
    }

    if (mTransformList[joint].mLocalMtx != nullptr) {
        NebulaMtxConcat(pMtx, mTransformList[joint].mLocalMtx, pMtx);
    }
}

Vec3f XanimeCore::calcTransformScale(const Vec3f& rScale, u32 joint) const {
    Vec3f scale(rScale);

    if (mTransformList != nullptr) {
        scale.x *= mTransformList[joint].mScale.x * mTransformList[joint].mExtraScale.x;
        scale.y *= mTransformList[joint].mScale.y * mTransformList[joint].mExtraScale.y;
        scale.z *= mTransformList[joint].mScale.z * mTransformList[joint].mExtraScale.z;
    }

    return scale;
}

void XanimeCore::calcScaleBlendMaya(const Vec3f& rScale, const Vec3f& rTranslate) {
    Joint* pJoint = Skeleton::getJoint();
    u32 joint = pJoint->getJntNo();
    MtxPtr pMtx = Skeleton::mModel->getAnmMtx(joint);

    applyLocalMatrices(pMtx, joint);

    if (mTransformList != nullptr && mTransformList[joint].mParentIndex != NEBULA_NONE32 &&
        mTransformList[mTransformList[joint].mParentIndex].mLocalMtx != nullptr) {
        Mtx inverse;
        NebulaMtxInverse(mTransformList[mTransformList[joint].mParentIndex].mLocalMtx, inverse);
        NebulaMtxConcat(inverse, pMtx, pMtx);
    }

    setTranslation(pMtx, rTranslate);

    if (mTransformList != nullptr) {
        addTranslation(pMtx, mTransformList[joint].mTranslateOffset);
    }

    applyScale(Skeleton::mModel, joint, pMtx, calcTransformScale(rScale, joint));
    divideParentTransformScale(pMtx, joint);

    if (pJoint->getScaleCompensate() == 1) {
        divideRowsByScale(pMtx, Skeleton::mParentS);
    }

    NebulaMtxConcat(Skeleton::mCurrentMtx, pMtx, Skeleton::mCurrentMtx);

    if (mTransformList != nullptr && mTransformList[joint].mMirrorMtx != nullptr) {
        Vec3f translate;
        MR::extractMtxTrans(Skeleton::mCurrentMtx, &translate);
        MR::setMtxTrans(Skeleton::mCurrentMtx, 0.0f, 0.0f, 0.0f);
        NebulaMtxConcat(mTransformList[joint].mMirrorMtx, Skeleton::mCurrentMtx, Skeleton::mCurrentMtx);
        MR::setMtxTrans(Skeleton::mCurrentMtx, translate.x, translate.y, translate.z);
    }

    if (mTransformList != nullptr) {
        addTranslation(Skeleton::mCurrentMtx, mTransformList[joint].mWorldTranslate);
    }

    NebulaMtxCopy(Skeleton::mCurrentMtx, pMtx);

    if (mTransformList != nullptr) {
        addZeroTranslation(pMtx);
    }

    Skeleton::mParentS.x = rScale.x;
    Skeleton::mParentS.y = rScale.y;
    Skeleton::mParentS.z = rScale.z;
}

void XanimeCore::calcScaleBlendMayaNoTransform(const Vec3f& rScale, const Vec3f& rTranslate) {
    Joint* pJoint = Skeleton::getJoint();
    u32 joint = pJoint->getJntNo();
    MtxPtr pMtx = Skeleton::mModel->getAnmMtx(joint);

    setTranslation(pMtx, rTranslate);
    applyScale(Skeleton::mModel, joint, pMtx, rScale);

    if (pJoint->getScaleCompensate() == 1) {
        divideRowsByScale(pMtx, Skeleton::mParentS);
    }

    NebulaMtxConcat(Skeleton::mCurrentMtx, pMtx, Skeleton::mCurrentMtx);
    NebulaMtxCopy(Skeleton::mCurrentMtx, pMtx);

    Skeleton::mParentS.x = rScale.x;
    Skeleton::mParentS.y = rScale.y;
    Skeleton::mParentS.z = rScale.z;
}

void XanimeCore::calcScaleBlendSI(const Vec3f& rScale, const Vec3f& rTranslate) {
    Vec& rCurrentScale = Skeleton::mCurrentS;
    Joint* pJoint = Skeleton::getJoint();
    u32 joint = pJoint->getJntNo();
    MtxPtr pMtx = Skeleton::mModel->getAnmMtx(joint);

    applyLocalMatrices(pMtx, joint);
    applyScale(Skeleton::mModel, joint, pMtx, calcTransformScale(rScale, joint));
    divideParentTransformScale(pMtx, joint);

    if (pJoint->getScaleCompensate() == 1) {
        divideRowsByScale(pMtx, Skeleton::mParentS);
    }

    pMtx[0][3] = rTranslate.x * rCurrentScale.x;
    pMtx[1][3] = rTranslate.y * rCurrentScale.y;
    pMtx[2][3] = rTranslate.z * rCurrentScale.z;

    if (mTransformList != nullptr) {
        addTranslation(pMtx, mTransformList[joint].mTranslateOffset);
    }

    NebulaMtxConcat(Skeleton::mCurrentMtx, pMtx, Skeleton::mCurrentMtx);

    rCurrentScale.x *= rScale.x;
    rCurrentScale.y *= rScale.y;
    rCurrentScale.z *= rScale.z;

    if (rCurrentScale.x == 1.0f && rCurrentScale.y == 1.0f && rCurrentScale.z == 1.0f) {
        Skeleton::mModel->setScaleFlag(joint, 1);
        NebulaMtxCopy(Skeleton::mCurrentMtx, pMtx);
    } else {
        Skeleton::mModel->setScaleFlag(joint, 0);
        NebulaMtxApplyScale(Skeleton::mCurrentMtx, pMtx, rCurrentScale.x, rCurrentScale.y, rCurrentScale.z);
        pMtx[0][3] = Skeleton::mCurrentMtx[0][3];
        pMtx[1][3] = Skeleton::mCurrentMtx[1][3];
        pMtx[2][3] = Skeleton::mCurrentMtx[2][3];
    }

    if (mTransformList != nullptr) {
        addZeroTranslation(pMtx);
    }
}

void XanimeCore::calcScaleBlendBasic(const Vec3f& rScale, const Vec3f& rTranslate) {
    Joint* pJoint = Skeleton::getJoint();
    u32 joint = pJoint->getJntNo();
    MtxPtr pMtx = Skeleton::mModel->getAnmMtx(joint);

    Vec3f& rCurrentScale = reinterpret_cast< Vec3f& >(Skeleton::mCurrentS);
    setTranslation(pMtx, rTranslate);

    rCurrentScale.x *= rScale.x;
    rCurrentScale.y *= rScale.y;
    rCurrentScale.z *= rScale.z;

    if (rCurrentScale.x == 1.0f && rCurrentScale.y == 1.0f && rCurrentScale.z == 1.0f) {
        Skeleton::mModel->setScaleFlag(joint, 1);
    } else {
        Skeleton::mModel->setScaleFlag(joint, 0);
        NebulaMtxApplyScale(pMtx, pMtx, rScale.x, rScale.y, rScale.z);
    }

    NebulaMtxConcat(Skeleton::mCurrentMtx, pMtx, Skeleton::mCurrentMtx);
    NebulaMtxCopy(Skeleton::mCurrentMtx, pMtx);
}

void XanimeCore::calcScaleBlendSpecial() {
    u32 joint = Skeleton::getJoint()->getJntNo();
    MtxPtr pMtx = Skeleton::mModel->getAnmMtx(joint);
    Vec3f scale;
    Vec3f translate;
    scale = mJointList[joint].mLast.mScale;
    translate = mJointList[joint].mLast.mTranslate;
    NebulaMtxQuat(pMtx, &mJointList[joint].mLast.mRotation);
    calcScaleBlendMaya(scale, translate);
}

// Copies this core's last pose of `joint` and its descendants into pOther's frozen pose, blended away over `duration` frames.
void XanimeCore::freezeCopy(ModelData* pModelData, XanimeCore* pOther, u32 joint, u32 duration) {
    for (Joint* pChild = pModelData->getJointAt(joint)->getChild(); pChild != nullptr; pChild = pChild->getYounger()) {
        freezeCopy(pModelData, pOther, pChild->getJntNo(), duration);
    }

    pOther->mJointList[joint].mFrozen = mJointList[joint].mLast;
    pOther->mJointList[joint].mSpecialBlend = 0.0f;
    f32 rate = 1.0f;

    if (duration != 0) {
        rate = 1.0f / duration;
    }

    pOther->mJointList[joint].mSpecialBlendStep = rate;
}

void XanimeCore::initT(ModelData* pModelData) {
    for (u32 i = 0; i < mJointCount; i++) {
        const TransformInfo& rTransform = pModelData->getJointAt(i)->getTransformInfo();
        Quaternion rotation;
        eulerRadiansToQuat(rTransform.mRotation.x, rTransform.mRotation.y, rTransform.mRotation.z, &rotation);
        mJointList[i].mFrozen.mScale = reinterpret_cast< const Vec3f& >(rTransform.mScale);
        mJointList[i].mFrozen.mTranslate = reinterpret_cast< const Vec3f& >(rTransform.mTranslate);
        mJointList[i].mFrozen.mRotation = rotation;
        mJointList[i].mLast = mJointList[i].mFrozen;
    }
}

XtransformInfo::XtransformInfo() {
    mScale.zero();
    mTranslate.zero();
    setIdentityRotation(&mRotation);
}

void XanimeCore::calc() {
    Skeleton::mCurrentMtxCalc = this;

    if (mCalcMode == CalcMode_BlendSpecial) {
        calcBlendSpecial();
        return;
    }

    if (mCalcMode == CalcMode_ScaleBlendSpecial) {
        calcScaleBlendSpecial();
        return;
    }

    Vec3f scale;
    Vec3f translate;

    if (mTrackCount == 1) {
        calcSingle(&scale, &translate);
    } else {
        calcBlend(&scale, &translate);
    }

    switch (mScaleMode) {
    case ScaleMode_SoftImage:
        calcScaleBlendSI(scale, translate);
        break;
    case ScaleMode_Basic:
        if (mTransformList == nullptr) {
            calcScaleBlendBasic(scale, translate);
            break;
        }
        [[fallthrough]];
    case ScaleMode_Maya:
        if (mTransformList != nullptr) {
            calcScaleBlendMaya(scale, translate);
        } else {
            calcScaleBlendMayaNoTransform(scale, translate);
        }
        break;
    }
}

void XanimeCore::init(const Vec& rScale, const Mtx& rMtx) {
    switch (mScaleMode) {
    case ScaleMode_Basic:
    case ScaleMode_SoftImage:
    case ScaleMode_Maya:
        Skeleton::initMaya(rScale, rMtx);
        break;
    }
}
