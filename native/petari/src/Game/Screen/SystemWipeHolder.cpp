#include "Game/Render/GameTexture.hpp"
#include "Game/Screen/SystemWipeHolder.hpp"
#include "Game/Screen/WipeFade.hpp"
#include "Game/Screen/WipeHolderBase.hpp"
#include "Game/Screen/WipeRing.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/LumaCore.hpp"

SystemWipeHolder::SystemWipeHolder() : WipeHolderBase(4, "システムワイプ保持"), mIsCapturing(false) {
    lumacore::setClass(this, lumacore::kClassId< SystemWipeHolder >);
}

void SystemWipeHolder::init(const TableIter& /*rIter*/) {
    addWipeLayout(new WipeFade("フェードワイプ", Color8(0, 0, 0, 255)));
    addWipeLayout(new WipeRing(false, "円ワイプ"));
    addWipeLayout(new WipeFade("白フェードワイプ", Color8(255, 255, 255, 255)));
}

bool SystemWipeHolder::isCurrentAlive() const {
    return getCurrent() != nullptr && !MR::isDead(getCurrent());
}

void SystemWipeHolder::movement() {
    if (mIsCapturing) {
        if (!isCurrentAlive() || !getCurrent()->isWipeOut()) {
            mIsCapturing = false;
            MR::endToCaptureScreen("SystemWipe");
        }
    }

    if (isCurrentAlive()) {
        getCurrent()->movement();
    }
}

void SystemWipeHolder::calcAnim() {
    if (isCurrentAlive()) {
        getCurrent()->calcAnim();
    }
}

void SystemWipeHolder::draw() const {
    MR::captureScreenIfAllow("SystemWipe");
    drawGameScreenCapture();

    if (isCurrentAlive()) {
        getCurrent()->draw();
    }
}

void SystemWipeHolder::setWipeRingCenter(const Vec3f& rCenter) {
    static_cast< WipeRing* >(findWipe("円ワイプ"))->setCenterPos(rCenter);
}

void SystemWipeHolder::startGameScreenCapture() {
    mIsCapturing = true;
    MR::startToCaptureScreen("SystemWipe");
}

void SystemWipeHolder::drawGameScreenCapture() const {
    if (!mIsCapturing) {
        return;
    }

    MR::setupScreen2D();
    GameTexture texture(MR::getScreenResTIMG());
    MR::drawScreenPicture(texture);
}

void SystemWipeHolder::updateWipe(const char* pWipeName) {
    mIsCapturing = false;
    MR::endToCaptureScreen("SystemWipe");
    WipeHolderBase::updateWipe(pWipeName);
}

namespace MR {
    SystemWipeHolder* createSystemWipeHolder() {
        SystemWipeHolder* newHolder = new SystemWipeHolder();
        newHolder->initWithoutIter();
        return newHolder;
    }
};  // namespace MR
