#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;

class WaterPressureBullet : public LiveActor {
public:
    WaterPressureBullet(const char*);

    virtual ~WaterPressureBullet();
    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void shotWaterBullet(LiveActor*, const TPos3f&, f32, bool, bool, bool, ActorCameraInfo**);

    bool isBound() const {
        return mHostActor != nullptr;
    }

    bool isNotBoundInline() const {
        return _B2 && !isBound();
    }

    bool isNotBound() {
        return isNotBoundInline() || !_B2;
    }

    void exeFly();
    void exeSpinKill();
    bool startHostCamera() const;
    bool endHostCamera() const;
    bool inviteMario(HitSensor*);
    void updateSuffererMtx();

    TVec3f _8C;
    TVec3f _98;
    LiveActor* _A4;
    f32 _A8;
    LiveActor* mHostActor;
    bool _B0;
    bool _B1;
    bool _B2;
    u8 _B3;
    ActorCameraInfo** mCameraInfo;
};
