#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class Camera;
class CameraHeightArrange;
class CameraMan;
class CameraParamChunk;
class CameraPoseParam;
class CameraTargetObj;

class CamTranslatorBase {
public:
    CamTranslatorBase() {
    }

    virtual void setParam(const CameraParamChunk*) = 0;
    virtual Camera* getCamera() const = 0;
};

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
    virtual CamTranslatorBase* createTranslator();

    void setZoneMtx(s32);
    void createVPanObj();

    bool doesVPanExist() const {
        return mVPan != nullptr;
    }

    CameraMan* mCameraMan;
    CameraPoseParam* mPoseParam;
    CameraHeightArrange* mVPan;
    bool mIsLOfsErpOff;
    TPos3f mZoneMatrix;
};

class CamTranslatorDummy : public CamTranslatorBase {
public:
    CamTranslatorDummy(Camera* pCamera) : mCamera(pCamera) {
    }

    virtual void setParam(const CameraParamChunk*) {};
    virtual Camera* getCamera() const {
        return mCamera;
    }

    Camera* mCamera;
};
