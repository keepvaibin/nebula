#include "Game/Screen/GalaxyMap.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/GalaxyMapCometIcon.hpp"
#include "Game/Screen/GalaxyMapDomeIcon.hpp"
#include "Game/Screen/GalaxyMapGalaxyDetail.hpp"
#include "Game/Screen/GalaxyMapGalaxyPlain.hpp"
#include "Game/Screen/GalaxyMapIcon.hpp"
#include "Game/Screen/GalaxyMapMarioIcon.hpp"
#include "Game/Screen/GalaxyMapTicoIcon.hpp"
#include "Game/Screen/GalaxyMapTitle.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/NebulaMods.hpp"
#include <nebula_format.h>
#include <Game/Ui/Pane.hpp>
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataConst.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <algorithm>
#include <cstdio>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(s32, sFocusKeepFrame, 30);
    NEBULA_TUNABLE(f32, sPointingRange, 20.0f);
};  // namespace

namespace {
    NEW_NERVE(GalaxyMapIdle, GalaxyMap, Idle);
    NEW_NERVE(GalaxyMapFocusIn, GalaxyMap, FocusIn);
    NEW_NERVE(GalaxyMapFocusOut, GalaxyMap, FocusOut);
    NEW_NERVE(GalaxyMapFadeinGalaxyMap, GalaxyMap, FadeinGalaxyMap);
    NEW_NERVE(GalaxyMapFadeinAstroMap, GalaxyMap, FadeinAstroMap);
    NEW_NERVE(GalaxyMapShowDetail, GalaxyMap, ShowDetail);
};  // namespace

GalaxyMap::GalaxyMap()
    : LayoutActor("ギャラクシー・天文台マップ", true), mMarioIcon1(), mMarioIcon2(), mGalaxyPlain(), mGalaxyDetail(), mPointingIcon(), mGalaxyName(),
      mIsPointingNew(), mIsFocusEnabled(true) {
    lumacore::setClass(this, lumacore::kClassId< GalaxyMap >);
}

template <>
inline MR::Vector< MR::AssignableArray< GalaxyMapIcon* > >::~Vector() {
}

template <>
inline MR::Vector< MR::AssignableArray< GalaxyMapCometIcon* > >::~Vector() {
}

template <>
inline MR::Vector< MR::AssignableArray< GalaxyMapDomeIcon* > >::~Vector() {
}

template <>
inline MR::Vector< MR::AssignableArray< GalaxyMapTicoIcon* > >::~Vector() {
}

void GalaxyMap::init(const TableIter& /*rIter*/) {
    initLayoutManager("MapGrandGalaxy", 1);
    initPaneCtrlPointing();
    initDomeIcon();
    initMarioIcon();
    initTicoIcon();
    initNerve(GET_NERVE_ANON(GalaxyMapIdle));

    mGalaxyPlain = new GalaxyMapGalaxyPlain(this);
    mGalaxyPlain->initWithoutIter();

    mGalaxyDetail = new GalaxyMapGalaxyDetail();
    mGalaxyDetail->initWithoutIter();

    mTitle = new GalaxyMapTitle();
    mTitle->initWithoutIter();


    kill();
}

void GalaxyMap::appear() {
    LayoutActor::appear();

    for (auto& item : mDomeIcon) {
        item->appear();
    }
    for (auto& item : mIcon) {
        item->appear();
    }
    for (auto& item : mCometIcon) {
        item->appear();
    }
    for (auto& item : mTicoIcon) {
        item->appear();
    }

    mMarioIcon1->showBlink();

    if (mMarioIcon2 != nullptr) {
        mMarioIcon2->showBlink();
    }

    MR::setTextBoxNumberRecursive(this, "Star", MR::getPowerStarNum());
}

void GalaxyMap::kill() {
    LayoutActor::kill();

    for (auto& item : mDomeIcon) {
        item->kill();
    }
    for (auto& item : mIcon) {
        item->kill();
    }
    for (auto& item : mCometIcon) {
        item->kill();
    }
    for (auto& item : mTicoIcon) {
        item->kill();
    }

    mGalaxyPlain->kill();
    mGalaxyDetail->kill();
    mMarioIcon1->kill();
    mTitle->kill();

    if (mMarioIcon2 != nullptr) {
        mMarioIcon2->kill();
    }
}

void GalaxyMap::movement() {
    LayoutActor::movement();

    if (!isNerve(GET_NERVE_ANON(GalaxyMapShowDetail))) {
        for (auto& item : mDomeIcon) {
            item->movement();
        }
        for (auto& item : mIcon) {
            item->movement();
        }
        for (auto& item : mCometIcon) {
            item->movement();
        }
        for (auto& item : mTicoIcon) {
            item->movement();
        }

        mMarioIcon1->movement();

        if (mMarioIcon2 != nullptr) {
            mMarioIcon2->movement();
        }
    }

    mGalaxyDetail->movement();
    mGalaxyPlain->movement();
    mTitle->movement();
}

void GalaxyMap::calcAnim() {
    LayoutActor::calcAnim();

    for (auto& item : mDomeIcon) {
        item->calcAnim();
    }
    for (auto& item : mIcon) {
        item->calcAnim();
    }
    for (auto& item : mCometIcon) {
        item->calcAnim();
    }
    for (auto& item : mTicoIcon) {
        item->calcAnim();
    }

    mMarioIcon1->calcAnim();
    mGalaxyDetail->calcAnim();
    mGalaxyPlain->calcAnim();
    mTitle->calcAnim();

    if (mMarioIcon2 != nullptr) {
        mMarioIcon2->calcAnim();
    }
}

void GalaxyMap::draw() const {
    if (!MR::isExecuteDrawLayout(this)) {
        return;
    }

    LayoutActor::draw();

    for (auto& item : mDomeIcon) {
        item->draw();
    }
    mMarioIcon1->draw();
    drawGalaxyIconWithOrder(1);
    mTitle->draw();
    for (auto& item : mTicoIcon) {
        item->draw();
    }

    if (mMarioIcon2 != nullptr) {
        mMarioIcon2->draw();
    }

    drawGalaxyIconWithOrder(3);
    for (auto& item : mCometIcon) {
        item->draw();
    }
    drawGalaxyIconWithOrder(2);

}

void GalaxyMap::movementForCapture() {
    if (mLayoutManager != nullptr && !MR::isStopAnimFrame(this)) {
        getLayoutManager()->movement();
    }

    if (isNerve(GET_NERVE_ANON(GalaxyMapShowDetail))) {
        return;
    }

    for (auto& item : mIcon) {
        item->movement();
    }
    for (auto& item : mCometIcon) {
        item->movement();
    }
}

void GalaxyMap::calcAnimForCapture(const Ui::DrawInfo& rDrawInfo) {
    MR::calcAnimLayoutWithDrawInfo(this, rDrawInfo);
    for (auto& item : mDomeIcon) {
        item->calcAnimForCapture(rDrawInfo);
    }
    for (auto& item : mIcon) {
        item->calcAnimForCapture(rDrawInfo);
    }
    for (auto& item : mCometIcon) {
        item->calcAnimForCapture(rDrawInfo);
    }
}

void GalaxyMap::drawForCapture(const Ui::DrawInfo& rDrawInfo) {
    MR::drawLayoutWithDrawInfoWithoutProjectionSetup(this, rDrawInfo);
    for (auto& item : mDomeIcon) {
        MR::drawLayoutWithDrawInfoWithoutProjectionSetup(item, rDrawInfo);
    }
    for (auto& item : mIcon) {
        MR::drawLayoutWithDrawInfoWithoutProjectionSetup(item, rDrawInfo);
    }
    for (auto& item : mCometIcon) {
        MR::drawLayoutWithDrawInfoWithoutProjectionSetup(item, rDrawInfo);
    }
}

void GalaxyMap::setModeNormal() {
    for (auto& item : mIcon) {
        item->setModeNormal();
    }
    for (auto& item : mTicoIcon) {
        item->setModeNormal();
    }
    for (auto& item : mDomeIcon) {
        item->setModeNormal();
    }
    mTitle->setModeNormal();

    mIsFocusEnabled = true;
}

void GalaxyMap::setModeNewGalaxyDiscover() {
    for (auto& item : mIcon) {
        item->setModeNewGalaxyDiscover();
    }
    for (auto& item : mTicoIcon) {
        item->setModeNormal();
    }
    for (auto& item : mDomeIcon) {
        item->setModeNormal();
    }
    mTitle->setModeHideInformation();

    mIsFocusEnabled = false;
}

void GalaxyMap::setModeNewTicoGalaxyDiscover() {
    for (auto& item : mIcon) {
        item->setModeNormal();
    }
    for (auto& item : mTicoIcon) {
        item->setModeDiscover();
    }
    for (auto& item : mDomeIcon) {
        item->setModeNormal();
    }
    mTitle->setModeHideInformation();

    mIsFocusEnabled = false;
}

void GalaxyMap::setModeNewDomeDiscover() {
    for (auto& item : mIcon) {
        item->setModeNormal();
    }
    for (auto& item : mTicoIcon) {
        item->setModeNormal();
    }
    for (auto& item : mDomeIcon) {
        item->setModeNewDomeDiscover();
    }
    mTitle->setModeHideInformation();

    mIsFocusEnabled = false;
}

void GalaxyMap::setModeTicoCometPray() {
    for (auto& item : mIcon) {
        item->setModeNormal();
    }
    for (auto& item : mTicoIcon) {
        item->setModeNormal();
    }
    for (auto& item : mDomeIcon) {
        item->setModeNormal();
    }
    mTitle->setModeHideInformation();

    mIsFocusEnabled = false;
}

void GalaxyMap::setModeCapture() {
    appear();

    forceToGalaxyMap();

    mTitle->kill();
    mMarioIcon1->kill();

    if (mMarioIcon2 != nullptr) {
        mMarioIcon2->kill();
    }
}

void GalaxyMap::drawGalaxyInfo() const {
    if (!MR::isExecuteDrawLayout(this)) {
        return;
    }

    mGalaxyPlain->draw();
    mGalaxyDetail->draw();
}

bool GalaxyMap::isTransition() const {
    return isNerve(GET_NERVE_ANON(GalaxyMapFadeinAstroMap)) || isNerve(GET_NERVE_ANON(GalaxyMapFadeinGalaxyMap));
}

bool GalaxyMap::isPointingAnything() const {
    return mPointingIcon != nullptr;
}

bool GalaxyMap::isShowDetail() const {
    return !MR::isDead(this) && isNerve(GET_NERVE_ANON(GalaxyMapShowDetail));
}


void GalaxyMap::changeToGalaxyMap() {
    appear();
    setNerve(GET_NERVE_ANON(GalaxyMapFadeinGalaxyMap));
}

void GalaxyMap::changeToAstroMap() {
    appear();
    setNerve(GET_NERVE_ANON(GalaxyMapFadeinAstroMap));
}

void GalaxyMap::forceToGalaxyMap() {
    appear();
    MR::startAnim(this, "DomeIn", 0);
    MR::setAnimFrameAndStopAtEnd(this, 0);
    mTitle->startGalaxyMap();
    setNerve(GET_NERVE_ANON(GalaxyMapIdle));
}

void GalaxyMap::forceToAstroMap() {
    appear();
    MR::startAnim(this, "DomeOut", 0);
    MR::setAnimFrameAndStopAtEnd(this, 0);
    mTitle->startAstroMap();
    setNerve(GET_NERVE_ANON(GalaxyMapIdle));
}

void GalaxyMap::exeIdle() {
    tryFocusIn();
}

void GalaxyMap::exeFocusIn() {
    mPointingIcon->onPointing();
    updateGalaxyName();

    if (tryFocusIn()) {
        return;
    }
}

void GalaxyMap::exeFocusOut() {
    updateGalaxyName();

    if (tryFocusIn()) {
        return;
    }

    if (MR::isGreaterStep(this, ::sFocusKeepFrame)) {
        mGalaxyName = nullptr;

        setNerve(GET_NERVE_ANON(GalaxyMapIdle));
    }
}

void GalaxyMap::exeFadeinGalaxyMap() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "DomeIn", 0);
        mTitle->changeToGalaxyMap();
    }

    if (MR::isAnimStopped(this, 0)) {
        mGalaxyName = nullptr;
        mPointingIcon = nullptr;

        setNerve(GET_NERVE_ANON(GalaxyMapIdle));
    }
}

void GalaxyMap::exeFadeinAstroMap() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "DomeOut", 0);
        mTitle->changeToAstroMap();
    }

    if (MR::isAnimStopped(this, 0)) {
        mGalaxyName = nullptr;
        mPointingIcon = nullptr;

        setNerve(GET_NERVE_ANON(GalaxyMapIdle));
    }
}

void GalaxyMap::exeShowDetail() {
    if (MR::isFirstStep(this)) {
        mGalaxyDetail->startDisplay(mGalaxyName, mIsPointingNew);
        for (auto& item : mIcon) {
            item->deactivatePointing();
        }
    }

    if (MR::isDead(mGalaxyDetail)) {
        for (auto& item : mIcon) {
            item->activatePointing();
        }
        setNerve(GET_NERVE_ANON(GalaxyMapIdle));
    }
}

namespace {
    template < class T >
    void initIconArray(MR::Vector< MR::AssignableArray< T* > >& rIcons, T** pBuffer, s32 capacity) {
        rIcons.mArray.mArr = pBuffer;
        rIcons.mArray.mMaxSize = capacity;
    }
};  // namespace

void GalaxyMap::initPaneCtrlPointing() {
    Table galaxyInfo;
    galaxyInfo.attach(NebulaDataTables::getGalaxyInfoTable());
    ::initIconArray(mIcon, new GalaxyMapIcon*[galaxyInfo.getNumEntries()], galaxyInfo.getNumEntries());
    ::initIconArray(mCometIcon, new GalaxyMapCometIcon*[galaxyInfo.getNumEntries()], galaxyInfo.getNumEntries());
    initPointingTarget(galaxyInfo.getNumEntries());

    // A mod's galaxy names a pane its map layout may not have. Each that says where it goes gets it made before the
    // controls below look the panes up.
    for (s32 i = 0; i < galaxyInfo.getNumEntries(); i++) {
        const char* pPaneName = nullptr;
        if (!galaxyInfo.getValue(i, "MapPaneName", &pPaneName) || MR::isEqualString(pPaneName, "") ||
            MR::isEqualString(pPaneName, "dummy")) {
            continue;
        }
        // The tree itself: a pane made for an earlier galaxy of this loop is not listed yet.
        if (getLayoutManager()->getPane(pPaneName) != nullptr) {
            continue;
        }
        const char* pGalaxyName = nullptr;
        galaxyInfo.getValue(i, "name", &pGalaxyName);
        f32 x = 0.0f;
        f32 y = 0.0f;
        s32 dome = 0;
        if (!NebulaMods::mapSpot(pGalaxyName, &x, &y, &dome)) {
            continue;
        }
        MR::FormattedName parentName;
        if (dome > 0) {
            parentName.format("Galaxy%d", dome);
        } else {
            parentName.format("Tico");
        }
        Ui::Pane* pParent = getLayoutManager()->findPaneByName(parentName.c_str());
        if (pParent == nullptr) {
            continue;
        }
        Ui::Pane* pPane = pParent->GetLayout()->NewPane< Ui::Pane >();
        pPane->SetName(pPaneName);
        pPane->SetSRTElement(0, x);
        pPane->SetSRTElement(1, y);
        pParent->AppendChild(pPane);
    }

    for (s32 i = 0; i < galaxyInfo.getNumEntries(); i++) {
        const char* pPaneName = nullptr;

        if (!galaxyInfo.getValue(i, "MapPaneName", &pPaneName)) {
            continue;
        }

        if (MR::isEqualString(pPaneName, "") || MR::isEqualString(pPaneName, "dummy")) {
            continue;
        }

        if (getLayoutManager()->findPaneByName(pPaneName) == nullptr) {
            continue;
        }

        const char* pGalaxyName = nullptr;
        galaxyInfo.getValue(i, "name", &pGalaxyName);

        GalaxyStatusAccessor accessor = MR::makeGalaxyStatusAccessor(pGalaxyName);
        MR::createAndAddPaneCtrl(this, pPaneName, 1);
        MR::addStarPointerTargetCircle(this, pPaneName, ::sPointingRange, Vec2f(0.0f, 0.0f), nullptr);

        GalaxyMapIcon* pIcon = new GalaxyMapIcon(pGalaxyName, this, pPaneName);
        pIcon->initWithoutIter();
        mIcon.mArray[mIcon.mCount++] = pIcon;

        if (accessor.isExistAnyComet()) {
            GalaxyMapCometIcon* pCometIcon = new GalaxyMapCometIcon(pGalaxyName, this, pPaneName);
            pCometIcon->initWithoutIter();
            mCometIcon.mArray[mCometIcon.mCount++] = pCometIcon;
        }
    }
}

namespace {
    const char* const cDomes[] = {
        "Dome1", "Dome2", "Dome3", "Dome4", "Dome5", "Dome6",
    };
    const char* const cDomeShowHidePanes[] = {
        "Base1", "Base2", "Base3", "Base4", "Base5", "Base6",
    };
    const char* const cMarioIconPositions[] = {
        "Mario7", "Mario1", "Mario2", "Mario3", "Mario4", "Mario5", "Mario6",
    };
    const char* const cMarioIconPositionsInDome[] = {
        nullptr, "GMario1", "GMario2", "GMario3", "GMario4", "GMario5", "GMario6",
    };
};  // namespace

void GalaxyMap::initDomeIcon() {
    GalaxyMapDomeIcon* domeIcon;

    mDomeIcon.init(6);

    for (u32 i = 0; i < 6; i++) {
        domeIcon = new GalaxyMapDomeIcon(i + 1, this, ::cDomes[i], ::cDomeShowHidePanes[i]);
        domeIcon->initWithoutIter();

        mDomeIcon.push_back(domeIcon);
    }
}

namespace {
    GalaxyMapMarioIcon* createMarioIcon(LayoutActor* pHost, const char* pPaneName) {
        GalaxyMapMarioIcon* pIcon = new GalaxyMapMarioIcon(pHost, pPaneName);
        pIcon->initWithoutIter();
        return pIcon;
    }
};  // namespace

void GalaxyMap::initMarioIcon() {
    s32 scenarioNo = 0;

    if (MR::isEqualStageName("AstroDome")) {
        scenarioNo = MR::getCurrentScenarioNo();
    }
    // A dome past the game's six has no places on the map's own layout.
    if (scenarioNo > 6) {
        scenarioNo = 0;
    }

    mMarioIcon1 = ::createMarioIcon(this, ::cMarioIconPositions[scenarioNo]);
    const char* pDomePosition = ::cMarioIconPositionsInDome[scenarioNo];

    if (pDomePosition != nullptr) {
        mMarioIcon2 = ::createMarioIcon(this, pDomePosition);
    }
}

void GalaxyMap::initTicoIcon() {
    GalaxyMapTicoIcon* ticoIcon;
    MR::FormattedName exclamationGalaxyName;
    s32 exclamationGalaxyNum = GameEventFlagTable::calcExclamationGalaxyNum();

    mTicoIcon.init(exclamationGalaxyNum);

    for (s32 i = 0; i < exclamationGalaxyNum; i++) {
        exclamationGalaxyName.format("Tico%d", i + 1);

        // A slot no Luma uses, or a Luma a mod added with no place on its map.
        if (GameEventFlagTable::getExclamationGalaxyNameFromIndex(i) == nullptr || getLayoutManager()->findPaneByName(exclamationGalaxyName.c_str()) == nullptr) {
            continue;
        }

        ticoIcon = new GalaxyMapTicoIcon(i, GameEventFlagTable::getExclamationGalaxyNameFromIndex(i), this, exclamationGalaxyName.c_str());
        ticoIcon->initWithoutIter();

        mTicoIcon.push_back(ticoIcon);
    }
}

bool GalaxyMap::tryFocusIn() {
    if (!mIsFocusEnabled) {
        mGalaxyName = nullptr;
        mPointingIcon = nullptr;

        setNerve(GET_NERVE_ANON(GalaxyMapIdle));

        return true;
    }

    GalaxyMapIcon* pointingGalaxyIcon = getPointingGalaxyIcon();

    if (pointingGalaxyIcon != mPointingIcon) {
        const Nerve* nerve = GET_NERVE_ANON(GalaxyMapFocusOut);

        if (pointingGalaxyIcon != nullptr) {
            nerve = GET_NERVE_ANON(GalaxyMapFocusIn);
            mGalaxyName = pointingGalaxyIcon->mGalaxyName;
        }

        if (pointingGalaxyIcon != nullptr) {
            mIsPointingNew = pointingGalaxyIcon->isStatusNew();

            MR::startSystemSE("SE_SY_GALAMAP_ICON_POINT");
        }

        mPointingIcon = pointingGalaxyIcon;

        setNerve(nerve);

        return true;
    }

    if (pointingGalaxyIcon != nullptr && MR::isPressedJump(PLAYER_1)) {
        setNerve(GET_NERVE_ANON(GalaxyMapShowDetail));

        return true;
    }

    return false;
}

void GalaxyMap::updateGalaxyName() {
    if (mPointingIcon != nullptr) {
        mGalaxyPlain->show(mGalaxyName, mPointingIcon->mPaneName);
    }
}

void GalaxyMap::drawGalaxyIconWithOrder(int param1) const {
    for (s32 i = 0; i < mIcon.size(); i++) {
        s32 value = 1;

        if (mPointingIcon != nullptr && mPointingIcon == mIcon[i]) {
            value = 3;
        }

        if (mIcon[i]->isNewOnDiscoverMode()) {
            value = 2;
        }

        if (value == param1) {
            mIcon[i]->draw();
        }
    }
}

GalaxyMapIcon* GalaxyMap::getPointingGalaxyIcon() const {
    for (s32 i = 0; i < mIcon.size(); i++) {
        GalaxyMapIcon* icon = mIcon[i];

        if (icon->isPointing()) {
            return icon;
        }
    }

    return nullptr;
}
