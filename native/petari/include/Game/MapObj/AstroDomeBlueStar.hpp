#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/EffectUtil.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class GCaptureRibbon;

class AstroDomeBlueStar : public LiveActor {
public:
    class CaptureActor : public LiveActor {
    public:
        CaptureActor();

        virtual void init(const TableIter&) override;
        virtual void kill() override;

        void updateTransTraction(const Vec3f&, const Vec3f&, s32);

        void forceKill() {
            MR::forceDeleteEffectAll(this);
            LiveActor::kill();
        };

        void setPosAll(Vec3f& mPos) {
            mPosition.set(mPos);
            mHostMtx.setTrans(mPosition);
        }

        Pos3f mHostMtx;
    };

    AstroDomeBlueStar(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void waitAppear();
    void forceKill();
    bool tryStartBind(const LiveActor*);
    bool isActiveBind() const;
    bool isValidBindStart() const;
    void calcZoomInPos(Vec3f*) const;

    void exeAppear();
    void exeWait();
    void exePointing();
    void exeTryStartBind();
    void exeBindTraction();
    void exeBindHold();
    void exeBindEnd();
    void exeGalaxySelect();
    void exeGalaxyConfirmStart();
    void exeGalaxyConfirmCancel();
    void exeGalaxyConfirm();

    GCaptureRibbon* mCaptureRibbon;
    Pos3f mHostMtx;
    Pos3f mBindStartMtx;
    ActorCameraInfo* mCameraInfo;
    Vec3f mZoomPos;
    CaptureActor* mCaptureActor;
};
