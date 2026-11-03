#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class CameraTargetActor;
class CameraTargetObj;
class CameraTargetPlayer;
class LiveActor;
class MarioActor;

class CameraTargetHolder {
public:
    CameraTargetHolder();
    virtual ~CameraTargetHolder() {
    }

    void movement();
    CameraTargetObj* get();
    void set(CameraTargetObj*);
    void set(const LiveActor*);
    void set(const MarioActor*);
    bool isOnGround() const;
    bool isMoving() const;

    CameraTargetObj* mTarget;
    CameraTargetActor* mTargetActor;
    CameraTargetPlayer* mTargetPlayer;
};
