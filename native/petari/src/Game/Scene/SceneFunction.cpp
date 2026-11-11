#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/NameObj/NameObjListExecutor.hpp"
#include "Game/Scene/SceneDataInitializer.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"

namespace {
    SceneDataInitializer* getSceneDataInitializer() {
        return MR::getSceneObj< SceneDataInitializer >(SceneObj_SceneDataInitializer);
    }
};  // namespace

void SceneFunction::createStageDataHolder() {
    ::getSceneDataInitializer()->createStageDataHolder();
}

void SceneFunction::startActorPlacement() {
    ::getSceneDataInitializer()->startActorPlacement();
}

bool SceneFunction::startActorPlacementStep(uint64_t deadline) {
    return ::getSceneDataInitializer()->startActorPlacementStep(deadline);
}

void SceneFunction::initAfterScenarioSelected() {
    ::getSceneDataInitializer()->initAfterScenarioSelected();
}

void SceneFunction::initForNameObj() {
    MR::createSceneObj(SceneObj_NameObjExecuteHolder);
    MR::createSceneObj(SceneObj_StopSceneController);
    MR::createSceneObj(SceneObj_SceneNameObjMovementController);
}

void SceneFunction::initForLiveActor() {
    MR::createSceneObj(SceneObj_AllLiveActorGroup);
    MR::createSceneObj(SceneObj_ClippingDirector);
    MR::createSceneObj(SceneObj_DemoDirector);
    MR::createSceneObj(SceneObj_SensorHitChecker);
    MR::createSceneObj(SceneObj_CollisionDirector);
    MR::createSceneObj(SceneObj_MessageSensorHolder);
    MR::createSceneObj(SceneObj_LiveActorGroupArray);
    MR::createSceneObj(SceneObj_MovementOnOffGroupHolder);
    MR::createSceneObj(SceneObj_LightDirector);
    MR::createSceneObj(SceneObj_AreaObjContainer);
    MR::createSceneObj(SceneObj_CaptureScreenActor);
    MR::createSceneObj(SceneObj_StageSwitchContainer);
    MR::createSceneObj(SceneObj_SwitchWatcherHolder);
    MR::createSceneObj(SceneObj_SleepControllerHolder);
    MR::createSceneObj(SceneObj_TalkDirector);
    MR::createSceneObj(SceneObj_NPCDirector);
}

void SceneFunction::initEffectSystem(u32 emitterCount) {
    MR::createSceneObj(SceneObj_EffectSystem);
    MR::getEffectSystem()->entry(MR::getParticleResourceHolder(), emitterCount);
}

void SceneFunction::allocateDrawBufferActorList() {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    pListExecutor->allocateDrawBufferActorList();
    MR::initConnectting();
}

void CategoryList::execute(MR::MovementType type) {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    pListExecutor->executeMovement(type);
}

void CategoryList::execute(MR::CalcAnimType type) {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    pListExecutor->executeCalcAnim(type);
}

void CategoryList::execute(MR::DrawType type) {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    // What these draws are, for a renderer that needs to know (nebula_frame_list.h NEBULA_CATEGORY_*).
    const s32 number = static_cast< s32 >(type);
    const bool shadow = type == MR::DrawType_ShadowSurface || type == MR::DrawType_ShadowVolume || type == MR::DrawType_AlphaShadow;
    Frame::category(static_cast< u8 >(shadow ? NEBULA_CATEGORY_GAME_SHADOW : number >= 0 && number < 128 ? NEBULA_CATEGORY_DRAW + number : 0));
    pListExecutor->executeDraw(type);
    Frame::category(0);
}

void CategoryList::entryDrawBuffer2D() {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    pListExecutor->entryDrawBuffer2D();
}

void CategoryList::entryDrawBuffer3D() {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    pListExecutor->entryDrawBuffer3D();
}

void CategoryList::entryDrawBufferMirror() {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    pListExecutor->entryDrawBufferMirror();
}

void CategoryList::drawOpa(MR::DrawBufferType type) {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    Frame::category(static_cast< u8 >(type + 1));
    pListExecutor->drawOpa(type);
    Frame::category(0);
}

void CategoryList::drawXlu(MR::DrawBufferType type) {
    NameObjListExecutor* pListExecutor = SingletonHolder< GameSystem >::get()->mSceneController->getNameObjListExecutor();

    Frame::category(static_cast< u8 >(type + 1));
    pListExecutor->drawXlu(type);
    Frame::category(0);
}

void CategoryList::requestMovementOn(MR::MovementType type) {
    MR::requestMovementOnWithCategory(type);
}

void CategoryList::requestMovementOff(MR::MovementType type) {
    MR::requestMovementOffWithCategory(type);
}

static_assert(MR::DrawBufferType_Sky + 1 == NEBULA_CATEGORY_SKY && MR::DrawBufferType_Planet + 1 == NEBULA_CATEGORY_PLANET &&
              MR::DrawBufferType_NoShadowedMapObj + 1 == NEBULA_CATEGORY_NO_SHADOWED_MAP_OBJ && MR::DrawBufferType_NPC + 1 == NEBULA_CATEGORY_NPC &&
              MR::DrawBufferType_Player + 1 == NEBULA_CATEGORY_PLAYER && MR::DrawBufferType_CrystalBox + 1 == NEBULA_CATEGORY_CRYSTAL_BOX &&
              MR::DrawBufferType_BloomModel + 1 == NEBULA_CATEGORY_BLOOM_MODEL && MR::DrawBufferType_Crystal + 1 == NEBULA_CATEGORY_CRYSTAL &&
              MR::DrawBufferType_GlaringLight + 1 == NEBULA_CATEGORY_GLARING_LIGHT &&
              MR::DrawBufferType_AstroDomeSky + 1 == NEBULA_CATEGORY_ASTRO_DOME_SKY &&
              MR::DrawBufferType_MirrorMapObj + 1 == NEBULA_CATEGORY_MIRROR_MAP_OBJ,
              "nebula_frame_list.h's categories are the draw buffer types");
