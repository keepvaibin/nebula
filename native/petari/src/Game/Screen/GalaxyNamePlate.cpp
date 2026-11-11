#include "Game/Screen/GalaxyNamePlate.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/GalaxyNamePlateDrawer.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(s32, cAppearReadyFrame, 40);
};  // namespace

namespace NrvGalaxyNamePlate {
    NEW_NERVE(GalaxyNamePlateNrvAppearReady, GalaxyNamePlate, AppearReady);
    NEW_NERVE(GalaxyNamePlateNrvAppear, GalaxyNamePlate, Appear);
    NEW_NERVE(GalaxyNamePlateNrvWait, GalaxyNamePlate, Wait);
};  // namespace NrvGalaxyNamePlate

GalaxyNamePlate::GalaxyNamePlate(const char* pGalaxyName, bool param2)
    : LayoutActor("ギャラクシー名プレート", true), mGalaxyName(pGalaxyName), mIsShownThisFrame(false), mIsAppearNow(false), mShowBalloonNozzle(true), mDrawerEntry(nullptr),
      mStateFrame(0), mPlayNewSe(true) {
    lumacore::setClass(this, lumacore::kClassId< GalaxyNamePlate >);
    initLayoutManager("GalaxyNamePlate", 3);

    if (param2) {
        MR::connectToSceneLayoutMovementCalcAnim(this);

        mDrawerEntry = MR::registerToGalaxyNamePlateDrawer(this);
    }

    initEffectKeeper(0, nullptr, nullptr);
    MR::startAnim(this, "OneLine", 2);
}

void GalaxyNamePlate::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvAppear));
}

void GalaxyNamePlate::kill() {
    MR::forceDeleteEffectAll(this);
    LayoutActor::kill();
}

void GalaxyNamePlate::showUnknown(bool a1) {
    show(MR::getGameMessageDirect("GalaxyNameShort_Unknown"), 0, true, a1);
}

void GalaxyNamePlate::showNew(bool a1) {
    show(MR::getGameMessageDirect("GalaxyNameShort_New"), 1, true, a1);
}

void GalaxyNamePlate::showOpen(bool a1) {
    show(MR::getGalaxyNameShortOnCurrentLanguage(mGalaxyName), 2, true, a1);
}

void GalaxyNamePlate::show(const char16_t* pName, bool a2) {
    show(pName, 2, a2, true);
}

void GalaxyNamePlate::setPos3D(const Vec3f& a1) {
    Vec3f vec;
    MR::calcScreenPosition(&vec, a1);
    setTrans(vec);
    mDrawerEntry->mZ = -vec.z * MR::getFarZ();
}

void GalaxyNamePlate::setShowBalloonNozzle(bool showBalloonNozzle) {
    mShowBalloonNozzle = showBalloonNozzle;
}

void GalaxyNamePlate::control() {
    mIsShownThisFrame = false;
}

void GalaxyNamePlate::show(const char16_t* pName, s32 a2, bool a3, bool a4) {
    if (MR::isDead(this)) {
        appear();
        if (a4) {
            setNerve(GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvAppear));
        } else {
            setNerve(GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvAppearReady));
        }
    } else {
        if (a4) {
            if (isNerve(GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvAppearReady))) {
                setNerve(GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvAppear));
            }
        } else if (mIsAppearNow) {
            setNerve(GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvAppearReady));
        }

        if (!isNerve(GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvWait)) || a2 != 1) {
            MR::forceDeleteEffect(this, "GalaxyNamePlateNew");
        }
    }

    const char *ShowNamePlate, *HideNamePlate, *shaBeak, *picBeak, *galaxyName, *txtGaxyName;
    ShowNamePlate = (a3) ? "GalaxyNamePlate" : "GalaxyNamePlateU";
    MR::showPaneRecursive(this, ShowNamePlate);
    HideNamePlate = (a3) ? "GalaxyNamePlateU" : "GalaxyNamePlate";
    MR::hidePaneRecursive(this, HideNamePlate);
    if (!mShowBalloonNozzle) {
        shaBeak = (a3) ? "ShaBeak" : "ShaBeakU";
        picBeak = (a3) ? "PicBeak" : "PicBeakU";
        MR::hidePaneRecursive(this, shaBeak);
        MR::hidePaneRecursive(this, picBeak);
    }

    galaxyName = (a3) ? "GalaxyName" : "GalaxyNameU";
    MR::setTextBoxMessageRecursive(this, galaxyName, pName);

    txtGaxyName = (a3) ? "TxtGalaxyName" : "TxtGalaxyNameU";
    MR::setAnimFrameAndStopAdjustTextWidth(this, txtGaxyName, 2);

    MR::startAnim(this, "Unknown", 1);
    MR::setAnimFrameAndStop(this, a2, 1);
    mIsAppearNow = a4;
    mIsShownThisFrame = true;
    mStateFrame = a2;
}

void GalaxyNamePlate::exeAppearReady() {
    if (MR::isFirstStep(this)) {
        MR::hideLayout(this);
    }

    if (!mIsShownThisFrame) {
        kill();
    } else {
        MR::setNerveAtStep(this, GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvAppear), ::cAppearReadyFrame);
    }
}

void GalaxyNamePlate::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Appear", 0);
        MR::showLayout(this);
    }

    if (!mIsShownThisFrame) {
        kill();
    } else {
        MR::setNerveAtAnimStopped(this, GET_NERVE(GalaxyNamePlate, GalaxyNamePlateNrvWait), 0);
    }
}

void GalaxyNamePlate::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Wait", 0);

        if (mStateFrame == 1) {
            MR::emitEffect(this, "GalaxyNamePlateNew");

            if (mPlayNewSe) {
                MR::startSystemSE("SE_SY_ASTRO_GALAXY_NEW_EFF");
            }
        }

        mPlayNewSe = false;
    }

    if (!mIsShownThisFrame) {
        kill();
    }
}
