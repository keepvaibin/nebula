#include "Game/MapObj/SnowplowSwitch.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sSensorRadius = 70.0f;
};  // namespace

SnowplowSwitch::SnowplowSwitch(const char* pName) : LiveActor(pName), mIsDiscovered() {
    lumacore::setClass(this, lumacore::kClassId< SnowplowSwitch >);
}

bool SnowplowSwitch::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_ASK_HIDDEN_BY_SNOW) {
        return !mIsDiscovered;
    }

    if (msg == ACTMES_NOTIFY_DISCOVER_SNOW && !mIsDiscovered) {
        mIsDiscovered = true;

        MR::onSwitchA(this);

        return true;
    }

    return false;
}

void SnowplowSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneMapObjMovement(this);
    MR::invalidateClipping(this);
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "Body", 8, ::sSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::needStageSwitchWriteA(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}
