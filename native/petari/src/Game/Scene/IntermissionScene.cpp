#include "Game/Scene/IntermissionScene.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <cstdio>
#include <cstdarg>

IntermissionScene::IntermissionScene() : Scene("IntermissionScene") {
    _54 = 0;
    mState[0] = 0;
}

void IntermissionScene::update() {
    _54++;
}

void IntermissionScene::draw() const {
    MR::setupScreen2D();
    MR::drawScreenColor(Color8(0, 0, 0, 255));
}

void IntermissionScene::setCurrentSceneControllerState(const char* pState, ...) {
    va_list list;
    va_start(list, pState);
    nebula_vsnprintf(mState, sizeof(mState), pState, list);
    va_end(list);
    JUTNativeLog("scene: %s\n", mState);
    _54 = 0;
}
