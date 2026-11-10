#include "Game/Util/TextString.hpp"
#include "Game/Screen/GalaxyMapGalaxyPlain.hpp"
#include "Game/Screen/GalaxyNamePlate.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/LumaCore.hpp"

#ifdef __cplusplus
extern "C" {
};
#endif

GalaxyMapGalaxyPlain::GalaxyMapGalaxyPlain(const LayoutActor* pHost)
    : LayoutActor("Galaxy情報簡易表示", true), mHost(pHost), mPaneName(), mNamePlate(), mGalaxyName(), mIsShow() {
    lumacore::setClass(this, lumacore::kClassId< GalaxyMapGalaxyPlain >);
}

void GalaxyMapGalaxyPlain::show(const char* pGalaxyName, const char* pPaneName) {
    appear();

    mPaneName = pPaneName;
    if (!MR::isOnGameEventFlagGalaxyOpen(pGalaxyName)) {
        char16_t* pDstGalaxyName = mGalaxyName.room(2);
        MR::formatText(pDstGalaxyName, 2, u"?");
    } else {
        const char16_t* pShortGalaxyName = MR::getGalaxyNameShortOnCurrentLanguage(pGalaxyName);
        // The name whole, a space, two picture font tags (3 units each), a number font tag (its header and an s32's
        // digits: 15 units at most) and the terminator.
        char16_t* pDstGalaxyName = mGalaxyName.room(static_cast< u32 >(MR::textLength(pShortGalaxyName)) + 1 + 6 + 15 + 1);
        char16_t* pFontTagIter = pDstGalaxyName + MR::formatText(pDstGalaxyName, mGalaxyName.capacity(), u"%ls ", pShortGalaxyName);

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
    Box2f galaxyNameBox;
    MR::calcTextBoxRectRecursive(&galaxyNameBox, mNamePlate, "GalaxyName");

    Vec2f paneTrans;
    MR::copyPaneTrans(&paneTrans, mHost, mPaneName);

    Vec2f plateTrans(paneTrans);

    if (galaxyNameBox.i.x < MR::getScreenMargin()) {
        plateTrans.x += -(galaxyNameBox.i.x - MR::getScreenMargin());
    }

    if ((MR::getScreenWidth() - MR::getScreenMargin()) < galaxyNameBox.f.x) {
        plateTrans.x += (MR::getScreenWidth() - MR::getScreenMargin()) - galaxyNameBox.f.x;
    }

    mNamePlate->setTrans(plateTrans);
}

void GalaxyMapGalaxyPlain::init(const TableIter& /*rIter*/) {
    mNamePlate = new GalaxyNamePlate(nullptr, false);
    mNamePlate->initWithoutIter();
    mNamePlate->setShowBalloonNozzle(false);
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

    const u32 units = static_cast< u32 >(MR::textLength(pGalaxyName)) + 1;
    MR::copyString(mGalaxyName.room(units), pGalaxyName, units);

    mIsShow = true;
}

void GalaxyMapGalaxyPlain::control() {
    if (mPaneName != nullptr) {
        MR::setLayoutScalePosAtPaneScaleTrans(mNamePlate, mHost, mPaneName);
    }

    if (mIsShow) {
        mNamePlate->show(mGalaxyName.c_str(), true);
    }

    mIsShow = false;
}
