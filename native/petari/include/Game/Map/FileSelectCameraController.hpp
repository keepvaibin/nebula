#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FileSelectCameraController : public LiveActor {
public:
    /// @brief Creates a new `FileSelectCameraController`.
    /// @param pName A pointer to the null-terminated name of the object.
    FileSelectCameraController(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void control();

    void goToFarPoint();
    void goToNearPoint(const TVec3f&);
    bool isAtFarPoint() const;
    bool isAtNearPoint() const;
    bool isToOrAtFarPoint() const;
    bool isToOrAtNearPoint() const;

    void exeTitle();
    void exeMoveToFarPoint();
    void exeFarPoint();
    void exeMoveToNearPoint();
    void exeNearPoint();

private:
    TVec3f mNearTarget;
    TVec3f mPrevNearTarget;
    TVec3f mWPoint;
    TVec3f mPrevWPoint;
    f32 mFovy;
    f32 mPrevFovy;
    TVec3f mUpVec;
};
