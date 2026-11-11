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
    // The game's camera types (its table), for the camera registry (LumaRegistry).
    static u32 getTypeNum();
    static const char* getTypeName(u32 index);
    Camera* getDefaultCamera();
    s32 getIndexOfDefault() const;
    const char* getNameStrOfDefault() const;
    s32 getIndexOf(Camera*) const;
    void createCameras();
    Camera* getCameraInner(s32) const;

    s32 mDefaultCameraIndex;
    Camera** mCameras;
    MR::NameIndex mIndex;  // camera type name -> its index, the first of a name
};
