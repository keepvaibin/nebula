#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FileSelectCameraController : public LiveActor {
public:
    FileSelectCameraController(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void control();

    void goToFarPoint();
    void goToNearPoint(const Vec3f&);
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
    Vec3f mNearTarget;
    Vec3f mWPoint;
    Vec3f mPrevWPoint;
    f32 mFovy;
    f32 mPrevFovy;
    Vec3f mUpVec;
};
