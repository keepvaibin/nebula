#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class CubeCameraArea;
class GravityInfo;
class MarioActor;
class Triangle;

class CameraTargetObj : public NameObj {
public:
    CameraTargetObj(const char*);

    virtual void init(const TableIter&) {};

    virtual void movement() = 0;

    virtual const Vec3f& getPosition() const = 0;
    virtual const Vec3f& getUpVec() const = 0;
    virtual const Vec3f& getFrontVec() const = 0;
    virtual const Vec3f& getSideVec() const = 0;
    virtual const Vec3f& getLastMove() const = 0;
    virtual const Vec3f& getGroundPos() const = 0;
    virtual const Vec3f& getGravityVector() const = 0;

    virtual f32 getRadius() const {
        return 150.0f;
    }

    virtual bool isTurning() const {
        return false;
    }

    virtual bool isJumping() const {
        return false;
    }

    virtual bool isLongDrop() const {
        return false;
    }

    virtual bool isFastDrop() const {
        return false;
    }

    virtual bool isFastRise() const {
        return false;
    }

    virtual bool isWaterMode() const {
        return false;
    }

    virtual bool isOnWaterSurface() const {
        return false;
    }

    virtual bool isBeeMode() const {
        return false;
    }

    virtual bool isFooFighterMode() const {
        return false;
    }

    virtual u32 getSpecialMode() const {
        return 0;
    }

    virtual bool isCameraStateOn(u32) const {
        return false;
    };
    virtual CubeCameraArea* getCubeCameraArea() const {
        return nullptr;
    }

    virtual Triangle* getGroundTriangle() const {
        return nullptr;
    }

    virtual GravityInfo* getGravityInfo() const {
        return nullptr;
    }

    virtual Pos3f* getMapBaseMtx() const {
        return nullptr;
    }

    virtual void resetStatus() {
    }
};

class CameraTargetActor : public CameraTargetObj {
public:
    CameraTargetActor(const char*);

    virtual void movement();

    virtual const Vec3f& getPosition() const;
    virtual const Vec3f& getUpVec() const;
    virtual const Vec3f& getFrontVec() const;
    virtual const Vec3f& getSideVec() const;
    virtual const Vec3f& getLastMove() const;
    virtual const Vec3f& getGroundPos() const;
    virtual const Vec3f& getGravityVector() const;

    virtual f32 getRadius() const {
        return 150.0f;
    }

    virtual CubeCameraArea* getCubeCameraArea() const;
    virtual Triangle* getGroundTriangle() const;

    const LiveActor* mActor;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mSide;
    CubeCameraArea* mCameraArea;
};

class CameraTargetPlayer : public CameraTargetObj {
public:
    CameraTargetPlayer(const char*);

    virtual void movement();

    virtual const Vec3f& getPosition() const;
    virtual const Vec3f& getUpVec() const;
    virtual const Vec3f& getFrontVec() const;
    virtual const Vec3f& getSideVec() const;
    virtual const Vec3f& getLastMove() const;
    virtual const Vec3f& getGroundPos() const;
    virtual const Vec3f& getGravityVector() const;

    virtual bool isTurning() const;
    virtual bool isJumping() const;
    virtual bool isLongDrop() const;
    virtual bool isFastDrop() const;
    virtual bool isFastRise() const;
    virtual bool isWaterMode() const;
    virtual bool isOnWaterSurface() const;
    virtual bool isFooFighterMode() const;
    virtual u32 getSpecialMode() const;
    virtual bool isCameraStateOn(u32) const;
    virtual CubeCameraArea* getCubeCameraArea() const;
    virtual Triangle* getGroundTriangle() const;
    virtual GravityInfo* getGravityInfo() const;
    virtual Pos3f* getMapBaseMtx() const;

    const MarioActor* mActor;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mGravity;
    Vec3f mGroundPos;
    CubeCameraArea* mCameraArea;
    Triangle* mGroundTriangle;
    u16 mPlayerMovementTimer;
    bool mIsPlayerMoving;
};

class CameraTargetDemoActor : public LiveActor {
public:
    CameraTargetDemoActor(MtxPtr, const char*);

    virtual void init(const TableIter& rIter);
    virtual MtxPtr getBaseMtx() const {
        return (const MtxPtr)mMtx.mMtx;
    }

    void setTargetMtx(MtxPtr);

    Pos3f mMtx;
};
