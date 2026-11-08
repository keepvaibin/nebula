#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class DinoPackun;
template < typename T >
class JointControlDelegator;
class JointController;
struct JointControllerInfo;

class DinoPackunTailNode : public LiveActor {
public:
    DinoPackunTailNode(const char*, DinoPackun*);

    virtual const Vec3f* getNodeDirection() const;
    virtual void requestLockPosition();
    virtual void requestUnLockPosition();
    virtual void addNodeVelocity(const Vec3f&);
    virtual JointController* createJointControllerOwn(LiveActor*, const char*) = 0;

    void createJointController(LiveActor*, const char*);
    void resetJoint();
    f32 getLinkLength() const;
    f32 getKeepBendPower() const;
    bool preCalcJoint(Pos3f*, const JointControllerInfo&);
    bool turnJointLocalXDir(Pos3f*, const JointControllerInfo&);
    bool calcJointScale(Pos3f*, const JointControllerInfo&);
    void registerPreCalcJointCallBack();
    void registerJointCallBack();
    void lockPosition();
    void unLockPosition();
    void addNodeVelocityHost(const Vec3f&);

    DinoPackun* mParent;
    Vec3f mNodeDirection;
    Vec3f mJointPos;
    Vec3f mJointFront;
    LiveActor* mPrevNode;
    LiveActor* mNextNode;
    MtxPtr mJointMtx;
    JointControlDelegator< DinoPackunTailNode >* mPreCalcJointCtrl;
    JointControlDelegator< DinoPackunTailNode >* mJointCtrl;
    f32 mLinkLength;
    f32 mKeepBendPower;
    bool mIsLocked;
};
