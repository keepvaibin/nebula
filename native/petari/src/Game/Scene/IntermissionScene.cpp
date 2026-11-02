#include "Game/Scene/IntermissionScene.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
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
    J2DOrthoGraphSimple graph;
    graph.setPort();
    graph.setColor(static_cast< const NebulaColor8& >(NebulaColor8(Color8(0, 0, 0, 255))));

    f32 height = static_cast< s32 >(NebulaFrameClock::cCanvasHeight);
    f32 width = MR::getScreenWidth();
    graph.fillBox(0.0f, 0.0f, width, height);
}

void IntermissionScene::setCurrentSceneControllerState(const char* pState, ...) {
    va_list list;
    va_start(list, pState);
    nebula_vsnprintf(mState, sizeof(mState), pState, list);
    va_end(list);
    _54 = 0;
}
