#include "Game/Boss/TombSpiderThreadAttacher.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvTombSpiderThreadAttacher {
    NEW_NERVE(TombSpiderThreadAttacherNrvFree, TombSpiderThreadAttacher, Free);
    NEW_NERVE(TombSpiderThreadAttacherNrvAttach, TombSpiderThreadAttacher, Attach);
};  // namespace NrvTombSpiderThreadAttacher

TombSpiderThreadAttacher::TombSpiderThreadAttacher(LiveActor* pActor, const char* pJointName, f32 radius, f32 offset)
    : LiveActor("巣の付着点"), mJointMtx(MR::getJointMtx(pActor, pJointName)), mRadius(radius), mZOffset(offset) {
    lumacore::setClass(this, lumacore::kClassId< TombSpiderThreadAttacher >);
}

void TombSpiderThreadAttacher::init(const TableIter& /*rIter*/) {
    MR::connectToSceneEnemyMovement(this);
    initNerve(GET_NERVE(TombSpiderThreadAttacher, TombSpiderThreadAttacherNrvFree));
    MR::invalidateClipping(this);
    makeActorDead();
}

void TombSpiderThreadAttacher::exeFree() {
    Vec3f pos(mJointMtx[0][3], mJointMtx[1][3], mJointMtx[2][3] - mZOffset);
    MR::tryPushSpiderThread(pos, mRadius);
}

void TombSpiderThreadAttacher::exeAttach() {
}
