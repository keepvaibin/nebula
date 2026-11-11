#include "Game/Screen/OneUpBoard.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sOffsetY, 60.0f);
    NEBULA_TUNABLE(f32, sOffsetYMin, 160.0f);
};  // namespace

namespace NrvOneUpBoard {
    NEW_NERVE(HostTypeWait, OneUpBoard, Wait);
    NEW_NERVE(HostTypeAppear, OneUpBoard, Appear);
};  // namespace NrvOneUpBoard

OneUpBoard::OneUpBoard() : LayoutActor("1UPボード", true) {
    lumacore::setClass(this, lumacore::kClassId< OneUpBoard >);
}

void OneUpBoard::init(const TableIter& /*rIter*/) {
    initLayoutManager("OneUp", 2);
    initNerve(GET_NERVE(OneUpBoard, HostTypeWait));
    MR::connectToSceneLayout(this);
    MR::registerDemoSimpleCastAll(this);
    initEffectKeeper(1, "OneUp", nullptr);
    kill();
}

void OneUpBoard::popup(s32 oneUpNum) {
    MR::setTextBoxArgNumberRecursive(this, "OneUp", oneUpNum, 0);
    appear();
}

void OneUpBoard::appear() {
    LayoutActor::appear();
    setNerve(GET_NERVE(OneUpBoard, HostTypeAppear));
}

void OneUpBoard::calcPos() {
    Vec2f pos;

    MR::calcScreenPosition(&pos, *MR::getPlayerCenterPos());

    pos.y -= ::sOffsetY;
    pos.y = pos.y >= ::sOffsetYMin ? pos.y : ::sOffsetYMin;

    setTrans(pos);
}

void OneUpBoard::control() {
}

void OneUpBoard::exeWait() {
}

void OneUpBoard::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Appear", 0);
        MR::emitEffect(this, "OneUpGet");
    }

    calcPos();

    if (MR::isAnimStopped(this, 0)) {
        kill();
    }
}
