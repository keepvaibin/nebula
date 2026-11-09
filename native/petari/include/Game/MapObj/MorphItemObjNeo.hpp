#pragma once

#include "Game/Effect/SimpleEffectObj.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class ModelObj;
class CameraTargetMtx;

class MorphItemObjNeo : public LiveActor {
public:
    enum Container {
        Container_None = 0,
        Container_Bubble = 1,
        Container_CrystalBox = 2,
    };

    MorphItemObjNeo(const char*, s32);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual bool isDemo() const;


    void glowEffect();

    void exeWait();
    void exeAppear();
    void exeSwitchAppear();
    void exeWait2();
    void exeFly();
    void exeDemo();

    s32 mSpiralTime;    // frames of the switch-appear spiral left
    s32 mAppearFrames;  // frames of the current appear camera left
    bool mIsUseSwitch;
    bool mIsFloating;  // Obj_arg2 != 1
    bool mIsDemoPending;
    u8 mCameraMode;  // appear cameras still to run (Obj_arg5: 1 or 2)
    s8 mRotateMode;  // Obj_arg6
    bool mIsSwitchAppearOnce;  // Obj_arg0 == 1
    u8 mContainerType;         // Container, from Obj_arg3
    u8 mBoundCount;
    s8 mIsBind;  // Obj_arg1
    Vec3f mZRotation;
    s32 mMorphType;  // PlayerItemStatus
    f32 mAngle;
    Vec3f mSpawnPosition;
    ActorCameraInfo* mActorCameraInfo;
    ModelObj* mContainerModel;
    ModelObj* mContainerBreakModel;
    Pos3f mBaseMtx;
    CameraTargetMtx* mCameraTargetMtx;
};

class MorphItemNeoHopper : public MorphItemObjNeo {
public:
    MorphItemNeoHopper(const char* pName) : MorphItemObjNeo(pName, PlayerItemStatus_Hopper) {
    }
};

class MorphItemNeoIce : public MorphItemObjNeo {
public:
    MorphItemNeoIce(const char* pName) : MorphItemObjNeo(pName, PlayerItemStatus_Ice) {
    }
};

class MorphItemNeoFire : public MorphItemObjNeo {
public:
    MorphItemNeoFire(const char* pName) : MorphItemObjNeo(pName, PlayerItemStatus_Fire) {
    }
};

class MorphItemNeoFoo : public MorphItemObjNeo {
public:
    MorphItemNeoFoo(const char* pName) : MorphItemObjNeo(pName, PlayerItemStatus_Foo) {
    }
};

class MorphItemNeoBee : public MorphItemObjNeo {
public:
    MorphItemNeoBee(const char* pName) : MorphItemObjNeo(pName, PlayerItemStatus_Bee) {
    }
};

class MorphItemNeoTeresa : public MorphItemObjNeo {
public:
    MorphItemNeoTeresa(const char* pName) : MorphItemObjNeo(pName, PlayerItemStatus_Teresa) {
    }
};

class EffectObjR100F50SyncClipping : public SimpleEffectObj {
public:
    EffectObjR100F50SyncClipping(const char* pName) : SimpleEffectObj(pName) {
    }

    virtual f32 getClippingRadius() const override {
        return 100.0f;
    }

    virtual f32 getFarClipDistance() const override {
        return 50.0f;
    }

    virtual bool isSyncClipping() const override {
        return true;
    }
};

class EffectObjR500F50 : public SimpleEffectObj {
public:
    EffectObjR500F50(const char* pName) : SimpleEffectObj(pName) {
    }

    virtual f32 getClippingRadius() const override {
        return 500.0f;
    }

    virtual f32 getFarClipDistance() const override {
        return 50.0f;
    }
};

class EffectObjR1000F50 : public SimpleEffectObj {
public:
    EffectObjR1000F50(const char* pName) : SimpleEffectObj(pName) {
    }

    virtual f32 getClippingRadius() const override {
        return 1000.0f;
    }

    virtual f32 getFarClipDistance() const override {
        return 50.0f;
    }
};

class EffectObj20x20x10SyncClipping : public SimpleEffectObj {
public:
    EffectObj20x20x10SyncClipping(const char* pName) : SimpleEffectObj(pName) {
    }

    virtual f32 getClippingRadius() const override {
        return 1000.0f;
    }

    virtual f32 getFarClipDistance() const override {
        return 50.0f;
    }

    virtual Vec3f getClippingCenterOffset() const override {
        return Vec3f(0.0f, 200.0f, 0.0f);
    }

    virtual bool isSyncClipping() const override {
        return true;
    }
};

class EffectObj50x50x10SyncClipping : public SimpleEffectObj {
public:
    EffectObj50x50x10SyncClipping(const char* pName) : SimpleEffectObj(pName) {
    }

    virtual f32 getClippingRadius() const override {
        return 2500.0f;
    };
    virtual f32 getFarClipDistance() const override {
        return 50.0f;
    };
    virtual Vec3f getClippingCenterOffset() const override {
        return Vec3f(0.0f, 200.0f, 0.0f);
    };
    virtual bool isSyncClipping() const override {
        return true;
    };
};

class EffectObj10x10x10SyncClipping : public SimpleEffectObj {
public:
    EffectObj10x10x10SyncClipping(const char* pName) : SimpleEffectObj(pName) {
    }

    virtual f32 getClippingRadius() const override {
        return 1000.0f;
    }

    virtual f32 getFarClipDistance() const override {
        return 50.0f;
    }

    virtual Vec3f getClippingCenterOffset() const override {
        return Vec3f(0.0f, 580.0f, 0.0f);
    }

    virtual bool isSyncClipping() const override {
        return true;
    }
};

namespace NrvMorphItemObjNeo {
    NEW_NERVE(MorphItemObjNeoNrvWait, MorphItemObjNeo, Wait);
    NEW_NERVE(MorphItemObjNeoNrvAppear, MorphItemObjNeo, Appear);
    NEW_NERVE(MorphItemObjNeoNrvSwitchAppear, MorphItemObjNeo, SwitchAppear);
    NEW_NERVE(MorphItemObjNeoNrvWait2, MorphItemObjNeo, Wait);
    NEW_NERVE(MorphItemObjNeoNrvFly, MorphItemObjNeo, Fly);
    NEW_NERVE(MorphItemObjNeoNrvDemo, MorphItemObjNeo, Wait);
};  // namespace NrvMorphItemObjNeo
