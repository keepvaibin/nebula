#include "Game/Screen/ScreenPreserver.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/LumaCore.hpp"

ScreenPreserver::ScreenPreserver() : NameObj(""), mIsActive(false) {
    lumacore::setClass(this, lumacore::kClassId< ScreenPreserver >);
}

void ScreenPreserver::captureIfAllow() {
    MR::captureScreenIfAllow("GameScreen");
}

void ScreenPreserver::draw() const {
    if (!mIsActive) {
        return;
    }

    MR::setupScreen2D();
    GameTexture tex(MR::getScreenResTIMG());
    MR::drawScreenPicture(tex);
}

void ScreenPreserver::activate() {
    if (!mIsActive) {
        mIsActive = true;
        MR::startToCaptureScreen("GameScreen");
    }
}

void ScreenPreserver::deactivate() {
    if (mIsActive) {
        mIsActive = false;
        MR::endToCaptureScreen("GameScreen");
    }
}
