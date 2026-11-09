#include "Game/Scene/SceneNameObjListExecutor.hpp"
#include "Game/Scene/SceneFunction.hpp"

namespace {
    // One category per value of each enum; the lists size each category by the objects registered to it.
    const u32 cMovementTypeNum = MR::MovementType_ShadowControllerHolder + 1;
    const u32 cCalcAnimTypeNum = MR::CalcAnimType_AnimParticleIgnorePause + 1;
    const u32 cDrawTypeNum = MR::DrawType_CameraCover + 1;

    // The model groups start with room for this many models each and grow past it.
    const DrawBufferInitialTable cDrawBufferListInitTable[] = {
        {MR::DrawBufferType_Planet, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectPlanet, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Player, 4, MR::LightType_Player, MR::CameraType_3D},
        {MR::DrawBufferType_PlayerDecoration, 8, MR::LightType_Player, MR::CameraType_3D},
        {MR::DrawBufferType_CrystalBox, 8, MR::LightType_Player, MR::CameraType_3D},
        {MR::DrawBufferType_PlayerParts, 1, MR::LightType_Weak, MR::CameraType_3D},
        {MR::DrawBufferType_NPC, 80, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_Enemy, 64, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_EnemyDecoration, 64, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_TripodBoss, 64, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_ClippedMapParts, 32, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectMapObj, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectMapObjStrongLight, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectNpc, 16, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectEnemy, 64, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_Ride, 32, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_NoShadowedMapObj, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_NoShadowedMapObjStrongLight, 64, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_NoSilhouettedMapObj, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_NoSilhouettedMapObjWeakLight, 64, MR::LightType_Weak, MR::CameraType_3D},
        {MR::DrawBufferType_NoSilhouettedMapObjStrongLight, 64, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_MapObj, 256, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_MapObjWeakLight, 32, MR::LightType_Weak, MR::CameraType_3D},
        {MR::DrawBufferType_MapObjStrongLight, 64, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_MirrorMapObj, 256, MR::LightType_Planet, MR::CameraType_Mirror},
        {MR::DrawBufferType_Crystal, 8, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_CrystalItem, 8, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_GlaringLight, 8, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_PlanetLow, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Sky, 4, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Air, 8, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Sun, 4, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Environment, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_EnvironmentStrongLight, 64, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_AstroDomeSky, 1, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_BloomModel, 16, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Model3DFor2D, 16, MR::LightType_None, MR::CameraType_2D},
        {MR::DrawBufferType_None, 0, MR::LightType_None, MR::CameraType_3D},
    };
};  // namespace

void SceneNameObjListExecutor::initMovementList() {
    mMovementList = new NameObjCategoryList(::cMovementTypeNum, &NameObj::executeMovement);
}

void SceneNameObjListExecutor::initCalcAnimList() {
    mCalcAnimList = new NameObjCategoryList(::cCalcAnimTypeNum, &NameObj::calcAnim);
}

void SceneNameObjListExecutor::initCalcViewAndEntryList() {
    mBufferHolder = new DrawBufferHolder();
    mBufferHolder->initTable(::cDrawBufferListInitTable, ARRAY_SIZE(::cDrawBufferListInitTable) - 1);
}

void SceneNameObjListExecutor::initDrawList() {
    mDrawList = new NameObjCategoryList(::cDrawTypeNum, &NameObj::draw);
}
