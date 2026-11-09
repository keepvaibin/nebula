#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class CameraTargetMtx;
class GameTexture;
class PartsModel;
class PlantPoint;

class SpaceCocoon : public LiveActor {
public:
    SpaceCocoon(const char*);

    virtual void init(const TableIter&);
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

    void endBind(const Vec3f&, bool);
    void endCommandStream();

    void drawPlane(TypedDraw&, f32, f32, f32, f32, Color8, Color8, f32, f32) const;
    SiteMaterial loadMaterial() const;

    /// @note if the next use of the SpaceCocoon should signal the associated camera area to stop focusing on this SpaceCocoon (for use when Kinipio
    /// is not yet launched from SpaceCocoon)
    bool mIsKinopioCameraFocused;
    Vec3f mNeutralPos;
    Vec3f mBasePos;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    f32 mHeight;
    Vec3f mPointerPos;
    s32 mPadChannel;
    Vec3f mCocoonPos;
    Vec3f mSpringVel;

    /// @note objArg0: number of frames in "attack mode" in the air before regaining control
    s32 mAttackTime;
    s32 mTouchTime;
    LiveActor* mRider;

    Pos3f mBaseMtx;
    s32 mNumPoints;
    PlantPoint** mPlantPoints;
    CameraTargetMtx* mCameraTargetMtx;
    ActorCameraInfo* mCameraInfo;
    PartsModel* mCocoonModel;
    Pos3f mTopMtx;
    GameTexture* mTexture;
};
