#pragma once

#include "Game/LiveActor/LiveActorFlag.hpp"
#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class ActorAnimKeeper;
class ActorLightCtrl;
class ActorPadAndCameraCtrl;
class AudAnmSoundObject;
class Binder;
class CollisionParts;
class EffectKeeper;
class HitSensor;
class HitSensorKeeper;
class ModelManager;
class Nerve;
class RailRider;
class ResourceHolder;
class ShadowControllerList;
class Spine;
class StageSwitchCtrl;
class StarPointerTarget;

/// @brief The basis of a drawable actor that can contain states (see: Nerve)
class LiveActor : public NameObj {
public:
    /// @param pName A pointer to the null-terminated name of the actor.
    LiveActor(const char* pName);

    /// @brief Intializes the `LiveActor` while being placed into a scene.
    /// @param rIter A reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter) override;

    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void calcViewAndEntry() override;
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void makeActorDead();
    virtual bool receiveMessage(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    /// @brief Gets the base matrix of the model used for the actor.
    /// @returns A MtxPtr to the base matrix. NULL if there is no model present.
    virtual MtxPtr getBaseMtx() const;
    virtual MtxPtr getTakingMtx() const;
    virtual void startClipped();
    virtual void endClipped();

    virtual void control() {
    }

    /// @brief Calculates and sets the base matrix of the actor.
    virtual void calcAndSetBaseMtx();
    void prepareDrawClipped();

    // What a startClipped or endClipped in progress changes, its actor's overrides and parts included: everything (the
    // actor left or entered both its running distance and the camera's view), its running alone (still seen past its
    // distance) or its being seen alone (past its distance, entering or leaving the view).
    enum ClipScope { ClipScope_All, ClipScope_Run, ClipScope_Seen };
    static ClipScope sClipScope;

    virtual void updateHitSensor(HitSensor* /*pSensor*/) {
    }

    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    virtual bool receiveMsgPush(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveMsgPlayerAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveMsgEnemyAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveMsgTake(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveMsgTaken(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveMsgThrow(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    virtual bool receiveMsgApart(HitSensor* pSender, HitSensor* pReceiver);

    virtual bool receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    }

    HitSensorKeeper* getSensorKeeper() const {
        return mSensorKeeper;
    }

    Binder* getBinder() const {
        return mBinder;
    }

    const Vec3f* getGravity() const {
        return &mGravity;
    }

    const Vec3f* getVelocity() const {
        return &mVelocity;
    }

    const Vec3f* getPosition() const {
        return &mPosition;
    }

    Vec3f* editPosition() {
        return &mPosition;
    }

    void calcAnmMtx();
    void setNerve(const Nerve* pNerve);
    bool isNerve(const Nerve* pNerve) const;
    s32 getNerveStep() const;

    /// @brief Gets a sensor.
    /// @param pSensorName The name of the sensor to get.
    /// @returns The sensor that contains the name given. Returns the result of HitSensorKeeper::getSensor or NULL if there is no HitSensorKeeper
    /// created.
    HitSensor* getSensor(const char* pSensorName) const;
    void initModelManagerWithAnm(const char* pModelArcName, const char* pAnimArcName, bool deferMaterials);
    void initNerve(const Nerve*);
    void initHitSensor(int);
    void initBinder(f32 radius, f32 offsetY, u32 planeNum);
    void initRailRider(const TableIter& rIter);
    void initEffectKeeper(int extraEmitterNum, const char* pResourceName);
    void initSound(int numHandles, bool is2D);
    void initShadowControllerList(u32);
    void initActorCollisionParts(const char*, HitSensor*, ResourceHolder*, MtxPtr, bool, bool);
    void initStageSwitch(const TableIter& rIter);
    void initActorStarPointerTarget(f32, const Vec3f*, MtxPtr, Vec3f);
    void initActorLightCtrl();
    void addToSoundObjHolder();
    void updateBinder();

    /// @brief The three-dimensional location vector.
    Vec3f mPosition;

    /// @brief The three-dimensional rotation vector, in degrees.
    Vec3f mRotation;

    /// @brief The three-dimensional scale vector.
    Vec3f mScale;

    /// @brief The three-dimensional velocity vector, in units per step.
    Vec3f mVelocity;

    /// @brief The three-dimensional gravity unit vector.
    Vec3f mGravity;

    /// @brief A pointer to a `ModelManager` instance, used for drawing a 3D model.
    ModelManager* mModelManager;

    /// @brief A pointer to an `ActorAnimKeeper` instance, used for storing and playing model animations.
    ActorAnimKeeper* mAnimKeeper;

    Spine* mSpine;

    /// @brief A pointer to a `HitSensorKeeper` instance, used for storing `HitSensor` instances.
    HitSensorKeeper* mSensorKeeper;

    Binder* mBinder;

    /// @brief A pointer to a `RailRider` instance, used for allowing the actor to ride on paths.
    RailRider* mRailRider;
    EffectKeeper* mEffectKeeper;
    AudAnmSoundObject* mSoundObject;

    /// @brief Flags relating to drawing, animation calculation, etc.
    LiveActorFlag mFlag;

    ShadowControllerList* mShadowControllerList;
    CollisionParts* mCollisionParts;

    /// @brief A pointer to a `StageSwitchCtrl` instance, used for activating and deactivating switches.
    StageSwitchCtrl* mStageSwitchCtrl;

    StarPointerTarget* mStarPointerTarget;
    ActorLightCtrl* mActorLightCtrl;
    ActorPadAndCameraCtrl* mCameraCtrl;
};
