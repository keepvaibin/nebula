#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

class SkeletalFishRailControl;
struct JointControllerInfo;

class SkeletalFishJointCalc {
public:
    static void calcJoint(TPos3f*, const TPos3f*, SkeletalFishRailControl*, const JointControllerInfo&);
};
