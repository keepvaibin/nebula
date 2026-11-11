#include "Game/Screen/WipeRing.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeWipeIn_f1, "exeWipeIn.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWipeOut_f1, "exeWipeOut.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setCenterPos_f1, "setCenterPos.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setCenterPos_f2, "setCenterPos.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_canCenterOnMario_f1, "canCenterOnMario.f1", 15060.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_canCenterOnMario_f2, "canCenterOnMario.f2", 11800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_canCenterOnMario_f3, "canCenterOnMario.f3", 1260.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_canCenterOnMario_f4, "canCenterOnMario.f4", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRadius_f1, "calcRadius.f1", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcMaxRadius_f1, "calcMaxRadius.f1", 900160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePlayerPos_f1, "updatePlayerPos.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePlayerPos_f2, "updatePlayerPos.f2", 0.5f);

namespace {
    NEBULA_TUNABLE(s32, sAnimFrame, 60);
    static const char* sOutAnimName = "Out";
    static const char* sInAnimName = "In";

    f32 ringCanvasScale() {
        const auto& display = MR::getDisplay();
        if (!MR::isScreen16Per9() || !display.wide) return 1.0f;
        return __builtin_fmaxf(1.0f, __builtin_fmaxf(display.layout_width / NebulaFrameClock::cLayoutWidth,
                                                   display.vertical_fit));
    }
};  // namespace

namespace {
    NEW_NERVE(WipeRingNrvClose, WipeRing, Close);
    NEW_NERVE(WipeRingNrvWipeIn, WipeRing, WipeIn);
    NEW_NERVE(WipeRingNrvOpen, WipeRing, Open);
    NEW_NERVE(WipeRingNrvWipeOut, WipeRing, WipeOut);
};  // namespace

WipeRing::WipeRing(bool isCenterOnPlayer, const char* pName)
    : WipeLayoutBase(pName), mCenterPos(0.0f, 0.0f), mAnimFrame(::sAnimFrame), mWipeOutFrame(), mWipeInFrame(), mIsCenterOnPlayer(isCenterOnPlayer) {
    lumacore::setClass(this, lumacore::kClassId< WipeRing >);
}

void WipeRing::init(const TableIter& /*rIter*/) {
    initLayoutManager("WipeRing", 1);
    MR::createAndAddPaneCtrl(this, "Ring", 1);
    MR::setFollowPos(&mCenterPos, this, "Ring");
    initNerve(GET_NERVE_ANON(WipeRingNrvOpen));
    kill();
}

void WipeRing::exeClose() {
    updatePlayerPos();
}

void WipeRing::calcAnim() {
    // The mask scales about its followed centre (moving that centre would detach it from Mario): its own screen scale,
    // which its children do not inherit.
    if (Ui::Pane* pRing = mLayoutManager->findPaneByName("Ring")) {
        pRing->mScreenScale = ringCanvasScale();
    }
    LayoutActor::calcAnim();
}

void WipeRing::exeWipeIn() {
    updatePlayerPos();

    if (MR::isFirstStep(this)) {
        MR::getAnimCtrl(this, 0)->setRate(0.0f);
    }
    const f32 radiusRate = __builtin_fminf(calcRadius() / calcMaxRadius(), 1.0f);
    mWipeInFrame = radiusRate * MR::getAnimCtrl(this, 0)->getEnd();

    const f32 angle = (1.0f + static_cast< f32 >(getNerveStep()) / mAnimFrame) * FloatUtil::PI();
    f32 f = MR::sin(cNum_exeWipeIn_f1 * angle);

    MR::getAnimCtrl(this, 0)->setFrame(mWipeInFrame * (1.0f - f));

    if (MR::isStep(this, mAnimFrame)) {
        setNerve(GET_NERVE_ANON(WipeRingNrvOpen));
    }
}

void WipeRing::exeOpen() {
    updatePlayerPos();

    if (MR::isFirstStep(this)) {
        kill();
    }
}

void WipeRing::exeWipeOut() {
    updatePlayerPos();

    if (MR::isFirstStep(this)) {
        MR::getAnimCtrl(this, 0)->setRate(0.0f);
    }
    const f32 invRadiusRate = 1.0f - __builtin_fminf(calcRadius() / calcMaxRadius(), 1.0f);
    mWipeOutFrame = invRadiusRate * MR::getAnimCtrl(this, 0)->getEnd();

    const f32 angle = (static_cast< f32 >(getNerveStep()) / mAnimFrame) * FloatUtil::PI();
    f32 f = MR::sin(cNum_exeWipeOut_f1 * angle);

    MR::getAnimCtrl(this, 0)->setFrame(mWipeOutFrame * (1.0f - f) + f * MR::getAnimCtrl(this, 0)->getEnd());

    if (MR::isStep(this, mAnimFrame)) {
        setNerve(GET_NERVE_ANON(WipeRingNrvClose));
    }
}

void WipeRing::wipe(s32 frame) {
    if (frame < 1) {
        mAnimFrame = 90;
    } else {
        mAnimFrame = frame;
    }

    if (isNerve(GET_NERVE_ANON(WipeRingNrvClose))) {
        setNerve(GET_NERVE_ANON(WipeRingNrvWipeIn));
        startAnim(::sInAnimName);
    } else if (isNerve(GET_NERVE_ANON(WipeRingNrvWipeIn))) {
        setNerve(GET_NERVE_ANON(WipeRingNrvWipeOut));
        startAnim(::sOutAnimName);
    } else if (isNerve(GET_NERVE_ANON(WipeRingNrvOpen))) {
        if (MR::isDead(this)) {
            appear();
        }

        setNerve(GET_NERVE_ANON(WipeRingNrvWipeOut));
        startAnim(::sOutAnimName);
    } else if (isNerve(GET_NERVE_ANON(WipeRingNrvWipeOut))) {
        setNerve(GET_NERVE_ANON(WipeRingNrvWipeIn));
        startAnim(::sInAnimName);
    }
}

void WipeRing::forceClose() {
    if (MR::isDead(this)) {
        appear();
    }

    MR::startAnim(this, ::sInAnimName, 0);
    MR::setAnimFrameAndStop(this, 0.0f, 0);
    setNerve(GET_NERVE_ANON(WipeRingNrvClose));
}

void WipeRing::forceOpen() {
    kill();
    setNerve(GET_NERVE_ANON(WipeRingNrvOpen));
}

bool WipeRing::isOpen() const {
    return isNerve(GET_NERVE_ANON(WipeRingNrvOpen));
}

bool WipeRing::isClose() const {
    return isNerve(GET_NERVE_ANON(WipeRingNrvClose));
}

bool WipeRing::isWipeIn() const {
    return isNerve(GET_NERVE_ANON(WipeRingNrvWipeIn));
}

bool WipeRing::isWipeOut() const {
    return isNerve(GET_NERVE_ANON(WipeRingNrvWipeOut));
}

void WipeRing::setCenterPos(const Vec3f& rPos) {
    if (MR::calcScreenPosition(&mCenterPos, rPos)) {
        return;
    }

    mCenterPos.set(MR::getScreenWidth() * cNum_setCenterPos_f1, MR::getScreenHeight() * cNum_setCenterPos_f2);
}

void WipeRing::startAnim(const char* pAnimName) {
    MR::startAnim(this, pAnimName, 0);
    MR::getAnimCtrl(this, 0)->setRate(static_cast< f32 >(MR::getAnimCtrl(this, 0)->getEnd()) / mAnimFrame);
}

namespace {
    bool canCenterOnMario() {
        if (MR::isEqualStageName("IceVolcanoGalaxy") && MR::getCurrentScenarioNo() == 1) {
            Vec3f position(cNum_canCenterOnMario_f1, -cNum_canCenterOnMario_f2, cNum_canCenterOnMario_f3);
            if (MR::getPlayerPos()->distance(position) < cNum_canCenterOnMario_f4) {
                return false;
            }
        }

        return !MR::isStageSuddenDeathDodoryu();
    }
};  // namespace

bool WipeRing::getMarioCenterPos(Vec3f* pCenterPos) {
    if (MR::isExistMario() && ::canCenterOnMario()) {
        pCenterPos->set(static_cast< const Vec& >(*MR::getPlayerCenterPos()));

        return true;
    }

    pCenterPos->zero();
    return false;
}

f32 WipeRing::calcRadius() const {
    // Measure in the ring's circular coordinate system, including the taller layout canvas.
    // Screen positions themselves always use the game's logical 456-line height.
    const f32 width = MR::getScreenWidth(), height = MR::getScreenHeight();
    const auto& display = MR::getDisplay();
    const bool extended = MR::isScreen16Per9() && display.wide;
    const f32 canvasWidth = extended ? display.layout_width : NebulaFrameClock::cLayoutWidth;
    const f32 vertical = extended ? display.vertical_fit : 1.0f;
    const f32 locationX = MR::isScreen16Per9() ? cNum_calcRadius_f1 : 1.0f;
    const f32 x = __builtin_fmaxf(__builtin_fabsf(mCenterPos.x), __builtin_fabsf(width - mCenterPos.x)) *
                  canvasWidth / (width * locationX);
    const f32 y = __builtin_fmaxf(__builtin_fabsf(mCenterPos.y), __builtin_fabsf(height - mCenterPos.y)) * vertical;
    return NebulaSqrt(x * x + y * y) / ringCanvasScale();
}

f32 WipeRing::calcMaxRadius() const {
    return NebulaSqrt(cNum_calcMaxRadius_f1);
}

void WipeRing::updatePlayerPos() {
    if (!mIsCenterOnPlayer) {
        return;
    }

    Vec3f marioCenterPos;

    if (!getMarioCenterPos(&marioCenterPos) && !MR::isFirstStep(this)) {
        return;
    }

    if (MR::calcScreenPosition(&mCenterPos, marioCenterPos)) {
        return;
    }

    mCenterPos.set(MR::getScreenWidth() * cNum_updatePlayerPos_f1, MR::getScreenHeight() * cNum_updatePlayerPos_f2);
}
