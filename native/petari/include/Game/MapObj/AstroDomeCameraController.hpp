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

        void setEnd(const TVec3f& rEnd) {
            mStart.set(mTarget);
            mEnd.set(rEnd);
        }

        void reset(const TVec3f& rVec) {
            mStart.set(rVec);
            mEnd.set(rVec);
            mTarget.set(rVec);
        }

        TVec3f mStart;
        TVec3f mEnd;
        TVec3f mTarget;
    };

    AstroDomeCameraController(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void calcZoomOutPos(TVec3f*) const;
    void calcZoomInPos(TVec3f*, const TVec3f&) const;
    void calcZoomInTarget(TVec3f*, const TVec3f&) const;
    void calcZoomInUp(TVec3f*) const;

    void exeGalaxySelectStart();
    void exeGalaxySelect();
    void exeGalaxyConfirmStart();
    void exeGalaxyConfirm();
    void exeGalaxyConfirmCancel();

    TVec3f mStartPos;
    Position mZoomPos;
    Position mTargetPos;
    Position mUpPos;
    f32 mEaseRate;
    TVec3f mZeroPos;
};
