#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class CameraDirector;
class CameraPoseParam;

class CameraMan : public NameObj {
public:
    CameraMan(const char*);

    virtual void init(const TableIter&) override {
    }
    virtual void movement() override;
    virtual void calc() {
    }
    virtual void notifyActivate() {
    }
    virtual void notifyDeactivate() {
    }
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
    virtual bool isEnableToReset() const {
        return false;
    }
    virtual bool isEnableToRoundLeft() const {
        return false;
    }
    virtual bool isEnableToRoundRight() const {
        return false;
    }
    virtual void roundLeft() {
    }
    virtual void roundRight() {
    }

    void owned(CameraDirector*);
    void released();
    void activate();
    void deactivate();

    CameraDirector* mDirector;
    CameraPoseParam* mPoseParam;
    bool mIsActivated;
    bool mRequestLOfsReset;
    Pos3f mMatrix;
};
