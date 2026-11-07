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
        /// @brief Creates a new `CaptureActor`.
        CaptureActor();

        virtual void init(const TableIter&);
        virtual void kill();

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

    /// @brief Creates a new `AstroDomeBlueStar`.
    /// @param pName A pointer to the null-terminated name of the object.
    AstroDomeBlueStar(const char* pName);

    virtual void init(const TableIter&);
    virtual void draw() const;
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
