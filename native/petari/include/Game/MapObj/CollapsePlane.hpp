#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AnimScaleController;
class JointController;
struct JointControllerInfo;
class WalkerStateBindStarPointer;

class CollapsePlane : public MapObjActor {
public:
    /// @brief Creates a new `CollapsePlane`.
    /// @param pName A pointer to the null-terminated name of the object.
    CollapsePlane(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void exeCollapse();
    void exeDPDStop();
    void exeEnd();
    bool calcJointPlane(TPos3f*, const JointControllerInfo&);
    bool tryDPDStop();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStateBindStartPointer;
    JointController* mJointController;
    s32 mCollapseStep;
    s32 mCollapseTime;
};
