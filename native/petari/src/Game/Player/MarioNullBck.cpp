#include "Game/Player/MarioNullBck.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getFramePos_f1, "getFramePos.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_getFramePos_f2, "getFramePos.f2", 0.01f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_getFramePos_setBckRate_1, "getFramePos.setBckRate.1", 0.0f);

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
    if (frame > MR::getBckCtrl(this)->getEnd() - cNum_getFramePos_f1) {
        frame = MR::getBckCtrl(this)->getEnd() - cNum_getFramePos_f2;
        ret = true;
    }
    if ((f32)mLastFrame >= frame) {
        ret = true;
        frame = MR::getBckCtrl(this)->getEnd();
    }
    mLastFrame = (s16)(s32)(frame);
    MR::setBckFrame(this, frame);
    MR::setBckRate(this, cNum_getFramePos_setBckRate_1);
    MR::updateModelManager(this);
    MR::calcAnimModelManager(this);
    MR::copyJointPos(this, "DummyItem", pPos);
    MR::makeRTFromMtxPtr(pPos, pRot, MR::getJointMtx(this, "DummyPoly"), true);
    return ret;
}
