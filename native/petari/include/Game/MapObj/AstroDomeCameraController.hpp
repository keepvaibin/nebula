#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AstroDomeCameraController : public LiveActor {
public:
    class Position {
    public:
        Position() {
            mStart.zero();
            mEnd.zero();
            mTarget.zero();
        }

        void setEnd(const Vec3f& rEnd) {
            mStart.set(mTarget);
            mEnd.set(rEnd);
        }

        void reset(const Vec3f& rVec) {
            mStart.set(rVec);
            mEnd.set(rVec);
            mTarget.set(rVec);
        }

        Vec3f mStart;
        Vec3f mEnd;
        Vec3f mTarget;
    };

    AstroDomeCameraController(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void calcZoomOutPos(Vec3f*) const;
    void calcZoomInPos(Vec3f*, const Vec3f&) const;
    void calcZoomInTarget(Vec3f*, const Vec3f&) const;
    void calcZoomInUp(Vec3f*) const;

    void exeGalaxySelectStart();
    void exeGalaxySelect();
    void exeGalaxyConfirmStart();
    void exeGalaxyConfirm();
    void exeGalaxyConfirmCancel();

    Vec3f mStartPos;
    Position mZoomPos;
    Position mTargetPos;
    Position mUpPos;
    f32 mEaseRate;
    Vec3f mZeroPos;
};
