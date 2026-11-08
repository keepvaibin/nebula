#pragma once

#include "Game/Camera/CameraPointer.hpp"

class CamTranslatorPointer : public CamTranslatorBase {
public:
    CamTranslatorPointer(CameraPointer* pCamera) : mCamera(pCamera) {
    }

    void setParam(const CameraParamChunk*);
    Camera* getCamera() const;

    CameraPointer* mCamera;
};
