#include "Game/Boss/TombSpiderEnvironment.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Map/PlanetMap.hpp"
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

TombSpiderEnvironment::TombSpiderEnvironment(LiveActor* pActor) : mActor(pActor), mPlanet(), mCocoon() {
    MR::setEffectHostSRT(mActor, "Noctiluca", MR::getPlayerPos(), nullptr, nullptr);
    MR::createSceneObj(SceneObj_SpiderThread);
    MR::initSpiderThread(mActor->mPosition);

    mPlanet = new PlanetMap("\220\355\217\352[\203\147\203\104\201\133\203\200\203\130\203\160\203\103\203\137\201\133]" /* 戦場[トゥームスパイダー] */, "TombSpiderPlanet");
    mPlanet->mPosition.set(mActor->mPosition);
    mPlanet->initWithoutIter();
    mPlanet->mLODCtrl->setDistanceToLow(25000.0f);
    MR::registerDemoSimpleCastAll(mPlanet);
    mPlanet->appear();

    mCocoon = new ModelObj("\202\334\202\344[\203\147\203\104\201\133\203\200\203\130\203\160\203\103\203\137\201\133]" /* まゆ[トゥームスパイダー] */, "TombSpiderCocoon", nullptr, -2, -2, -2, false);
    mCocoon->mPosition.set(mActor->mPosition);
    mCocoon->initWithoutIter();
    MR::invalidateClipping(mCocoon);
    MR::registerDemoSimpleCastAll(mCocoon);
    mCocoon->kill();
}
