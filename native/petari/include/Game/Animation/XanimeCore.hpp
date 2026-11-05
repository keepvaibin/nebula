#pragma once

#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <nebula_geometry.hpp>

class J3DModelData;
class J3DAnmTransform;

class XtransformInfo {
public:
    XtransformInfo();

    Vec3f _0;
    Vec3f _C;
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
    Vec3f mScale;
    Vec3f _14;
    Vec3f _20;
    Vec3f _2C;
    Vec3f _38;
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
    Vec3f _50;
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
    void calcBlend(Vec3f*, Vec3f*);
    void calcSingle(Vec3f*, Vec3f*);
    void calcBlendSpecial();
    void calcScaleBlendMaya(const Vec3f&, const Vec3f&);
    void calcScaleBlendMayaNoTransform(const Vec3f&, const Vec3f&);
    void calcScaleBlendSI(const Vec3f&, const Vec3f&);
    void calcScaleBlendBasic(const Vec3f&, const Vec3f&);
    void calcScaleBlendSpecial();
    void freezeCopy(J3DModelData*, XanimeCore*, u32, u32);
    void initT(J3DModelData*);
    void reconfigJointTransform(J3DModelData*);
    void updateFrame();
    void fixT(Vec3f*);

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
