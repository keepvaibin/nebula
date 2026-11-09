#include "Game/Scene/SceneNameObjListExecutor.hpp"
#include "Game/Scene/SceneFunction.hpp"

namespace {
    // One category per value of each enum; the lists size each category by the objects registered to it.
    const u32 cMovementTypeNum = MR::MovementType_ShadowControllerHolder + 1;
    const u32 cCalcAnimTypeNum = MR::CalcAnimType_AnimParticleIgnorePause + 1;
    const u32 cDrawTypeNum = MR::DrawType_CameraCover + 1;

    // One row per draw buffer type.
    const DrawBufferInitialTable cDrawBufferListInitTable[] = {
        {MR::DrawBufferType_Planet, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectPlanet, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Player, MR::LightType_Player, MR::CameraType_3D},
        {MR::DrawBufferType_PlayerDecoration, MR::LightType_Player, MR::CameraType_3D},
        {MR::DrawBufferType_CrystalBox, MR::LightType_Player, MR::CameraType_3D},
        {MR::DrawBufferType_PlayerParts, MR::LightType_Weak, MR::CameraType_3D},
        {MR::DrawBufferType_NPC, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_Enemy, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_EnemyDecoration, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_TripodBoss, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_ClippedMapParts, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectMapObj, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectMapObjStrongLight, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectNpc, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_IndirectEnemy, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_Ride, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_NoShadowedMapObj, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_NoShadowedMapObjStrongLight, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_NoSilhouettedMapObj, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_NoSilhouettedMapObjWeakLight, MR::LightType_Weak, MR::CameraType_3D},
        {MR::DrawBufferType_NoSilhouettedMapObjStrongLight, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_MapObj, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_MapObjWeakLight, MR::LightType_Weak, MR::CameraType_3D},
        {MR::DrawBufferType_MapObjStrongLight, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_MirrorMapObj, MR::LightType_Planet, MR::CameraType_Mirror},
        {MR::DrawBufferType_Crystal, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_CrystalItem, MR::LightType_Strong, MR::CameraType_3D},
        {MR::DrawBufferType_GlaringLight, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_PlanetLow, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Sky, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Air, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Sun, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Environment, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_EnvironmentStrongLight, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_AstroDomeSky, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_BloomModel, MR::LightType_Planet, MR::CameraType_3D},
        {MR::DrawBufferType_Model3DFor2D, MR::LightType_None, MR::CameraType_2D},
    };

    static_assert(ARRAY_SIZE(cDrawBufferListInitTable) == MR::DrawBufferType_MirrorMapObj + 1, "one row per draw buffer type");
};  // namespace

void SceneNameObjListExecutor::initMovementList() {
    mMovementList = new NameObjCategoryList(::cMovementTypeNum, &NameObj::executeMovement, 0);
}

void SceneNameObjListExecutor::initCalcAnimList() {
    mCalcAnimList = new NameObjCategoryList(::cCalcAnimTypeNum, &NameObj::calcAnim, 1);
}

void SceneNameObjListExecutor::initCalcViewAndEntryList() {
    mBufferHolder = new DrawBufferHolder();
    mBufferHolder->initTable(::cDrawBufferListInitTable, ARRAY_SIZE(::cDrawBufferListInitTable));
}

void SceneNameObjListExecutor::initDrawList() {
    mDrawList = new NameObjCategoryList(::cDrawTypeNum, &NameObj::draw, 2);
}
