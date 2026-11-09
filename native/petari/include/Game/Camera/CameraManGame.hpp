#pragma once

#include "Game/AreaObj/CubeCamera.hpp"
#include "Game/Camera/CameraMan.hpp"

class Camera;
class CameraHolder;
class CameraParamChunk;
class CameraParamChunkHolder;
class CameraParamChunkID;
class CamKarikariEffector;
class CamHeliEffector;

class CameraManGame : public CameraMan {
public:
    CameraManGame(CameraHolder*, CameraParamChunkHolder*, const char*);

    virtual void init(const TableIter&) override;

    virtual void calc() override;
    virtual void notifyActivate() override;
    virtual void notifyDeactivate() override {};
    virtual bool isInterpolationOff() const override;
    virtual bool isCollisionOff() const override;
    virtual bool isZeroFrameMoveOff() const override;
    virtual bool isSubjectiveCameraOff() const override;
    virtual bool isCorrectingErpPositionOff() const override;
    virtual bool isEnableToReset() const override;
    virtual bool isEnableToRoundLeft() const override;
    virtual bool isEnableToRoundRight() const override;

    void closeCreatingCameraChunk();
    void startStartPosCamera(bool);
    void endStartPosCamera();
    void zoomIn();
    void zoomOut();
    void selectCameraChunk();
    void setChunk(const CameraParamChunkID&);
    void setNullCamera();
    CameraParamChunk* tryToReplaceChunkToDefault(CameraParamChunk*);
    void requestResetIfNecessary(CameraParamChunk*);
    bool isNecessaryToReset(CameraParamChunk*) const;
    bool isCurrentChunkEnableEndInterp() const;
    void replaceCurrentChunkAndCamera(CameraParamChunk*);
    void applyParameter();
    void checkReset();
    void setSafePose();
    void createDefaultCamera();
    void createDefaultWaterCamera();
    void createDefaultWaterSurfaceCamera();
    void createDefaultFooFighterCamera();
    void createStartAnimCamera();
    void createZoomCamera();
    void checkStateShift();
    bool tryShiftToGCapture();
    bool tryShiftToSwimOrWaterSurface();
    bool tryShiftToFooFighter();
    void updateNormal();
    void updateSwim();
    void updateWaterSurface();
    void updateGCapture();
    void updateFooFighter();
    bool setCubeChunk(CubeCameraArea::ECategory);
    bool tryStartPosCamera();
    bool tryZoomCamera();
    bool isZoomCamera() const;

    CameraHolder* mHolder;
    CameraParamChunkHolder* mChunkHolder;
    Camera* mCamera;
    CameraParamChunk* mChunk;
    bool mRequestReset;
    CamKarikariEffector* mKarikari;
    CamHeliEffector* mHeli;
    u32 mTypeState;
    bool mIsStartPosActive;
    s32 mInterpolateOffCounter;
    bool mZoomedIn;
};
