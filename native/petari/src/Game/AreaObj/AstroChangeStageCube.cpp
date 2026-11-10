#include "Game/AreaObj/AstroChangeStageCube.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(s32, cChangeStageWaitFrame, 5);
    NEBULA_TUNABLE(s32, cHeavensDoorScenarioNo, 2);
    NEBULA_TUNABLE(s32, cWipeFrame, 110);
};  // namespace

AstroChangeStageCube::AstroChangeStageCube(int formType, const char* pName) : AreaObj(formType, pName), mDestNo(1), mWaitFrame() {
    lumacore::setClass(this, lumacore::kClassId< AstroChangeStageCube >);
}

void AstroChangeStageCube::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);

    switch (mObjArg0) {
    case 0:
        mDestNo = MR::getCurrentScenarioNo();
        break;
    case 1:
    case 2:
    case 3:
    case 4:
        mDestNo = mObjArg1;
        break;
    case 5:
        mDestNo = mObjArg1;
        break;
    }
}

bool AstroChangeStageCube::isInVolume(const Vec3f& rPos) const {
    if (MR::isPlayerInBind() || MR::isDemoActive()) {
        return false;
    }

    if (isValidSwitchA() && isOnSwitchA()) {
        return false;
    }

    switch (mObjArg0) {
    case 5:
        if (!MR::hasGrandStar(mObjArg2)) {
            return false;
        }
        break;
    case 1:
        if (!MR::isOnGameEventFlagChildsRoom()) {
            return false;
        }
        break;
    case 3:
        if (!MR::isOnGameEventFlagLibraryRoom()) {
            return false;
        }
        break;
    }

    return AreaObj::isInVolume(rPos);
}

void AstroChangeStageCube::movement() {
    if (isValid()) {
        Vec3f* playerPos = MR::getPlayerPos();

        if (!isInVolume(*playerPos)) {
            return;
        }

        mIsValid = false;

        MR::stopStageBGM(::cChangeStageWaitFrame);
        MR::stopSubBGM(::cChangeStageWaitFrame);

        switch (mObjArg0) {
        case 0:
        case 2:
        case 4:
            MR::startSystemSE("SE_DM_ASTRO_WIPE_OUT");
            break;
        case 1:
        case 3:
        case 5:
        default:
            MR::startSystemSE("SE_DM_ASTRO_WIPE_IN");
            break;
        }

        mWaitFrame = 0;
    } else if (mWaitFrame < ::cChangeStageWaitFrame) {
        mWaitFrame++;
    } else {
        MR::closeSystemWipeCircleWithCaptureScreen(::cWipeFrame);
        MR::setWipeCircleCenterPos(*MR::getPlayerCenterPos());

        switch (mObjArg0) {
        case 0:
        case 4:
            MR::requestGoToAstroGalaxy(mDestNo);
            break;
        case 2:
            MR::requestChangeStageInGameMoving("AstroGalaxy", 1, PlacementId(mDestNo, 0));
            break;
        case 1:
            if (MR::isOnGameEventFlagRosettaTalkAboutTicoInTower()) {
                MR::requestStartScenarioSelect("HeavensDoorGalaxy");
            } else {
                MR::requestChangeStageInGameMoving("HeavensDoorGalaxy", ::cHeavensDoorScenarioNo, PlacementId(mDestNo, 0));
            }
            break;
        case 3:
            MR::requestChangeStageInGameMoving("LibraryRoom", 1, PlacementId(mDestNo, 0));
            break;
        case 5:
            MR::requestGoToAstroDomeFromAstroGalaxy(mObjArg2, mDestNo);
            break;
        }
    }
}
