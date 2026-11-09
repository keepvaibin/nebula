#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFixedThere : public Camera {
public:
    enum CameraType {
        CameraType_GravityUp,
    };

    CameraFixedThere(const char* pName = "その場定点カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isCorrectingErpPositionOff() const override {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(u32 cameraType, bool isFovyFixed) {
        mCameraType = cameraType;
        mIsFovyFixed = isFovyFixed;
    }

    void copyStatusFromPrevCamera();
    bool calcEyeDir(Vec3f*);
    void makeAxisAndRoll();
    void updateUpVec(const Vec3f&);
    void updateNormalUpVec(const Vec3f&);

    u32 mCameraType;
    bool mIsFovyFixed;
    Vec3f mUp;
    Vec3f mAxis;
    f32 mRoll;
};
