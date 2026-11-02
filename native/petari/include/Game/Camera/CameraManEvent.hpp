#pragma once

#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraTargetArg.hpp"

class Camera;
class CameraHolder;
class CameraParamChunk;
class CameraParamChunkEvent;
class CameraParamChunkHolder;

#define NR_FIFO_ITEMS 2

class CameraManEvent : public CameraMan {
public:
    enum EPriority {

    };

    class ChunkFIFOItem {
    public:
        ChunkFIFOItem();

        CameraParamChunkEvent* mChunk;
        CameraTargetArg mTargetArg;
        u32 mFrame;
    };

    class ChunkFIFOItemPair {
    public:
        inline ChunkFIFOItemPair() {
        }

        ChunkFIFOItem mFirst;
        ChunkFIFOItem mSecond;
    };

    CameraManEvent(CameraHolder*, CameraParamChunkHolder*, const char*);
    virtual ~CameraManEvent();

    virtual void init(const JMapInfoIter&);

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
    void updateChunkFIFO();
    void applyChunk();
    void checkReset(ChunkFIFOItem*);
    void changeCamera();
    void setExtraParam();
    void setVPanParam();
    void resetCameraIfRequested();
    void setSafePose();
    CameraParamChunkEvent* findChunk(s32, const char*) const;
    void requestChunk(CameraParamChunkEvent*, EPriority, const CameraTargetArg&, s32);
    bool cleanChunkFIFO(CameraParamChunk*);
    bool isChunkFIFOEmpty() const;
    void sendStartInterpolateFrame(CameraParamChunkEvent*, s32);
    s32 getInterpolateFrame(CameraParamChunkEvent*, s32) const;
    void sendFinishInterpolateFrame(CameraParamChunkEvent*, s32);
    s32 searchPriority(CameraParamChunk*) const;
    bool isInFIFO(CameraParamChunk*) const;
    bool isAnimCameraEnd(s32, const char*) const;

    CameraHolder* mHolder;
    CameraParamChunkHolder* mChunkHolder;
    Camera* mCamera;
    ChunkFIFOItemPair mItems[NR_FIFO_ITEMS];
    CameraParamChunkEvent* mChunk;
    u32 _B8;
    bool mRequestReset;
};
