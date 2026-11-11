#include "Game/Boss/DinoPackunTailRoot.hpp"
#include "Game/Boss/DinoPackunAction.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/System/LumaCore.hpp"

typedef JointControlDelegator< DinoPackunTailRoot > Delegator;

typedef bool (DinoPackunTailRoot::*func)(Pos3f*, const JointControllerInfo&);

DinoPackunTailRoot::DinoPackunTailRoot(const char* name, DinoPackun* pPackun) : DinoPackunTailNode(name, pPackun) {
    lumacore::setClass(this, lumacore::kClassId< DinoPackunTailRoot >);
}

void DinoPackunTailRoot::init(const TableIter& /*rIter*/) {
    MR::invalidateClipping(this);
    makeActorAppeared();
}

JointController* DinoPackunTailRoot::createJointControllerOwn(LiveActor* pActor, const char* pJointName) {
    return MR::createJointDelegator(this, pActor, &DinoPackunTailRoot::calcJointDirection, (func) nullptr, pJointName);
}

// The root follows its animated joint.
bool DinoPackunTailRoot::calcJointDirection(Pos3f* pOut, const JointControllerInfo& rInfo) {
    Pos3f jointMtx = pOut;
    MR::orthogonalize(&jointMtx);
    jointMtx.getZDir(mJointFront);
    pOut->getTrans(mPosition);
    return turnJointLocalXDir(pOut, rInfo);
}

// The root is not pushed by the tail.
void DinoPackunTailRoot::addNodeVelocity(const Vec3f& /*rVel*/) {}
