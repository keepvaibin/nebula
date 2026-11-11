#include "Game/NPC/SignBoard.hpp"
#include "Game/AreaObj/FollowCollisionArea.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/VectorUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 80.0f);

namespace NrvSignBoard {
    NEW_NERVE(SignBoardNrvWait, SignBoard, Wait);
};  // namespace NrvSignBoard

SignBoard::SignBoard(const char* pName) : NPCActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SignBoard >);
}

void SignBoard::init(const TableIter& rIter) {
    NPCActorCaps caps("SignBoard");
    caps.setDefault();
    caps.mWaitNerve = GET_NERVE(SignBoard, SignBoardNrvWait);
    caps.mShadowSize = cNum_init_f1;
    caps.mSensorSize = cNum_init_f2;
    caps.mSensorOffset.x = 0.0f;
    caps.mSensorOffset.y = cNum_init_f3;
    caps.mSensorOffset.z = 0.0f;
    caps.mLightCtrl = false;
    caps.mBinder = 0;
    initialize(rIter, caps);

    getSensor("Body")->setType(ATYPE_EYE);

    new FollowCollisionArea(Vec3f(cNum_init_f4, cNum_init_f5, cNum_init_f6), getBaseMtx(), cNum_init_f7);

    Vec3f up;
    MR::getUpVecFromQuat(&up, mPoseQuat);

    MR::setShadowProjection(this, nullptr, mPosition, up, true);
    MR::offCalcShadow(this, nullptr);
}

void SignBoard::control() {
}

void SignBoard::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

void SignBoard::exeWait() {
    MR::tryTalkNearPlayer(mMsgCtrl);
}
