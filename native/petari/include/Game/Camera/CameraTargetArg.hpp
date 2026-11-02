#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class CameraTargetObj;
class CameraTargetMtx;
class LiveActor;
class MarioActor;

class CameraTargetArg {
public:
    CameraTargetArg() : mTargetObj(), mTargetMtx(), mLiveActor(), mMarioActor() {
    }
    CameraTargetArg(const LiveActor*);

    CameraTargetArg(CameraTargetMtx* pTargetMtx) {
        mTargetObj = nullptr;
        mTargetMtx = pTargetMtx;
        mLiveActor = nullptr;
        mMarioActor = nullptr;
    }

    inline CameraTargetArg(CameraTargetObj* pTargetObj, CameraTargetMtx* pMtx, const LiveActor* pActor, MarioActor* pMario) {
        mTargetObj = pTargetObj;
        mTargetMtx = pMtx;
        mLiveActor = pActor;
        mMarioActor = pMario;
    }

    void setTarget() const;

    CameraTargetObj* mTargetObj;
    CameraTargetMtx* mTargetMtx;
    const LiveActor* mLiveActor;
    MarioActor* mMarioActor;
};
