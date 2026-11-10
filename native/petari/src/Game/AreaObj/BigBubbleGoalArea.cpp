#include "Game/AreaObj/BigBubbleGoalArea.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Ride/BigBubble.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 150.0f);

namespace {
    NEBULA_TUNABLE(f32, sMeterUpSpeed, 0.05f);
};  // namespace

BigBubbleGoalArea::BigBubbleGoalArea(int formType, const char* pName)
    : AreaObj(formType, pName), mTrans(0.0f, 0.0f, 0.0f), mMeterCapacity(1.0f), mMeterDisplayAmount(), mMeterAmount() {
}

void BigBubbleGoalArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    mMeterCapacity = mObjArg0 <= 0 ? 1.0f : mObjArg0;

    MR::getPlacementTrans(rIter, &mTrans);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_BigBubbleGoal);
}

void BigBubbleGoalArea::addBubble(BigBubble* pBubble) {
    if (isValidSwitchA() && !isOnSwitchA()) {
        mMeterAmount += pBubble->mVolume;

        if (mMeterAmount >= mMeterCapacity) {
            mMeterAmount = mMeterCapacity;
        }
    }
}

void BigBubbleGoalArea::movement() {
    if (!isValidSwitchA() || isOnSwitchA()) {
        return;
    }

    if (!(mMeterDisplayAmount < mMeterAmount)) {
        return;
    }

    mMeterDisplayAmount += ::sMeterUpSpeed;

    if (mMeterDisplayAmount >= mMeterCapacity) {
        mMeterDisplayAmount = mMeterCapacity;
        onSwitchA();
        mIsValid = false;
        return;
    }

    if (mMeterDisplayAmount > mMeterAmount) {
        mMeterDisplayAmount = mMeterAmount;
    }
}

void BigBubbleGoalArea::draw() const {
    if (!isValidSwitchA() || isOnSwitchA()) {
        return;
    }

    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, true);
    material.mState.cull = NEBULA_CULL_BACK;

    f32 f = TWO_PI * (mMeterDisplayAmount / mMeterCapacity);

    TypedDraw site = TDDraw::site(material, 0);
    TDDraw::drawFillFan(site, mTrans, MR::getCamZdir(), MR::getCamYdir() * cNum_draw_f1, 0xFFFF00FF, 0.0f, f, 16);
    TDDraw::drawFillFan(site, mTrans, MR::getCamZdir(), MR::getCamYdir() * cNum_draw_f2, 0x80FF, f, TWO_PI, 16);
}

namespace MR {
    bool checkBigBubbleGoal(BigBubble* pBubble) {
        BigBubbleGoalArea* const pGoalArea = static_cast< BigBubbleGoalArea* >(MR::getAreaObj("BigBubbleGoalArea", pBubble->mPosition));

        if (pGoalArea == nullptr) {
            return false;
        }

        pGoalArea->addBubble(pBubble);
        return true;
    }
};  // namespace MR
