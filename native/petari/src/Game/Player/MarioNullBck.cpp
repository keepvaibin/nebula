#include "Game/Player/MarioNullBck.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/System/LumaCore.hpp"

MarioNullBck::MarioNullBck(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< MarioNullBck >);
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

bool MarioNullBck::getFramePos(f32 frame, Vec3f* pPos, Vec3f* pRot) {
    bool ret = false;
    if (MR::isDead(this)) {
        return true;
    }
    if (frame > MR::getBckCtrl(this)->getEnd() - 0.01f) {
        frame = MR::getBckCtrl(this)->getEnd() - 0.01f;
        ret = true;
    }
    if ((f32)mLastFrame >= frame) {
        ret = true;
        frame = MR::getBckCtrl(this)->getEnd();
    }
    mLastFrame = (s16)(s32)(frame);
    MR::setBckFrame(this, frame);
    MR::setBckRate(this, 0.0f);
    MR::updateModelManager(this);
    MR::calcAnimModelManager(this);
    MR::copyJointPos(this, "DummyItem", pPos);
    MR::makeRTFromMtxPtr(pPos, pRot, MR::getJointMtx(this, "DummyPoly"), true);
    return ret;
}
