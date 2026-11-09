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

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

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
