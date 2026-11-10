#include "Game/Screen/GalaxyMapMarioIcon.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/System/LumaCore.hpp"

GalaxyMapMarioIcon::GalaxyMapMarioIcon(LayoutActor* pParam1, const char* pParam2) : LayoutActor("マリオ", true), mTargetLayout(pParam1), mTargetPaneName(pParam2) {
    lumacore::setClass(this, lumacore::kClassId< GalaxyMapMarioIcon >);
}

void GalaxyMapMarioIcon::init(const TableIter& /*rIter*/) {
    f32 animFrame;

    initLayoutManager("IconMario", 2);
    MR::setInfluencedAlphaToChild(this);
    MR::startAnim(this, "Luigi", 1);

    if (MR::isPlayerLuigi()) {
        animFrame = 1.0f;
    } else {
        animFrame = 0.0f;
    }

    MR::setAnimFrameAndStop(this, animFrame, 1);
}

void GalaxyMapMarioIcon::showBlink() {
    appear();
    MR::startAnim(this, "Wait", 0);
}

void GalaxyMapMarioIcon::control() {
    MR::setLayoutScalePosAtPaneScaleTrans(this, mTargetLayout, mTargetPaneName);
    MR::setLayoutAlpha(this, MR::getPaneAlpha(mTargetLayout, mTargetPaneName));
}
