#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFixedThere : public Camera {
public:
    enum CameraType {
        CameraType_GravityUp,
        CameraType_WorldUp,
    };

    CameraFixedThere(const char* pName = "\202\273\202\314\217\352\222\350\223\137\203\112\203\201\203\211" /* その場定点カメラ */);

    virtual ~CameraFixedThere();
    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isCorrectingErpPositionOff() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    void setParam(u32 cameraType, bool isFovyFixed) {
        mCameraType = cameraType;
        mIsFovyFixed = isFovyFixed;
    }

    void copyStatusFromPrevCamera();
    bool calcEyeDir(TVec3f*);
    void makeAxisAndRoll();
    void updateUpVec(const TVec3f&);
    void updateNormalUpVec(const TVec3f&);

    u32 mCameraType;
    bool mIsFovyFixed;
    TVec3f mUp;
    TVec3f mAxis;
    f32 mRoll;
};
