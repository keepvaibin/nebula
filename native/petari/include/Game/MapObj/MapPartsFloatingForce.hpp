#pragma once

#include "Game/MapObj/MapPartsFunction.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class SpringValue;

class SpringStep {
public:
    SpringStep(f32, f32, f32);

    void addSpringVelocity(f32);
    void setSpringBaseValue(f32);

    f32 mCondition;
    f32 mSpeed;
    f32 mAngle;
    SpringValue* mSpringValue;
};

class MapPartsFloatingForce : public MapPartsFunction {
public:
    MapPartsFloatingForce(LiveActor* pActor);

    virtual ~MapPartsFloatingForce();
    virtual void init(const JMapInfoIter& rIter);
    virtual void start();

    void setObjectName(const char* pName);
    void initForNormalMotion(const JMapInfoIter& rIter);
    void initForSpringMotion(const JMapInfoIter& rIter);
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
    f32 _20;
    f32 mRotateAngle;
    f32 mRotateSpeed;
    s32 mRotateAccelType;
    TVec3f _30;
    TVec3f mUpDirection;
    f32 _48;
    f32 _4C;
    TVec3f _50;
    f32 _5C;
};
