#pragma once

#include <nebula_geometry.hpp>

class Nerve;

class MapObjActorInitInfo {
public:
    MapObjActorInitInfo();

    void setupDefaultPos();
    void setupModelName(const char*);
    void setupConnectToScene();
    void setupBinder(f32, f32);
    void setupEffect(const char*);
    void setupSound(s32);
    void setupSoundPos(Vec3f*);
    void setupNoAppearRiddleSE();
    void setupHitSensor();
    void setupHitSensorCallBack();
    void setupHitSensorParam(u16, f32, const Vec3f&);
    void setupNerve(const Nerve*);
    void setupShadow(const char*);
    void setupGroupClipping(s32);
    void setupClippingRadius(f32);
    void setupFarClipping(f32);
    void setupProjmapMtx(bool isEveryFrame);
    void setupRailMover();
    void setupRotator();
    void setupRailRotator();
    void setupSeesaw1AxisRotator(const char*, f32);
    void setupRailPosture();
    void setupBaseMtxFollowTarget();
    void setupAffectedScale();
    void setupSeAppear();
    void setupMirrorReflection(bool isFromModel);
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
    bool mHasSeesaw1AxisRotator;
    bool mHasSeesaw2AxisRotator;
    bool mUseRailPosture;
    bool mUseBaseMtxFollowTarget;
    bool mIsAffectedByScale;
    bool mUseMirrorReflection;
    const char* mModelName;
    u16 mSensorType;
    f32 mSensorRadius;
    Vec3f mSensorPosition;
    bool mHasHitSensorCB;
    f32 mBinderRadius;
    f32 mBinderCenterY;
    const char* mEffectName;
    s32 mSound;
    Vec3f* mSoundPos;
    bool mIsProjmapEveryFrame;
    const Nerve* mNerve;
    s32 mGroupClipping;
    f32 mClippingRadius;
    f32 mFarClipping;
    s32 mLightType;
    s32 mColorChangeArg;    // Obj_arg0
    s32 mTextureChangeArg;  // Obj_arg1
    bool mIsAppearRiddleSE;
    const char* mSeesaw1AxisRotatorName;
    f32 mSeesaw1AxisRotatorRadius;
    bool mIsMirrorFromModel;
    const char* mShadowName;
    f32 mShadowLength;  // Obj_arg2
    const char* mBreakModelName;
    const char* mDummyChangeTexture;
    bool mInitLightCtrl;
    bool mNoUseLOD;
};
