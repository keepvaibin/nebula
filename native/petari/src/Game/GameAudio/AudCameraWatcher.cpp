#include "Game/GameAudio/AudCameraWatcher.hpp"
#include "Game/AudioLib/AudEffector.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/GameAudio/AudBgmConductor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_movement_f1, "movement.f1", 20.0f);

AudCameraWatcher::AudCameraWatcher() : NameObj("AudCameraWatcher") {
    lumacore::setClass(this, lumacore::kClassId< AudCameraWatcher >);
}

void AudCameraWatcher::playAtmosphereSE() {
    if (MR::isCameraInWater()) {
        MR::startAtmosphereLevelSE("SE_AT_LV_UNDER_WATER");
        AudWrap::getSystem()->mAudEffector->setCutoff(3);
        MR::setStageBGMStateBit(AudBgmConductor::StateBit_CameraInWater);
        return;
    }
    AudWrap::getSystem()->mAudEffector->setCutoff(127);
}

void AudCameraWatcher::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_AudCameraWatcher, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void AudCameraWatcher::movement() {
    MR::setMicMtx(const_cast< Pos3f& >(MR::getCameraViewMtx()), 0);

    // A narrow view (first person, a zoom) hears as from afar.
    const bool isFar = MR::getFovy() < cNum_movement_f1;
    AudWrap::getSystem()->setFarCamera(isFar);

    playAtmosphereSE();
}
