#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFixedThere : public Camera {
public:
    enum CameraType {
        /* 0x0 */ CameraType_GravityUp,
        /* 0x1 */ CameraType_WorldUp,
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

    /* 0x4C */ u32 mCameraType;
    /* 0x50 */ bool mIsFovyFixed;
    /* 0x54 */ TVec3f mUp;
    /* 0x60 */ TVec3f mAxis;
    /* 0x6C */ f32 mRoll;
};
