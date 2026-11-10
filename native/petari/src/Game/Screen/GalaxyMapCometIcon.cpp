#include "Game/Screen/GalaxyMapCometIcon.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/System/LumaCore.hpp"

GalaxyMapCometIcon::GalaxyMapCometIcon(const char* pName, LayoutActor* pParam2, const char* pParam3)
    : LayoutActor(pName, true), mGalaxyName(pName), mHost(pParam2), mPaneName(pParam3) {
    lumacore::setClass(this, lumacore::kClassId< GalaxyMapCometIcon >);
}

void GalaxyMapCometIcon::appear() {
    LayoutActor::appear();
    syncStatus();
}

void GalaxyMapCometIcon::calcAnim() {
    MR::setLayoutScalePosAtPaneScaleTransIfExecCalcAnim(this, mHost, mPaneName);
    LayoutActor::calcAnim();
}

void GalaxyMapCometIcon::calcAnimForCapture(const Ui::DrawInfo& rDrawInfo) {
    MR::setLayoutScalePosAtPaneScaleTransIfExecCalcAnim(this, mHost, mPaneName);
    MR::calcAnimLayoutWithDrawInfo(this, rDrawInfo);
}

void GalaxyMapCometIcon::control() {
}

GalaxyMapCometIcon::~GalaxyMapCometIcon() {
}

void GalaxyMapCometIcon::init(const TableIter& /*rIter*/) {
    initLayoutManager("IconComet", 2);
    MR::startAnim(this, "Wait", 0);
}

void GalaxyMapCometIcon::syncStatus() {
    int cometId = -1;

    if (MR::isGalaxyCometLandInStage(mGalaxyName)) {
        cometId = MR::getEncounterGalaxyCometNameId(mGalaxyName);
    }

    if (cometId < 0) {
        MR::hideLayout(this);
    } else {
        MR::showLayout(this);
        MR::startAnim(this, "Color", 1);
        MR::setCometAnimFromId(this, cometId, 1);
    }
}
