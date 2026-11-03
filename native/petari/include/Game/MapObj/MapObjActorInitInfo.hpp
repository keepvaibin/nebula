#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class Nerve;

class MapObjActorInitInfo {
public:
    MapObjActorInitInfo();

    void setupHioNode(const char*);
    void setupDefaultPos();
    void setupModelName(const char*);
    void setupConnectToScene();
    void setupBinder(f32, f32);
    void setupEffect(const char*);
    void setupSound(s32);
    void setupSoundPos(TVec3f*);
    void setupNoAppearRiddleSE();
    void setupHitSensor();
    void setupHitSensorCallBack();
    void setupHitSensorParam(u16, f32, const TVec3f&);
    void setupNerve(const Nerve*);
    void setupShadow(const char*);
    void setupGroupClipping(s32);
    void setupClippingRadius(f32);
    void setupFarClipping(f32);
    void setupProjmapMtx(bool);
    void setupRailMover();
    void setupRotator();
    void setupRailRotator();
    void setupSeesaw1AxisRotator(const char*, f32);
    void setupRailPosture();
    void setupBaseMtxFollowTarget();
    void setupAffectedScale();
    void setupSeAppear();
    void setupMirrorReflection(bool);
    void setupPrepareChangeDummyTexture(const char*);
    void setupNoUseLodCtrl();

    bool mSetDefaultPosition;
    bool mConnectToScene;
    bool mInitBinder;
    bool mHasEffect;
    bool mHasSensors;
    bool mHasShadows;
    bool mCalcGravity;
    bool mUseProjectMapMtx;
    bool mInitFur;
    bool mHasRailMover;
    bool mHasRotator;
    bool mHasRailRotator;
    bool _C;
    bool _D;
    bool mUseRailPosture;
    bool mUseBaseMtxFollowTarget;
    bool mIsAffectedByScale;
    bool mUseMirrorReflection;
    bool _12;
    bool _13;
    const char* mModelName;
    const char* mHioNode;
    u16 _1C;
    f32 mSensorRadius;
    TVec3f mSensorPosition;
    bool mHasHitSensorCB;
    u8 _31;
    u8 _32;
    u8 _33;
    f32 mBinderRadius;
    f32 mBinderCenterY;
    const char* mEffectName;
    s32 mSound;
    TVec3f* mSoundPos;
    bool _48;
    const Nerve* mNerve;
    s32 mGroupClipping;
    f32 mClippingRadius;
    f32 mFarClipping;
    s32 _5C;
    s32 mColorChangeArg;    // Obj_arg0
    s32 mTextureChangeArg;  // Obj_arg1
    bool mIsAppearRiddleSE;
    const char* mSeesaw1AxisRotatorName;
    f32 mSeesaw1AxisRotatorRadius;
    bool _74;
    const char* mShadowName;
    f32 mShadowLength;  // Obj_arg2
    const char* _80;
    const char* mDummyChangeTexture;
    bool _8C;
    bool mNoUseLOD;
};
