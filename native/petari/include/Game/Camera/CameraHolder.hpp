#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/NameIndex.hpp"

class Camera;

class CameraHolder : public NameObj {
public:
    CameraHolder(const char*);

    s32 getNum() const;
    s32 getIndexOf(const char*) const;
    const char* getNameStrOf(s32) const;
    Camera* getDefaultCamera();
    s32 getIndexOfDefault() const;
    const char* getNameStrOfDefault() const;
    s32 getIndexOf(Camera*) const;
    void createCameras();
    Camera* getCameraInner(s32) const;

    bool isCameraIndexType(s32 index, const char* pType) const {
        return getIndexOf(pType) == index;
    }

    s32 mDefaultCameraIndex;
    Camera** mCameras;
    MR::NameIndex mIndex;  // camera type name -> its index, the first of a name
};
