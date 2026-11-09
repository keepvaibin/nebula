#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <nebula_geometry.hpp>

class SpringValue;

class SpringStep {
public:
    SpringStep(f32, f32, f32);

    void addSpringVelocity(f32);
    void setSpringBaseValue(f32);

    f32 mSpeed;
    f32 mAngle;
    SpringValue* mSpringValue;
};

class MapPartsFloatingForce : public MapPartsFunction {
public:
    MapPartsFloatingForce(LiveActor* pActor);

    virtual void init(const TableIter& rIter) override;
    virtual void start() override;

    void setObjectName(const char* pName);
    void initForNormalMotion(const TableIter& rIter);
    void initForSpringMotion(const TableIter& rIter);
    void updateVelocity();
    void updateVelocitySpring();
    bool tryOn();
    bool tryReturn();
    void exeWait();
    void exeMove();
    void exeMoveSpring();
    void exeMoveReturn();

    SpringStep* mSpringStep;
    const char* mObjectName;
    f32 mMaxDepth;
    f32 mRotateAngle;
    f32 mRotateSpeed;
    s32 mRotateAccelType;
    Vec3f mBasePos;
    Vec3f mUpDirection;
    f32 mSinkDepth;
    f32 mSinkSpeed;
    Vec3f mVelocity;
    f32 mRideSinkSpeed;
};
