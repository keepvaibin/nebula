#include "Game/MapObj/ClipAreaDropLaser.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/ClipAreaDropHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_ClipAreaDropLaser_f1, "ClipAreaDropLaser.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 6.0f);

namespace NrvClipAreaDropLaser {
    NEW_NERVE(ClipAreaDropLaserNrvWait, ClipAreaDropLaser, Wait);
    NEW_NERVE(ClipAreaDropLaserNrvMove, ClipAreaDropLaser, Move);
};  // namespace NrvClipAreaDropLaser

ClipAreaDropLaser::ClipAreaDropLaser(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ClipAreaDropLaser >);
    mNumPointsToDraw = 0.0f;
    mPointIndexToSkipDraw = -1;
    mDrawCount = -1;
    mSpeed = cNum_ClipAreaDropLaser_f1;
    MR::createClipAreaDropHolder();
}

void ClipAreaDropLaser::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipAreaDropLaser);
    initRailRider(rIter);
    MR::moveCoordAndTransToRailStartPoint(this);
    mSpeed = cNum_init_f1;
    MR::getPlacementArg0NoInit(rIter, &mSpeed);
    initEffectKeeper(0, "ClipAreaDropLaser");
    initSound(4, false);
    initNerve(GET_NERVE(ClipAreaDropLaser, ClipAreaDropLaserNrvMove));
    MR::invalidateClipping(this);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        setNerve(GET_NERVE(ClipAreaDropLaser, ClipAreaDropLaserNrvWait));
    }
    makeActorAppeared();
}
void ClipAreaDropLaser::control() {
}

void ClipAreaDropLaser::draw() const {
    TDDraw::cameraInit3D();
    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 2, 0);
    material.mState.line_width = cNum_draw_f1 / cNum_draw_f2;
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);

    // Back from the newest point through the ring.
    for (int i = 1; i < mNumPointsToDraw; i++) {
        int from = mDrawCount - (i - 1);
        int to = mDrawCount - i;

        if (from < 0)
            from += cPointNum;
        if (to < 0)
            to += cPointNum;

        if (from != mPointIndexToSkipDraw) {
            TypedDraw site = TDDraw::site(material, 0);
            TDDraw::drawLine(site, mPoints[from], mPoints[to], 0x40F080);
        }
    }
}

void ClipAreaDropLaser::exeWait() {
    if (mNumPointsToDraw > 0.0f) {
        mNumPointsToDraw = mNumPointsToDraw - 1.0f;
    }

    if (MR::isValidSwitchAppear(this) && MR::isOnSwitchAppear(this))
        setNerve(GET_NERVE(ClipAreaDropLaser, ClipAreaDropLaserNrvMove));
}

void ClipAreaDropLaser::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::moveCoordAndTransToRailStartPoint(this);
        mNumPointsToDraw = 0.0f;
    }
    s32 railPoint = 0;
    railPoint = MR::moveCoordAndCheckPassPointNo(this, mSpeed);
    MR::moveTransToCurrentRailPos(this);
    incrementDrawCount();
    if (MR::isRailReachedGoal(this)) {
        mPointIndexToSkipDraw = mDrawCount;
        railPoint = MR::getRailPointNum(this) - 1;
        MR::moveCoordAndTransToRailStartPoint(this);
    }
    mPoints[mDrawCount].set(mPosition);
    if (railPoint != -1) {
        Vec3f pos;
        MR::calcRailPointPos(&pos, this, railPoint);
        f32 arg = -1.0;
        MR::getRailPointArg0NoInit(this, railPoint, &arg);
        if (arg > 0.0f) {
            MR::emitEffectHit(this, pos, "Splash");
            MR::startSound(this, "SE_OJ_DROP_LASER_SPLASH");
            MR::appearClipAreaDrop(pos, arg);
        }
    }

    if (MR::isValidSwitchAppear(this) && !MR::isOnSwitchAppear(this))
        setNerve(GET_NERVE(ClipAreaDropLaser, ClipAreaDropLaserNrvWait));
}

void ClipAreaDropLaser::incrementDrawCount() {
    mDrawCount += 1;
    if (mDrawCount >= cPointNum) {
        mDrawCount += -cPointNum;
    }
    if (mNumPointsToDraw < static_cast< f32 >(cPointNum)) {
        mNumPointsToDraw += 1.0f;
    }
}
