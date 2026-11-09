#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>

class OctahedronBezierSurface;
class BigBubbleMoveLimitter;

class BigBubble : public LiveActor {
public:
    enum ShapeType { ShapeType_Sphere = -1, ShapeType_Cube = 0, ShapeType_Octahedron = 1 };

    BigBubble(const char*);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAnim();
    virtual void draw() const;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mBaseMtx;
    }

    void initSensor();
    void generate(const Vec3f&, const Vec3f&, f32, bool, s32, s32, const BigBubbleMoveLimitter*);
    void setActionType(s32);
    void setShapeType(s32);
    f32 getBaseRadius() const;
    void setHost(LiveActor*);
    void setScale(f32);
    bool requestBind(HitSensor*);
    bool requestMerged(HitSensor* pSender, HitSensor* pReceiver);
    bool requestAssimilate(HitSensor* pSender, HitSensor* pReceiver);
    bool requestCancelBind();
    bool tryAppearEnd();
    bool tryBreak();
    bool tryAutoBreak();
    bool tryBreakEnd();
    bool tryEscape();
    bool tryEscapeEnd();
    bool tryMergedCancel();
    bool tryMergedEnd();
    bool tryGoal();

    void exeAppear();
    void exeWait();
    void exeCapture();
    void exeMerged();
    void exeBreak();
    void exeEscape();
    void exeGoal();

    void endCapture();

    void addDeformVelocityOuter(const Vec3f&, bool);
    void addDeformVelocityInternalPressure();
    void addDeformVelocityRebound();
    void addCoriolisAccel();
    void addAccelMoment(s32 padChannel);
    bool addAccelPointing(s32 padChannel);
    void doMoveLimit();
    void updatePose();
    void updateBindActorMatrix();
    void updateNormalVelocity();
    void resetDeformVelocity();
    void updateDeformVelocity();
    void updateMeshPoint();
    void updateCaptureReduceVolume();
    void updateCaptureWarningColor();
    void calcMergePosition(Vec3f*, Vec3f*, const BigBubble*) const;
    void calcLocalDirection(Vec3f*, s32) const;
    s32 getNearAxisIndex(const Vec3f&) const;
    s32 getMergeIndex(const Vec3f&) const;

    bool isPushable() const;
    bool isBindMario() const;
    bool isDraw() const;
    bool isMerged() const;
    bool isEnemyAttackBreakable() const;

    inline f32 getSize() {
        f32 radius = getBaseRadius();
        return radius * mScale.x;
    }

    inline f32 getRadius(f32 volume) {
        return pow(volume, 1.0f / 3.0f);
    }

    inline f32 getDrawZ() const {
        return mDrawZ;
    }

    inline s32 getCycle() const;

    OctahedronBezierSurface* mSurface;
    const BigBubbleMoveLimitter* mMoveLimitter;
    LiveActor* mHost;
    LiveActor* mRider;
    BigBubble* mMergeBubble;
    BigBubble* mMergeBubbles[6];
    Pos3f mEffectMtx;
    Pos3f mBaseMtx;
    Quat4f mBubbleQuat;
    Quat4f mRiderQuat;
    Vec3f mRiderBasePos;
    Vec3f mRiderPos;
    Vec3f mSpawnPosition;
    Vec3f mPointerPos;
    Vec3f mPushDirection;
    Vec3f mMoment;
    Vec3f mCoriolisAccel;
    f32 mDeformCoeff[6];
    f32 mDeformSpeed[6];
    f32 mInterpolateRate;
    f32 mPointAccel;
    s32 mPushInvalidTime;  // some timer
    s32 mReduceVolumeTimer;
    s32 mMergeIndex;
    s32 mShapeType;
    f32 mAppearRadius;
    f32 mVolume;
    f32 mDrawZ;
    f32 mBinderRadius;
    Color8 mWarningColor;
    bool mIsObstruct;
    bool mIsHidden;
    bool mIsExitLimitter;
    bool mIsBroken;
    bool mIsShrinkable;
    bool mIsAttached;  // disables addCoriolisAccel
};
