#include "Game/Screen/SaveIcon.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/System/LumaCore.hpp"

SaveIcon::SaveIcon(const LayoutActor* pActor) : LayoutActor("SaveIcon", true), mActor(pActor) {
    lumacore::setClass(this, lumacore::kClassId< SaveIcon >);
    initLayoutManager("IconSave", 1);
}

void SaveIcon::appear() {
    LayoutActor::appear();
    MR::startAnim(this, "Rotate", 0);
}

void SaveIcon::calcAnim() {
    MR::setLayoutScalePosAtPaneScaleTransIfExecCalcAnim(this, mActor, "SaveIconPosition");
    LayoutActor::calcAnim();
}
