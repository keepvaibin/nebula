#pragma once

#include <nebula_geometry.hpp>

class DinoPackunTailNode;

class DinoPackunTail {
public:
    DinoPackunTail(u32 nodeCount);

    void addTailNode(DinoPackunTailNode* pNode);
    void activate();
    void deactivate();
    void onMovement();
    void lockNodePosition(u32 idx);
    void unlockNodePosition(u32 idx);
    void lockEndNodePosition();
    void unlockEndNodePosition();
    void registerPreCalcJointCallBack();
    void registerJointCallBack();
    void updateJoint();
    void addAccelKeepBend();
    void addAccelKeepDistance();
    void addAccelToBck();
    void getTailNodePosition(Vec3f* pPos, s32 index) const;
    void getEndTailPosition(Vec3f* pPos) const;
    DinoPackunTailNode* getNode(u32 index) const;

    DinoPackunTailNode** mNodes;
    u32 mNumNodes;
    f32 mBendRate;
    f32 mDistanceRate;
    f32 mHostVelocityRate;
    bool mIsActive;
};
