#include "Game/NPC/SignBoard.hpp"
#include "Game/AreaObj/FollowCollisionArea.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/VectorUtil.hpp"
#include "Game/System/LumaCore.hpp"

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
    caps.mShadowSize = 30.0f;
    caps.mSensorSize = 100.0f;
    caps.mSensorOffset.x = 0.0f;
    caps.mSensorOffset.y = 130.0f;
    caps.mSensorOffset.z = 0.0f;
    caps.mLightCtrl = false;
    caps.mBinder = 0;
    initialize(rIter, caps);

    getSensor("Body")->setType(ATYPE_EYE);

    new FollowCollisionArea(Vec3f(130.0f, 160.0f, 20.0f), getBaseMtx(), 80.0f);

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
