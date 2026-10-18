#include "Game/Screen/SystemWipeHolder.hpp"
#include "Game/Screen/WipeFade.hpp"
#include "Game/Screen/WipeHolderBase.hpp"
#include "Game/Screen/WipeRing.hpp"
#include "Game/Util.hpp"
#include <JSystem/J2DGraph/J2DPicture.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>

SystemWipeHolder::SystemWipeHolder() : WipeHolderBase(4, "\203\126\203\130\203\145\203\200\203\217\203\103\203\166\225\333\216\235" /* システムワイプ保持 */), _1C(false) {
}

void SystemWipeHolder::init(const JMapInfoIter& rIter) {
    addWipeLayout(new WipeFade("\203\164\203\106\201\133\203\150\203\217\203\103\203\166" /* フェードワイプ */, Color8(0, 0, 0, 255)));
    addWipeLayout(new WipeRing(false, "\211\176\203\217\203\103\203\166" /* 円ワイプ */));
    addWipeLayout(new WipeFade("\224\222\203\164\203\106\201\133\203\150\203\217\203\103\203\166" /* 白フェードワイプ */, Color8(255, 255, 255, 255)));
}

bool SystemWipeHolder::isCurrentAlive() const {
    return getCurrent() != nullptr && !MR::isDead(getCurrent());
}

void SystemWipeHolder::movement() {
    if (_1C) {
        if (!isCurrentAlive() || !getCurrent()->isWipeOut()) {
            _1C = false;
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

void SystemWipeHolder::setWipeRingCenter(const TVec3f& rCenter) {
    static_cast< WipeRing* >(findWipe("\211\176\203\217\203\103\203\166" /* 円ワイプ */))->setCenterPos(rCenter);
}

void SystemWipeHolder::startGameScreenCapture() {
    _1C = true;
    MR::startToCaptureScreen("SystemWipe");
}

void SystemWipeHolder::drawGameScreenCapture() const {
    if (!_1C) {
        return;
    }

    J2DOrthoGraphSimple graph;
    graph.setPort();

    JUTTexture texture(MR::getScreenResTIMG(), 0);

    J2DPicture picture(&texture);
    f32 width = MR::getScreenWidth();
    // inlined MR::getscreenHeight
    f32 height = (s32)JUTVideo::getManager()->getEfbHeight();

    picture.draw(0.0f, 0.0f, width, height, false, false, false);
}

void SystemWipeHolder::updateWipe(const char* pWipeName) {
    _1C = false;
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
