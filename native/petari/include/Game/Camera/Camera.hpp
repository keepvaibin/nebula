#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class Camera;
class CameraHeightArrange;
class CameraMan;
class CameraParamChunk;
class CameraPoseParam;
class CameraTargetObj;

class Camera : public NameObj {
public:
    Camera(const char*);

    virtual void reset() {};
    virtual CameraTargetObj* calc() = 0;
    virtual bool isInterpolationOff() const {
        return false;
    }
    virtual bool isCollisionOff() const {
        return false;
    }
    virtual bool isZeroFrameMoveOff() const {
        return false;
    }
    virtual bool isSubjectiveCameraOff() const {
        return false;
    }
    virtual bool isCorrectingErpPositionOff() const {
        return false;
    }
    virtual void roundLeft() {};
    virtual void roundRight() {};
    virtual bool isEnableToRoundLeft() const {
        return false;
    }
    virtual bool isEnableToRoundRight() const {
        return false;
    }
    virtual bool isEnableToReset() const {
        return false;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) {};

    void setZoneMtx(s32);
    void createVPanObj();

    bool doesVPanExist() const {
        return mVPan != nullptr;
    }

    CameraMan* mCameraMan;
    CameraPoseParam* mPoseParam;
    CameraHeightArrange* mVPan;
    bool mIsLOfsErpOff;
    Pos3f mZoneMatrix;
};
