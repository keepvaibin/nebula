#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Color.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class CameraTargetMtx;
class JUTTexture;
class PartsModel;
class PlantPoint;

class SpaceCocoon : public LiveActor {
public:
    SpaceCocoon(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void calcAnim();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeStop();
    void exeFree();
    void exeFreeInvalid();
    void exeBindLand();
    void exeBindWait();
    void exeWaitKinopioAimDemo();
    void exeBindAim();
    void exeBindAttack();
    void exeBindAttackSuccess();

    bool updateBindWait();
    bool updateSpringPoint();
    void updateHang();
    void updateBindAttack();
    void updateActorMtx();
    void updateDrawPoints();

    bool tryTouch();
    bool tryRelease();
    bool tryAttackMap();
    bool isKinopioAttached() const;

    void endBind(const TVec3f&, bool);
    void endCommandStream();

    void drawPlane(f32, f32, f32, f32, Color8, Color8, f32, f32) const;
    void initDraw() const;

    /// @note if the next use of the SpaceCocoon should signal the associated camera area to stop focusing on this SpaceCocoon (for use when Kinipio
    /// is not yet launched from SpaceCocoon)
    bool mIsKinopioCameraFocused;
    TVec3f mNeutralPos;
    TVec3f mBasePos;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    f32 mHeight;
    TVec3f mPointerPos;
    s32 mPadChannel;
    TVec3f mCocoonPos;
    TVec3f mSpringVel;

    /// @note objArg0: number of frames in "attack mode" in the air before regaining control
    s32 mAttackTime;
    s32 mTouchTime;
    LiveActor* mRider;

    TPos3f mBaseMtx;
    s32 mNumPoints;
    PlantPoint** mPlantPoints;
    CameraTargetMtx* mCameraTargetMtx;
    ActorCameraInfo* mCameraInfo;
    PartsModel* mCocoonModel;
    TPos3f mTopMtx;
    JUTTexture* mTexture;
};
