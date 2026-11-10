#include "Game/MapObj/InvisiblePolygonObjGCapture.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/System/LumaCore.hpp"

InvisiblePolygonObjGCapture::InvisiblePolygonObjGCapture(const char* pName) : InvisiblePolygonObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< InvisiblePolygonObjGCapture >);
}

bool InvisiblePolygonObjGCapture::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return msg == ACTMES_IS_NO_BREAK_G_CAPTURE;
}
