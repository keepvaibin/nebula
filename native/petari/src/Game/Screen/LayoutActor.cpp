#include "Game/Screen/LayoutActor.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/PaneEffectKeeper.hpp"
#include "Game/Screen/StarPointerTarget.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/System/LumaCore.hpp"

LayoutActor::LayoutActor(const char* pName, bool) : NameObj(pName), mLayoutManager(), mSpine(), mEffectKeeper(), mPointingTarget() {
    lumacore::setClass(this, lumacore::kClassId< LayoutActor >);
}

void LayoutActor::movement() {
    if (MR::isDead(this)) {
        return;
    }

    updateSpine();

    if (MR::isDead(this)) {
        return;
    }

    control();

    if (mLayoutManager != nullptr && !MR::isStopAnimFrame(this)) {
        mLayoutManager->movement();
    }
}

void LayoutActor::calcAnim() {
    if (MR::isExecuteCalcAnimLayout(this)) {
        mLayoutManager->calcAnim();
    }
}

void LayoutActor::draw() const {
    if (mFlag.mHudPart != 0 && NebulaMods::isHudHidden(mFlag.mHudPart)) {
        return;
    }
    if (MR::isExecuteDrawLayout(this)) {
        MotionSignal::drawObject(mDrawSlot, mMotionEpoch);
        mLayoutManager->setDrawGeneration(mMotionEpoch);
        mLayoutManager->draw();
    }
}

void LayoutActor::appear() {
    // Back from dead: a new life, so its panes do not blend from where they were before it was killed.
    if (mFlag.mIsDead) {
        ++mMotionEpoch;
        NebulaMods::fire(NEBULA_MOD_EVENT_SCREEN, 1, nullptr, mName);
    }

    mFlag.mIsDead = false;

    calcAnim();
}

void LayoutActor::kill() {
    if (!mFlag.mIsDead) {
        NebulaMods::fire(NEBULA_MOD_EVENT_SCREEN, 0, nullptr, mName);
    }

    if (mEffectKeeper != nullptr) {
        mEffectKeeper->clear();
    }

    mFlag.mIsDead = true;
}

void LayoutActor::setNerve(const Nerve* pNerve) const {
    mSpine->setNerve(pNerve);
}

bool LayoutActor::isNerve(const Nerve* pNerve) const {
    return mSpine->getCurrentNerve() == pNerve;
}

s32 LayoutActor::getNerveStep() const {
    return mSpine->mStep;
}

Vec2f LayoutActor::getTrans() const {
    Vec2f trans;

    MR::copyPaneTrans(&trans, this, nullptr);
    MR::convertLayoutPosToScreenPos(&trans, trans);

    return trans;
}

namespace {
    inline void setRootTranslation(LayoutActor* pActor, const Vec2f& rTrans) {
        f32 y = rTrans.y;
        f32 x = rTrans.x;
        Vec2f trans(x, y);
        Ui::Pane* pRootPane = MR::getRootPane(pActor);
        Vec3f translation(trans.x, trans.y, 0.0f);
        pRootPane->mTranslate.x = translation.x;
        pRootPane->mTranslate.y = translation.y;
        pRootPane->mTranslate.z = translation.z;
    }
};  // namespace

void LayoutActor::setTrans(const Vec2f& rTrans) {
    Vec2f trans;
    MR::convertScreenPosToLayoutPos(&trans, rTrans);
    ::setRootTranslation(this, trans);
}

LayoutManager* LayoutActor::getLayoutManager() const {
    return mLayoutManager;
}

void LayoutActor::createPaneMtxRef(const char* pParam1) {
    mLayoutManager->createPaneMtxRef(pParam1);
}

MtxPtr LayoutActor::getPaneMtxRef(const char* pParam1) {
    return mLayoutManager->getPaneMtxRef(pParam1);
}

void LayoutActor::initLayoutManager(const char* pName, u32 rootPaneAnimLayerNum) {
    mLayoutManager = new LayoutManager(pName, true, rootPaneAnimLayerNum, 0x100);
}

void LayoutActor::initLayoutManagerNoConvertFilename(const char* pName, u32 rootPaneAnimLayerNum) {
    mLayoutManager = new LayoutManager(pName, false, rootPaneAnimLayerNum, 0x100);
}

void LayoutActor::initLayoutManagerWithTextBoxBufferLength(const char* pName, u32 textBoxBufferLength, u32 rootPaneAnimLayerNum) {
    mLayoutManager = new LayoutManager(pName, false, rootPaneAnimLayerNum, textBoxBufferLength);
}

void LayoutActor::initNerve(const Nerve* pNerve) {
    mSpine = new Spine(this, pNerve);
}

void LayoutActor::initEffectKeeper(int param1, const char* pParam2, const EffectSystem* pEffectSystem) {
    mEffectKeeper = new PaneEffectKeeper(this, mLayoutManager, param1, pParam2);
    mEffectKeeper->init(this, pEffectSystem);
}

void LayoutActor::initPointingTarget(int maxNumTargets) {
    mPointingTarget = new StarPointerLayoutTargetKeeper(maxNumTargets);
}

void LayoutActor::updateSpine() {
    if (mSpine != nullptr) {
        mSpine->update();
    }
}
