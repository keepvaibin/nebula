#include "Game/Scene/IntermissionScene.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <cstdarg>

IntermissionScene::IntermissionScene() : Scene("IntermissionScene") {
}

void IntermissionScene::draw() const {
    MR::setupScreen2D();
    MR::drawScreenColor(Color8(0, 0, 0, 255));
}

void IntermissionScene::setCurrentSceneControllerState(const char* pState, ...) {
    va_list list;
    va_start(list, pState);
    mState.vformat(pState, list);
    va_end(list);
    JUTNativeLog("scene: %s\n", mState.c_str());
}
