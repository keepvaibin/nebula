#include "Game/Screen/CameraInfo.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const s32 sLifeTime = NebulaFrameClock::stepsOfSeconds(2);
};  // namespace

namespace NrvCameraInfo {
    NEW_NERVE(CameraInfoNrvHide, CameraInfo, Hide);
    NEW_NERVE(CameraInfoNrvAppear, CameraInfo, Appear);
    NEW_NERVE(CameraInfoNrvNormal, CameraInfo, Normal);
    NEW_NERVE(CameraInfoNrvFirstPerson, CameraInfo, FirstPerson);
    NEW_NERVE(CameraInfoNrvEnd, CameraInfo, End);
};  // namespace NrvCameraInfo

CameraInfo::CameraInfo(const char* pName) : LayoutActor(pName, true), mStep(-1), mIsActive(true) {
    lumacore::setClass(this, lumacore::kClassId< CameraInfo >);
}

void CameraInfo::init(const TableIter& /*rIter*/) {
    initLayoutManager("CameraInfo", 2);
    MR::createAndAddPaneCtrl(this, "Forbid", 1);
    MR::hidePane(this, "Forbid");
    MR::connectToSceneLayout(this);
    initNerve(GET_NERVE(CameraInfo, CameraInfoNrvHide));
    appear();
}

void CameraInfo::control() {
    if (tryShow()) {
        return;
    }

    if (tryHide()) {
        return;
    }
}

void CameraInfo::movement() {
    if (!mIsActive) {
        return;
    }

    LayoutActor::movement();
}

void CameraInfo::calcAnim() {
    if (!mIsActive) {
        return;
    }

    LayoutActor::calcAnim();
}

void CameraInfo::draw() const {
    if (!mIsActive) {
        return;
    }

    LayoutActor::draw();
}

void CameraInfo::activate() {
    mIsActive = true;
}

void CameraInfo::deactivate() {
    mIsActive = false;
}

void CameraInfo::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::hideLayout(this);
    }
}

void CameraInfo::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::showLayout(this);
        MR::startAnim(this, "Appear", 0);
        hideCrossLineAll();

        mCanSkipAppearAnim = true;
        mStep = 0;
    }

    tryChangeCameraStateNerve();
}

void CameraInfo::exeNormal() {
    if (tryChangeCameraStateNerve()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Camera", 1);
        tryAnimShortCut();
        MR::hidePane(this, "LineD");
    }

    if (MR::isPossibleToShiftToFirstPersonCamera()) {
        MR::showPane(this, "LineU");
    } else {
        MR::hidePane(this, "LineU");
    }

    if (MR::isCameraPossibleToRoundLeft()) {
        MR::showPane(this, "LineL");
    } else {
        MR::hidePane(this, "LineL");
    }

    if (MR::isCameraPossibleToRoundRight()) {
        MR::showPane(this, "LineR");
    } else {
        MR::hidePane(this, "LineR");
    }

    updateCameraNG();
}

void CameraInfo::exeFirstPerson() {
    if (tryChangeCameraStateNerve()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "FirstPerson", 1);
        tryAnimShortCut();
        hideCrossLineAll();
        MR::showPane(this, "LineD");
    }

    updateCameraNG();
}

void CameraInfo::exeEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "End", 0);
    }

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE(CameraInfo, CameraInfoNrvHide));
    }
}

void CameraInfo::updateCameraNG() {
    if (MR::isHiddenPane(this, "Forbid")) {
        if (MR::isCameraControlNG()) {
            MR::showPane(this, "Forbid");
            MR::startPaneAnim(this, "Forbid", "Forbid", 0);
        }
    } else if (MR::isPaneAnimStopped(this, "Forbid", 0)) {
        MR::hidePane(this, "Forbid");
    }
}

void CameraInfo::hideCrossLineAll() {
    MR::hidePane(this, "LineU");
    MR::hidePane(this, "LineL");
    MR::hidePane(this, "LineR");
    MR::hidePane(this, "LineD");
}

bool CameraInfo::tryAnimShortCut() {
    if (!mCanSkipAppearAnim) {
        return false;
    }

    MR::setAnimFrameAndStop(this, MR::getAnimFrameMax(this, 1), 1);

    mCanSkipAppearAnim = false;

    return true;
}

bool CameraInfo::tryShow() {
    if (isNerve(GET_NERVE(CameraInfo, CameraInfoNrvAppear))) {
        return false;
    }

    if (isNerve(GET_NERVE(CameraInfo, CameraInfoNrvNormal))) {
        return false;
    }

    if (isNerve(GET_NERVE(CameraInfo, CameraInfoNrvFirstPerson))) {
        return false;
    }

    bool isTriggerAnyDir = MR::isPressedUp(PLAYER_1) || MR::isPressedDown(PLAYER_1) || MR::isPressedLeft(PLAYER_1) ||
                           MR::isPressedRight(PLAYER_1);

    if (!isTriggerAnyDir) {
        return false;
    }

    setNerve(GET_NERVE(CameraInfo, CameraInfoNrvAppear));

    return true;
}

bool CameraInfo::tryHide() {
    if (isNerve(GET_NERVE(CameraInfo, CameraInfoNrvEnd))) {
        return false;
    }

    if (isNerve(GET_NERVE(CameraInfo, CameraInfoNrvHide))) {
        return false;
    }

    if (isNerve(GET_NERVE(CameraInfo, CameraInfoNrvFirstPerson))) {
        return false;
    }

    bool isTriggerAnyDir = MR::isPressedUp(PLAYER_1) || MR::isPressedDown(PLAYER_1) || MR::isPressedLeft(PLAYER_1) ||
                           MR::isPressedRight(PLAYER_1);

    if (isTriggerAnyDir || getNerveStep() == 0) {
        mStep = 0;
    }

    if (mStep >= ::sLifeTime) {
        setNerve(GET_NERVE(CameraInfo, CameraInfoNrvEnd));

        return true;
    }

    mStep++;

    return false;
}

bool CameraInfo::tryChangeCameraStateNerve() {
    if (MR::isFirstPersonCamera()) {
        if (!isNerve(GET_NERVE(CameraInfo, CameraInfoNrvFirstPerson))) {
            setNerve(GET_NERVE(CameraInfo, CameraInfoNrvFirstPerson));

            return true;
        }

        return false;
    } else if (!isNerve(GET_NERVE(CameraInfo, CameraInfoNrvNormal))) {
        setNerve(GET_NERVE(CameraInfo, CameraInfoNrvNormal));

        return true;
    }

    return false;
}
