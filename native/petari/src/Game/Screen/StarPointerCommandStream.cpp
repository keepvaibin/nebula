#include "Game/Screen/StarPointerCommandStream.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sMinWidth, 6.0f);
    NEBULA_TUNABLE(f32, sMaxWidth, 32.0f);
    NEBULA_TUNABLE(f32, sMinLineWidthDistance, 40.0f);
    NEBULA_TUNABLE(f32, sMaxLineWidthDistance, 400.0f);
    NEBULA_TUNABLE(f32, hDefaultLength, 80.0f);
    NEBULA_TUNABLE(f32, hDefaultWidth, 30.0f);
};  // namespace

namespace NrvStarPointerCommandStream {
    NEW_NERVE(HostTypeNrvWait, StarPointerCommandStream, Wait);
    NEW_NERVE(HostTypeNrvSignal, StarPointerCommandStream, Signal);
};  // namespace NrvStarPointerCommandStream

StarPointerCommandStream::StarPointerCommandStream(const Vec2f* pScreenPos)
    : LayoutActor("スターポインタ指示線", true), mOffScreenTime(), mScreenPos(pScreenPos), mWorldPos(), mPadChannel(-1), mColor() {
    lumacore::setClass(this, lumacore::kClassId< StarPointerCommandStream >);
}

void StarPointerCommandStream::start(const Vec3f* pPos, bool /*b*/) {
    mWorldPos = pPos;
    MR::showLayout(this);
    MR::startAnim(this, "Wait", 0);
    Vec2f screenPos;
    MR::calcScreenPosition(&screenPos, *mWorldPos);
    calcPose(screenPos, screenPos);
    calcAnim();
}

void StarPointerCommandStream::show() {
    MR::showLayout(this);
}

void StarPointerCommandStream::hide() {
    MR::setPaneScale(this, 0.0f, 0.0f, "PicRibbon");
    MR::hideLayout(this);
}

void StarPointerCommandStream::clear() {
    mWorldPos = nullptr;
}

void StarPointerCommandStream::init(const TableIter& /*rIter*/) {
    initLayoutManager("CommandStream", 1);
    MR::createAndAddPaneCtrl(this, "CommandStream", 1);
    MR::createAndAddPaneCtrl(this, "PicRibbon", 1);
    MR::createAndAddPaneCtrl(this, "PicArrow", 1);
    MR::createAndAddPaneCtrl(this, "Arrow", 1);
    MR::setFollowPos(&mFollowPos, this, "CommandStream");
    initNerve(GET_NERVE(StarPointerCommandStream, HostTypeNrvWait));
    appear();
    MR::setPaneScale(this, 0.0f, 0.0f, "PicRibbon");
    MR::hideLayout(this);
}

void StarPointerCommandStream::control() {
    if (MR::isStarPointerInScreen(mPadChannel)) {
        mOffScreenTime = 0;
    } else {
        mOffScreenTime++;
        if ((mOffScreenTime & 0x10) != 0 && (mOffScreenTime & 0x0F) == 0) {
            MR::startSystemSE("SE_SY_DPD_DRAG_RANGE_OUT");
        }
    }

    Vec2f screenPos;
    MR::calcScreenPosition(&screenPos, *mWorldPos);
    Vec2f insidePos = *mScreenPos;
    StarPointerFunction::forceInsideScreenEdge(&insidePos);
    calcPose(screenPos, insidePos);
}

void StarPointerCommandStream::calcPose(const Vec2f& rPos, const Vec2f& rInsidePos) {
    mFollowPos = rPos;
    Vec2f diff;
    diff.x = rInsidePos.x - rPos.x;
    diff.y = rInsidePos.y - rPos.y;

    if (MR::isScreen16Per9()) {
    }

    if (!MR::isNearZero(diff)) {
        f32 length;
        MR::separateScalarAndDirection(&length, &diff, diff);
        f32 lineWidth = calcLineWidth(length);
        length /= ::hDefaultLength;
        lineWidth /= ::hDefaultWidth;
        MR::setPaneScale(this, lineWidth, length, "PicRibbon");
        MR::setPaneRotate(this, 0.0f, 0.0f, MR::toDegree(MR::atan2(diff.x, diff.y)), "Arrow");
        return;
    }

    MR::setPaneScale(this, 0.0f, 0.0f, "PicRibbon");
}

void StarPointerCommandStream::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Wait", 0);
    }

    if (!MR::isStarPointerInScreen(mPadChannel)) {
        setNerve(GET_NERVE(StarPointerCommandStream, HostTypeNrvSignal));
    }
}

void StarPointerCommandStream::exeSignal() {

    if (MR::isStarPointerInScreen(mPadChannel)) {
        setNerve(GET_NERVE(StarPointerCommandStream, HostTypeNrvWait));
    }
}

f32 StarPointerCommandStream::calcLineWidth(f32 length) const {
    f32 norm = MR::normalize(length, ::sMinLineWidthDistance, ::sMaxLineWidthDistance);
    return norm * ::sMinWidth + (1.0f - norm) * ::sMaxWidth;
}
