#include "Game/Screen/GalaxyMapDomeIcon.hpp"
#include "Game/Screen/GalaxyNamePlate.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataConst.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include "Game/System/LumaCore.hpp"

namespace {
    class AstroDomeCheckerBase {
    public:
        AstroDomeCheckerBase(int param1);

        void operator()(const TableIter& rIter);

        virtual void execute(const char* /*pGalaxyName*/) {
        }

        MR::FormattedName mGalaxyText;  // "Galaxy" and the dome's number, whole
        bool mIsPass;
    };

    class CheckerIsExistNewGalaxy : public AstroDomeCheckerBase {
    public:
        CheckerIsExistNewGalaxy(int param1) : AstroDomeCheckerBase(param1) {
            mIsPass = false;
        }

        virtual void execute(const char* pGalaxyName) {
            if (MR::canOpenGalaxy(pGalaxyName) && !MR::isOnGameEventFlagGalaxyOpen(pGalaxyName)) {
                mIsPass = true;
            }
        }
    };

    class CheckerIsComplete : public AstroDomeCheckerBase {
    public:
        CheckerIsComplete(int param1) : AstroDomeCheckerBase(param1) {
            mIsPass = true;
        }

        virtual void execute(const char* pGalaxyName) {
            GalaxyStatusAccessor accessor = MR::makeGalaxyStatusAccessor(pGalaxyName);

            if (accessor.getPowerStarNum()) {
                if (accessor.getPowerStarNumOwned() != accessor.getPowerStarNum()) {
                    mIsPass = false;
                }
            }
        }
    };
};  // namespace

GalaxyMapDomeIcon::GalaxyMapDomeIcon(int param1, LayoutActor* pHost, const char* pPaneName, const char* pParam4)
    : LayoutActor(pPaneName, true), mDomeNo(param1), mHost(pHost), mPaneName(pPaneName), mNamePaneName(pParam4), mNamePlate(), mMode() {
    lumacore::setClass(this, lumacore::kClassId< GalaxyMapDomeIcon >);
}

void GalaxyMapDomeIcon::appear() {
    LayoutActor::appear();
    mNamePlate->appear();
    syncStatus();
}

void GalaxyMapDomeIcon::kill() {
    LayoutActor::kill();
    mNamePlate->kill();
}

void GalaxyMapDomeIcon::movement() {
    LayoutActor::movement();
    mNamePlate->movement();
}

void GalaxyMapDomeIcon::calcAnim() {
    MR::setLayoutScalePosAtPaneScaleTrans(this, mHost, mPaneName);
    MR::setLayoutScalePosAtPaneScaleTrans(mNamePlate, mHost, mPaneName);
    LayoutActor::calcAnim();
    mNamePlate->calcAnim();
}

void GalaxyMapDomeIcon::calcAnimForCapture(const Ui::DrawInfo& rDrawInfo) {
    MR::setLayoutScalePosAtPaneScaleTrans(this, mHost, mPaneName);
    MR::calcAnimLayoutWithDrawInfo(this, rDrawInfo);
}

void GalaxyMapDomeIcon::setModeNormal() {
    mMode = 0;

    syncStatus();
}

void GalaxyMapDomeIcon::setModeNewDomeDiscover() {
    mMode = 1;

    syncStatus();
}

void GalaxyMapDomeIcon::draw() const {
    LayoutActor::draw();
    mNamePlate->draw();
}

s32 GalaxyMapDomeIcon::calcDomeStatus() const {
    if (isComplete()) {
        return 3;
    } else if (hasNewGalaxy()) {
        return 1;
    } else if (MR::hasGrandStar(mDomeNo)) {
        return 2;
    } else {
        return 0;
    }
}

bool GalaxyMapDomeIcon::isComplete() const {
    Table mapInfo = Table();

    mapInfo.attach(NebulaDataTables::getGalaxyInfoTable());

    CheckerIsComplete checker = CheckerIsComplete(mDomeNo);

    TableIter begin = mapInfo.begin();
    for (TableIter current = begin; current != mapInfo.end(); current.mIndex++) {
        checker(current);
    }

    return checker.mIsPass;
}

void GalaxyMapDomeIcon::init(const TableIter& /*rIter*/) {
    initLayoutManager("MapDomeIcon", 3);

    mNamePlate = new GalaxyNamePlate(nullptr, false);
    mNamePlate->initWithoutIter();
    mNamePlate->kill();

    MR::startAnim(this, "DomeColor", 0);
    MR::setAnimFrameAndStop(this, mDomeNo - 1, 0);
}

void GalaxyMapDomeIcon::control() {
    if (MR::isHiddenPane(mHost, mNamePaneName)) {
        MR::hideLayout(mNamePlate);
    } else {
        MR::showLayout(mNamePlate);
        mNamePlate->show(MR::getScenarioNameOnCurrentLanguage("AstroDome", mDomeNo), false);
    }
}

void GalaxyMapDomeIcon::syncStatus() {
    s32 domeStatus = calcDomeStatus();

    if (domeStatus == 0) {
        MR::hideLayout(this);
        MR::hidePane(mHost, mNamePaneName);
    } else {
        MR::showLayout(this);
        MR::showPane(mHost, mNamePaneName);
    }

    MR::startAnim(this, "Status", 0);
    MR::setAnimFrameAndStop(this, domeStatus, 0);

    bool b = mMode != 0 && mDomeNo == 2;

    if (b) {
        MR::startAnim(this, "SelectIn", 1);
        MR::setAnimFrameAndStopAtEnd(this, 1);
        MR::startAnim(this, "Blink", 0);
    } else {
        MR::startAnim(this, "Wait", 1);
    }
}

bool GalaxyMapDomeIcon::hasNewGalaxy() const {
    Table mapInfo = Table();

    mapInfo.attach(NebulaDataTables::getGalaxyInfoTable());

    CheckerIsExistNewGalaxy checker = CheckerIsExistNewGalaxy(mDomeNo);

    TableIter begin = mapInfo.begin();
    for (TableIter current = begin; current != mapInfo.end(); current.mIndex++) {
        checker(current);
    }

    return checker.mIsPass;
}

namespace {
    AstroDomeCheckerBase::AstroDomeCheckerBase(int param1) : mGalaxyText("Galaxy%1d", param1), mIsPass(false) {
    }
};  // namespace

namespace {
    void AstroDomeCheckerBase::operator()(const TableIter& rIter) {
        const char* pMapPaneName = nullptr;
        const char* pName = nullptr;

        rIter.getValue("MapPaneName", &pMapPaneName);
        rIter.getValue("name", &pName);

        if (pMapPaneName != nullptr && pName != nullptr && strstr(pMapPaneName, mGalaxyText.c_str()) != nullptr) {
            execute(pName);
        }
    }
};  // namespace
