#pragma once

#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include <nebula_geometry.hpp>

class ModelData;
class Clip;

class XtransformInfo {
public:
    XtransformInfo();

    Vec3f mScale;
    Vec3f mTranslate;
    Quaternion mRotation;
};

// Per-joint offsets an actor applies on top of the animation (scale, local matrices, translations).
class XjointTransform {
public:
    XjointTransform();

    void setLocalScale(f32 scale) {
        mScale.set(scale);
    }

    Joint* mJoint;
    // The joint whose child list holds this one, or NEBULA_NONE32.
    u32 mParentIndex;
    // Actor scale; divided back out of the children's matrices.
    Vec3f mScale;
    // A second scale multiplied in with mScale (JointRumbler, the player's arms); children keep it.
    Vec3f mExtraScale;
    // Added to the animated translation (the player's shoulders, arm swing and correction offset).
    Vec3f mTranslateOffset;
    // Added to the concatenated matrix, so children carry it (the player's joint-0 position, Maya scale only).
    Vec3f mWorldTranslate;
    // Concatenated first (the player's tilt, look and correction matrices), or null.
    MtxPtr mBaseMtx;
    // Concatenated after mBaseMtx (MR::setJointTransformLocalMtx), or null.
    MtxPtr mLocalMtx;
    // Applied to the concatenated matrix's rotation, translation kept (the player's mirror mode, Maya scale only), or null.
    MtxPtr mMirrorMtx;
};

class XjointInfo {
public:
    XjointInfo();

    // The pose captured by doFreeze, blended away over XanimeCore::mInterpoleRatio.
    XtransformInfo mFrozen;
    // The pose this joint had last frame.
    XtransformInfo mLast;
    // calcBlendSpecial's per-joint blend (1 = done) and its step per frame, set by freezeCopy.
    f32 mSpecialBlend;
    f32 mSpecialBlendStep;
};

class XanimeTrack {
public:
    XanimeTrack() {
    }

    void init();

    f32 getWeight() const {
        return mWeight;
    }

    Clip* mClip;
    f32 mWeight;
};

class XanimeCore : public MtxCalc {
public:
    enum ScaleMode {
        ScaleMode_Basic = 0,
        ScaleMode_SoftImage = 1,
        ScaleMode_Maya = 2,
    };

    // What calc runs (the player animator switches it around its own model calcs).
    enum CalcMode {
        CalcMode_Normal = 0,
        CalcMode_BlendSpecial = 1,
        CalcMode_ScaleBlendSpecial = 2,
    };

    XanimeCore(u32, u32, u8);
    XanimeCore(u32, XanimeCore*);

    virtual void setWeight(u8, f32) override;
    virtual void init(const Vec&, const Mtx&) override;
    virtual void calc() override;

    void shareJointTransform(const XanimeCore*);
    void doFreeze();
    void setBck(u32, Clip*);
    void calcBlend(Vec3f*, Vec3f*);
    void calcSingle(Vec3f*, Vec3f*);
    void calcBlendSpecial();
    void calcScaleBlendMaya(const Vec3f&, const Vec3f&);
    void calcScaleBlendMayaNoTransform(const Vec3f&, const Vec3f&);
    void calcScaleBlendSI(const Vec3f&, const Vec3f&);
    void calcScaleBlendBasic(const Vec3f&, const Vec3f&);
    void calcScaleBlendSpecial();
    void freezeCopy(ModelData*, XanimeCore*, u32, u32);
    void initT(ModelData*);
    void reconfigJointTransform(ModelData*);
    void updateFrame();

    void enableJointTransform(ModelData*);

    void initMember(u32);

    XjointTransform* getJointTransform(u32 index) {
        if (mTransformList == nullptr) {
            return nullptr;
        }

        return &mTransformList[index];
    }

private:
    f32 sumTrackWeight() const;
    void blendTracks(u32 joint, f32 totalWeight, Vec3f* pScale, Vec3f* pTranslate, Quaternion* pRotation) const;
    void blendWithFrozenAndLast(u32 joint, f32 blend, Vec3f* pScale, Vec3f* pTranslate, Quaternion* pRotation);
    void captureFrozen(u32 joint);
    void divideParentTransformScale(MtxPtr pMtx, u32 joint) const;
    void applyLocalMatrices(MtxPtr pMtx, u32 joint) const;
    Vec3f calcTransformScale(const Vec3f& rScale, u32 joint) const;

public:
    // The model's ScaleMode.
    u8 mScaleMode;
    u32 mTrackCount;
    // A CalcMode.
    u8 mCalcMode;
    u32 mJointCount;
    XjointInfo* mJointList;
    XjointTransform* mTransformList;
    XanimeTrack* mTrackList;
    // Blend from each joint's frozen pose (0) to the animation (1).
    f32 mInterpoleRatio;
    // Blend from last frame's pose (0) to this frame's (1).
    f32 mPoseBlendRatio;
    // Every track's frame as a fraction of its length.
    f32 mFrameRatio;
    // doFreeze asks; the next updateFrame turns it into mIsCaptureFrozen for one calc.
    bool mIsFreezeRequested;
    bool mIsCaptureFrozen;
};
