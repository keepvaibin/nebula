#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class JointController;
struct JointControllerInfo;
class WalkerStateBindStarPointer;

class CollapsePlane : public MapObjActor {
public:
    CollapsePlane(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void exeCollapse();
    void exePointerStop();
    void exeEnd();
    bool calcJointPlane(Pos3f*, const JointControllerInfo&);
    bool tryPointerStop();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStateBindStartPointer;
    JointController* mJointController;
    s32 mCollapseStep;
    s32 mCollapseTime;
};
