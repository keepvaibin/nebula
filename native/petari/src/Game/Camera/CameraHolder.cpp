#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraAnim.hpp"
#include "Game/Camera/CameraBehind.hpp"
#include "Game/Camera/CameraBlackHole.hpp"
#include "Game/Camera/CameraCharmedFix.hpp"
#include "Game/Camera/CameraCharmedTripodBoss.hpp"
#include "Game/Camera/CameraCharmedVecReg.hpp"
#include "Game/Camera/CameraCharmedVecRegTower.hpp"
#include "Game/Camera/CameraCubePlanet.hpp"
#include "Game/Camera/CameraPointer.hpp"
#include "Game/Camera/CameraDead.hpp"
#include "Game/Camera/CameraFix.hpp"
#include "Game/Camera/CameraFixedPoint.hpp"
#include "Game/Camera/CameraFixedThere.hpp"
#include "Game/Camera/CameraFollow.hpp"
#include "Game/Camera/CameraFooFighter.hpp"
#include "Game/Camera/CameraFooFighterPlanet.hpp"
#include "Game/Camera/CameraFrontAndBack.hpp"
#include "Game/Camera/CameraGround.hpp"
#include "Game/Camera/CameraInnerCylinder.hpp"
#include "Game/Camera/CameraInwardSphere.hpp"
#include "Game/Camera/CameraInwardTower.hpp"
#include "Game/Camera/CameraMedianPlanet.hpp"
#include "Game/Camera/CameraMedianTower.hpp"
#include "Game/Camera/CameraMtxRegParallel.hpp"
#include "Game/Camera/CameraObjParallel.hpp"
#include "Game/Camera/CameraParallel.hpp"
#include "Game/Camera/CameraRaceFollow.hpp"
#include "Game/Camera/CameraRailDemo.hpp"
#include "Game/Camera/CameraRailFollow.hpp"
#include "Game/Camera/CameraRailWatch.hpp"
#include "Game/Camera/CameraSlide.hpp"
#include "Game/Camera/CameraSpiral.hpp"
#include "Game/Camera/CameraSubjective.hpp"
#include "Game/Camera/CameraTalk.hpp"
#include "Game/Camera/CameraTower.hpp"
#include "Game/Camera/CameraTowerPos.hpp"
#include "Game/Camera/CameraTripodBoss.hpp"
#include "Game/Camera/CameraTripodBossJoint.hpp"
#include "Game/Camera/CameraTripodPlanet.hpp"
#include "Game/Camera/CameraTrundle.hpp"
#include "Game/Camera/CameraTwistedPassage.hpp"
#include "Game/Camera/CameraWaterFollow.hpp"
#include "Game/Camera/CameraWaterPlanet.hpp"
#include "Game/Camera/CameraWaterPlanetBoss.hpp"
#include "Game/Camera/CameraWonderPlanet.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

struct CameraTableEntry {
    const char* mName;
    Camera* (*mCreateFunc)(void);
};

namespace {
    template < typename T >
    Camera* createCamera() {
        return new T();
    }

    static const CameraTableEntry sCameraTable[] = {
        {"CAM_TYPE_XZ_PARA", createCamera< CameraParallel >},
        {"CAM_TYPE_TOWER", createCamera< CameraTower >},
        {"CAM_TYPE_FOLLOW", createCamera< CameraFollow >},
        {"CAM_TYPE_WONDER_PLANET", createCamera< CameraWonderPlanet >},
        {"CAM_TYPE_POINT_FIX", createCamera< CameraFix >},
        {"CAM_TYPE_EYEPOS_FIX", createCamera< CameraFixedPoint >},
        {"CAM_TYPE_SLIDER", createCamera< CameraBehind >},
        {"CAM_TYPE_INWARD_TOWER", createCamera< CameraInwardTower >},
        {"CAM_TYPE_EYEPOS_FIX_THERE", createCamera< CameraFixedThere >},
        {"CAM_TYPE_TRIPOD_BOSS", createCamera< CameraTripodBoss >},
        {"CAM_TYPE_TOWER_POS", createCamera< CameraTowerPos >},
        {"CAM_TYPE_TRIPOD_PLANET", createCamera< CameraTripodPlanet >},
        {"CAM_TYPE_DEAD", createCamera< CameraDead >},
        {"CAM_TYPE_INWARD_SPHERE", createCamera< CameraInwardSphere >},
        {"CAM_TYPE_RAIL_DEMO", createCamera< CameraRailDemo >},
        {"CAM_TYPE_RAIL_FOLLOW", createCamera< CameraRailFollow >},
        {"CAM_TYPE_TRIPOD_BOSS_JOINT", createCamera< CameraTripodBossJoint >},
        {"CAM_TYPE_CHARMED_TRIPOD_BOSS", createCamera< CameraCharmedTripodBoss >},
        {"CAM_TYPE_OBJ_PARALLEL", createCamera< CameraObjParallel >},
        {"CAM_TYPE_CHARMED_FIX", createCamera< CameraCharmedFix >},
        {"CAM_TYPE_GROUND", createCamera< CameraGround >},
        {"CAM_TYPE_TRUNDLE", createCamera< CameraTrundle >},
        {"CAM_TYPE_CUBE_PLANET", createCamera< CameraCubePlanet >},
        {"CAM_TYPE_INNER_CYLINDER", createCamera< CameraInnerCylinder >},
        {"CAM_TYPE_SPIRAL_DEMO", createCamera< CameraSpiral >},
        {"CAM_TYPE_TALK", createCamera< CameraTalk >},
        {"CAM_TYPE_MTXREG_PARALLEL", createCamera< CameraMtxRegParallel >},
        {"CAM_TYPE_CHARMED_VECREG", createCamera< CameraCharmedVecReg >},
        {"CAM_TYPE_MEDIAN_PLANET", createCamera< CameraMedianPlanet >},
        {"CAM_TYPE_TWISTED_PASSAGE", createCamera< CameraTwistedPassage >},
        {"CAM_TYPE_MEDIAN_TOWER", createCamera< CameraMedianTower >},
        {"CAM_TYPE_CHARMED_VECREG_TOWER", createCamera< CameraCharmedVecRegTower >},
        {"CAM_TYPE_FRONT_AND_BACK", createCamera< CameraFrontAndBack >},
        {"CAM_TYPE_RACE_FOLLOW", createCamera< CameraRaceFollow >},
        {"CAM_TYPE_2D_SLIDE", createCamera< CameraSlide >},
        {"CAM_TYPE_FOO_FIGHTER", createCamera< CameraFooFighter >},
        {"CAM_TYPE_FOO_FIGHTER_PLANET", createCamera< CameraFooFighterPlanet >},
        {"CAM_TYPE_BLACK_HOLE", createCamera< CameraBlackHole >},
        {"CAM_TYPE_ANIM", createCamera< CameraAnim >},
        {"CAM_TYPE_DPD", createCamera< CameraPointer >},
        {"CAM_TYPE_WATER_FOLLOW", createCamera< CameraWaterFollow >},
        {"CAM_TYPE_WATER_PLANET", createCamera< CameraWaterPlanet >},
        {"CAM_TYPE_WATER_PLANET_BOSS", createCamera< CameraWaterPlanetBoss >},
        {"CAM_TYPE_RAIL_WATCH", createCamera< CameraRailWatch >},
        {"CAM_TYPE_SUBJECTIVE", createCamera< CameraSubjective >},
    };
    static const char* sDefaultCamera = "CAM_TYPE_XZ_PARA";
};  // namespace

CameraHolder::CameraHolder(const char* pName) : NameObj(pName), mIndex() {
    lumacore::setClass(this, lumacore::kClassId< CameraHolder >);
    createCameras();

    mIndex.reset(static_cast< u32 >(getNum()));
    for (s32 i = 0; i < getNum(); i++) {
        mIndex.add(getNameStrOf(i), static_cast< u32 >(i));
    }

    mDefaultCameraIndex = getIndexOf(getNameStrOfDefault());
}

s32 CameraHolder::getNum() const {
    return ARRAY_SIZE(::sCameraTable);
}

s32 CameraHolder::getIndexOf(const char* pName) const {
    const u32 index = mIndex.find(pName);
    return index == MR::NameIndex::cNoValue ? -1 : static_cast< s32 >(index);
}

const char* CameraHolder::getNameStrOf(s32 index) const {
    return ::sCameraTable[index].mName;
}

Camera* CameraHolder::getDefaultCamera() {
    return mCameras[mDefaultCameraIndex];
}

s32 CameraHolder::getIndexOfDefault() const {
    return mDefaultCameraIndex;
}

const char* CameraHolder::getNameStrOfDefault() const {
    return ::sDefaultCamera;
}

s32 CameraHolder::getIndexOf(Camera* pCamera) const {
    for (s32 i = 0; i < getNum(); i++) {
        if (mCameras[i] == pCamera) {
            return i;
        }
    }

    return -1;
}

void CameraHolder::createCameras() {
    mCameras = new Camera*[getNum()];

    for (s32 i = 0; i < getNum(); i++) {
        mCameras[i] = ::sCameraTable[i].mCreateFunc();
    }
}

Camera* CameraHolder::getCameraInner(s32 index) const {
    return mCameras[index];
}
