#include "Game/Player/MarioNullBck.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"

MarioNullBck::MarioNullBck(const char* pName) : LiveActor(pName) {
}

MarioNullBck::~MarioNullBck() {
}

void MarioNullBck::init(const TableIter& rIter) {
    initModelManagerWithAnm("MarioDummyItem", nullptr, false);
    MR::startBck(this, "PickUpBall");
    makeActorDead();
}

void MarioNullBck::appear() {
    mLastFrame = 0;
    LiveActor::appear();
}

void MarioNullBck::getLastPos(Vec3f* pDest) {
    MR::copyJointPos(this, "DummyItem", pDest);
}

bool MarioNullBck::getFramePos(f32 a1, Vec3f* a2, Vec3f* a3) {
    bool ret = false;
    if (MR::isDead(this)) {
        return true;
    }
    if (a1 > MR::getBckCtrl(this)->getEnd() - 0.01f) {
        a1 = MR::getBckCtrl(this)->getEnd() - 0.01f;
        ret = true;
    }
    if ((f32)mLastFrame >= a1) {
        ret = true;
        a1 = MR::getBckCtrl(this)->getEnd();
    }
    mLastFrame = (s16)(s32)(a1);
    MR::setBckFrame(this, a1);
    MR::setBckRate(this, 0.0f);
    MR::updateModelManager(this);
    MR::calcAnimModelManager(this);
    MR::copyJointPos(this, "DummyItem", a2);
    MR::makeRTFromMtxPtr(a2, a3, MR::getJointMtx(this, "DummyPoly"), true);
    return ret;
}
