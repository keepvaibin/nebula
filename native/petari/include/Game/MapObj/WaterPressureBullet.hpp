#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;

class WaterPressureBullet : public LiveActor {
public:
    WaterPressureBullet(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void shotWaterBullet(LiveActor*, const Pos3f&, f32 speed, bool isNoGravity, bool isGroundSlide, bool isInvalidSpinKill, ActorCameraInfo**);

    bool isBound() const {
        return mHostActor != nullptr;
    }

    bool isNotBoundInline() const {
        return mIsInvalidSpinKill && !isBound();
    }

    bool isNotBound() {
        return isNotBoundInline() || !mIsInvalidSpinKill;
    }

    void exeFly();
    void exeSpinKill();
    bool startHostCamera() const;
    bool endHostCamera() const;
    bool inviteMario(HitSensor*);
    void updateSuffererMtx();

    Vec3f mFront;
    Vec3f mSide;
    LiveActor* mHost;
    f32 mSpeed;
    LiveActor* mHostActor;
    bool mIsNoGravity;
    bool mIsGroundSlide;
    bool mIsInvalidSpinKill;
    ActorCameraInfo** mCameraInfo;
};
