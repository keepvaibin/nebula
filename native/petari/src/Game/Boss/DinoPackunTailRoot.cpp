#include "Game/Boss/DinoPackunTailRoot.hpp"
#include "Game/Boss/DinoPackunAction.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"

typedef JointControlDelegator< DinoPackunTailRoot > Delegator;

typedef bool (DinoPackunTailRoot::*func)(Pos3f*, const JointControllerInfo&);

DinoPackunTailRoot::DinoPackunTailRoot(const char* name, DinoPackun* pPackun) : DinoPackunTailNode(name, pPackun) {
}

void DinoPackunTailRoot::init(const JMapInfoIter& rIter) {
    MR::invalidateClipping(this);
    makeActorAppeared();
}

JointController* DinoPackunTailRoot::createJointControllerOwn(LiveActor* pActor, const char* pJointName) {
    return MR::createJointDelegator(this, pActor, &DinoPackunTailRoot::calcJointDirection, (func) nullptr, pJointName);
}

bool DinoPackunTailRoot::calcJointDirection(Pos3f* pOut, const JointControllerInfo& rInfo) {
    Pos3f stack_20 = pOut;
    MR::orthogonalize(&stack_20);
    Vec3f stack_14;
    stack_20.getZDir(stack_14);
    _A8.set(stack_14);
    Vec3f v12;
    pOut->getTrans(v12);
    mPosition.set(v12);
    return turnJointLocalXDir(pOut, rInfo);
}

void DinoPackunTailRoot::addNodeVelocity(const Vec3f& rVel) {
    return;
}
