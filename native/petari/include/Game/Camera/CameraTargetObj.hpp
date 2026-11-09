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

    virtual void init(const TableIter&) override {};

    virtual void movement() override = 0;

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

    virtual void resetStatus() {
    }
};

class CameraTargetActor : public CameraTargetObj {
public:
    CameraTargetActor(const char*);

    virtual void movement() override;

    virtual const Vec3f& getPosition() const override;
    virtual const Vec3f& getUpVec() const override;
    virtual const Vec3f& getFrontVec() const override;
    virtual const Vec3f& getSideVec() const override;
    virtual const Vec3f& getLastMove() const override;
    virtual const Vec3f& getGroundPos() const override;
    virtual const Vec3f& getGravityVector() const override;

    virtual f32 getRadius() const override {
        return 150.0f;
    }

    virtual CubeCameraArea* getCubeCameraArea() const override;
    virtual Triangle* getGroundTriangle() const override;

    const LiveActor* mActor;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mSide;
    CubeCameraArea* mCameraArea;
};

class CameraTargetPlayer : public CameraTargetObj {
public:
    CameraTargetPlayer(const char*);

    virtual void movement() override;

    virtual const Vec3f& getPosition() const override;
    virtual const Vec3f& getUpVec() const override;
    virtual const Vec3f& getFrontVec() const override;
    virtual const Vec3f& getSideVec() const override;
    virtual const Vec3f& getLastMove() const override;
    virtual const Vec3f& getGroundPos() const override;
    virtual const Vec3f& getGravityVector() const override;

    virtual bool isTurning() const override;
    virtual bool isJumping() const override;
    virtual bool isLongDrop() const override;
    virtual bool isFastDrop() const override;
    virtual bool isFastRise() const override;
    virtual bool isWaterMode() const override;
    virtual bool isOnWaterSurface() const override;
    virtual bool isFooFighterMode() const override;
    virtual u32 getSpecialMode() const override;
    virtual bool isCameraStateOn(u32) const override;
    virtual CubeCameraArea* getCubeCameraArea() const override;
    virtual Triangle* getGroundTriangle() const override;
    virtual GravityInfo* getGravityInfo() const override;

    const MarioActor* mActor;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mGravity;
    Vec3f mGroundPos;
    CubeCameraArea* mCameraArea;
    Triangle* mGroundTriangle;
    s32 mPlayerMovementTimer;
    bool mIsPlayerMoving;
};

class CameraTargetDemoActor : public LiveActor {
public:
    CameraTargetDemoActor(MtxPtr, const char*);

    virtual void init(const TableIter& rIter) override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mMtx.mMtx);
    }

    void setTargetMtx(MtxPtr);

    Pos3f mMtx;
};
