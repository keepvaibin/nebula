#pragma once

#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraTargetArg.hpp"

class Camera;
class CameraHolder;
class CameraParamChunk;
class CameraParamChunkEvent;
class CameraParamChunkHolder;

class CameraManEvent : public CameraMan {
public:
    // An event camera's chunk with its target and the blend frame start/end asked for.
    struct Request {
        CameraParamChunkEvent* mChunk;
        CameraTargetArg mTargetArg;
        u32 mFrame;
    };

    // One event priority's camera: mRequested is the start made since the last calc, which calc makes mActive.
    struct PrioritySlot {
        Request mActive;
        Request mRequested;
    };

    CameraManEvent(CameraHolder*, CameraParamChunkHolder*, const char*);

    virtual void init(const TableIter&);

    virtual void calc();
    virtual void notifyActivate();
    virtual void notifyDeactivate();
    virtual bool isInterpolationOff() const;
    virtual bool isCollisionOff() const;
    virtual bool isZeroFrameMoveOff() const;
    virtual bool isCorrectingErpPositionOff() const;

    void start(s32, const char*, const CameraTargetArg&, s32);
    void end(s32, const char*, s32);
    bool isEventActive(s32, const char*) const;
    bool isActive() const;
    bool doesNextChunkHaveInterpolation() const;
    u32 getAnimCameraFrame(s32, const char*) const;
    void pauseOnAnimCamera(s32, const char*);
    void pauseOffAnimCamera(s32, const char*);
    void takeRequests();
    void applyChunk();
    void checkReset(Request*);
    void changeCamera();
    void setExtraParam();
    void setVPanParam();
    void resetCameraIfRequested();
    void setSafePose();
    CameraParamChunkEvent* findChunk(s32, const char*) const;
    void request(CameraParamChunkEvent*, u32 priority, const CameraTargetArg&, s32);
    bool removeRequest(CameraParamChunk*);
    bool hasNoRequest() const;
    void sendStartInterpolateFrame(CameraParamChunkEvent*, s32);
    s32 getInterpolateFrame(CameraParamChunkEvent*, s32) const;
    void sendFinishInterpolateFrame(CameraParamChunkEvent*, s32);
    bool isRequested(CameraParamChunk*) const;
    bool isAnimCameraEnd(s32, const char*) const;

    CameraHolder* mHolder;
    CameraParamChunkHolder* mChunkHolder;
    Camera* mCamera;
    static const u32 cPriorityNum = 2;
    PrioritySlot mSlots[cPriorityNum];
    CameraParamChunkEvent* mChunk;
    bool mRequestReset;
};
