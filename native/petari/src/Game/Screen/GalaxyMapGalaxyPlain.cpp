#include "Game/Util/TextString.hpp"
#include "Game/Screen/GalaxyMapGalaxyPlain.hpp"
#include "Game/Screen/GalaxyNamePlate.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"

#define GALAXY_NAME_BUFFER_SIZE 128

#ifdef __cplusplus
extern "C" {
};
#endif

GalaxyMapGalaxyPlain::GalaxyMapGalaxyPlain(const LayoutActor* pHost)
    : LayoutActor("Galaxy\217\356\225\361\212\310\210\325\225\134\216\246" /* Galaxy情報簡易表示 */, true), mHost(pHost), mPaneName(), mNamePlate(), mGalaxyName(), mIsShow() {
}

void GalaxyMapGalaxyPlain::show(const char* pGalaxyName, const char* pPaneName) {
    appear();

    mPaneName = pPaneName;
    char16_t* pDstGalaxyName = mGalaxyName;

    if (!MR::isOnGameEventFlagGalaxyOpen(pGalaxyName)) {
        MR::formatText(pDstGalaxyName, GALAXY_NAME_BUFFER_SIZE, u"?");
    } else {
        const char16_t* pShortGalaxyName = MR::getGalaxyNameShortOnCurrentLanguage(pGalaxyName);
        char16_t* pFontTagIter = pDstGalaxyName + MR::formatText(pDstGalaxyName, GALAXY_NAME_BUFFER_SIZE, u"%ls ", pShortGalaxyName);

        if (MR::isStarComplete(pGalaxyName)) {
            MR::addPictureFontTag(pFontTagIter, 80);
        } else {
            pFontTagIter = MR::addPictureFontTag(pFontTagIter, 55);
            pFontTagIter = MR::addPictureFontTag(pFontTagIter, 64);

            MR::addNumberFontTag(pFontTagIter, MR::getPowerStarNumSucceed(pGalaxyName));
        }
    }

    mIsShow = true;
}

void GalaxyMapGalaxyPlain::adjustTransInScreen() {
    TBox2f galaxyNameBox;
    MR::calcTextBoxRectRecursive(&galaxyNameBox, mNamePlate, "GalaxyName");

    TVec2f paneTrans;
    MR::copyPaneTrans(&paneTrans, mHost, mPaneName);

    TVec2f plateTrans(paneTrans);

    if (galaxyNameBox.i.x < MR::getScreenMargin()) {
        plateTrans.x += -(galaxyNameBox.i.x - MR::getScreenMargin());
    }

    if ((MR::getScreenWidth() - MR::getScreenMargin()) < galaxyNameBox.f.x) {
        plateTrans.x += (MR::getScreenWidth() - MR::getScreenMargin()) - galaxyNameBox.f.x;
    }

    mNamePlate->setTrans(plateTrans);
}

void GalaxyMapGalaxyPlain::init(const JMapInfoIter& rIter) {
    mNamePlate = new GalaxyNamePlate(nullptr, false);
    mNamePlate->initWithoutIter();
    mNamePlate->setShowBalloonNozzle(false);

    mGalaxyName = new (MR::getSceneHeap(), 0) char16_t[GALAXY_NAME_BUFFER_SIZE];
}

void GalaxyMapGalaxyPlain::appear() {
    LayoutActor::appear();
}

void GalaxyMapGalaxyPlain::kill() {
    LayoutActor::kill();
    mNamePlate->kill();
}

void GalaxyMapGalaxyPlain::movement() {
    LayoutActor::movement();
    mNamePlate->movement();
}

void GalaxyMapGalaxyPlain::calcAnim() {
    LayoutActor::calcAnim();
    mNamePlate->calcAnim();

    if (MR::isExecuteCalcAnimLayout(mNamePlate)) {
        adjustTransInScreen();
        mNamePlate->calcAnim();
    }
}

void GalaxyMapGalaxyPlain::draw() const {
    if (!MR::isDead(this)) {
        mNamePlate->draw();
    }
}

void GalaxyMapGalaxyPlain::show(const char16_t* pGalaxyName, const char* pPaneName) {
    appear();

    mPaneName = pPaneName;

    MR::copyString(mGalaxyName, pGalaxyName, GALAXY_NAME_BUFFER_SIZE);

    mIsShow = true;
}

void GalaxyMapGalaxyPlain::control() {
    if (mPaneName != nullptr) {
        MR::setLayoutScalePosAtPaneScaleTrans(mNamePlate, mHost, mPaneName);
    }

    if (mIsShow) {
        mNamePlate->show(mGalaxyName, true);
    }

    mIsShow = false;
}
