#pragma once

#include <nebula_geometry.hpp>

class SkeletalFishRailControl;
struct JointControllerInfo;

class SkeletalFishJointCalc {
public:
    static void calcJoint(Pos3f*, const Pos3f*, SkeletalFishRailControl*, const JointControllerInfo&);
};
