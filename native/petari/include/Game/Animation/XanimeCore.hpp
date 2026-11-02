#pragma once

#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/JGeometry/TVec.hpp>

class J3DModelData;
class J3DAnmTransform;

class XtransformInfo {
public:
    XtransformInfo();

    TVec3f _0;
    TVec3f _C;
    Quaternion mRotation;
};

class XjointTransform {
public:
    XjointTransform();

    void setLocalScale(f32 scale) {
        mScale.set(scale);
    }

    J3DJoint* _0;
    u16 _4;
    TVec3f mScale;
    TVec3f _14;
    TVec3f _20;
    TVec3f _2C;
    TVec3f _38;
    J3DTransformInfo _44;
    MtxPtr _64;
    uintptr_t _68;
    MtxPtr _6C;
};

class XjointInfo {
public:
    XjointInfo();

    XtransformInfo _0;
    XtransformInfo _28;
    TVec3f _50;
    f32 _5C;
    f32 _60;
};

class XanimeTrack {
public:
    XanimeTrack() {
    }

    void init();
    void setFrame(f32 frame) {
        _8 = frame;
        _C = 1;
    }

    f32 getWeight() const {
        return mWeight;
    }

    J3DAnmTransform* _0;
    f32 mWeight;
    f32 _8;
    u8 _C;
};

class XanimeCore : public J3DMtxCalc {
public:
    XanimeCore(u32, u32, u8);
    XanimeCore(u32, XanimeCore*);

    virtual ~XanimeCore();
    virtual void setWeight(u8, f32);
    virtual void init(const Vec&, const Mtx&);
    virtual void calc();

    void shareJointTransform(const XanimeCore*);
    void doFreeze();
    void setBck(u32, J3DAnmTransform*);
    void calcBlend(TVec3f*, TVec3f*);
    void calcSingle(TVec3f*, TVec3f*);
    void calcBlendSpecial();
    void calcScaleBlendMaya(const TVec3f&, const TVec3f&);
    void calcScaleBlendMayaNoTransform(const TVec3f&, const TVec3f&);
    void calcScaleBlendSI(const TVec3f&, const TVec3f&);
    void calcScaleBlendBasic(const TVec3f&, const TVec3f&);
    void calcScaleBlendSpecial();
    void freezeCopy(J3DModelData*, XanimeCore*, u32, u32);
    void initT(J3DModelData*);
    void reconfigJointTransform(J3DModelData*);
    void updateFrame();
    void fixT(TVec3f*);

    void enableJointTransform(J3DModelData*);

    void initMember(u32);

    XjointTransform* getJointTransform(u32 index) {
        if (mTransformList == nullptr) {
            return nullptr;
        }

        return &mTransformList[index];
    }

    u8 _4;
    u8 mTrackCount;
    u8 _6;
    u32 mJointCount;
    u16 _C;
    XjointInfo* mJointList;
    XjointTransform* mTransformList;
    XanimeTrack* mTrackList;
    f32 _1C;
    f32 _20;
    f32 mFrameRatio;
    u8 _28;
    u8 _29;
};
