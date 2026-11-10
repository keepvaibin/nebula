#include "Game/Map/SleepControllerHolder.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/SleepController.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SwitchEventFunctorListener.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    SleepControllerHolder* getSleepControllerHolder() {
        return MR::getSceneObj< SleepControllerHolder >(SceneObj_SleepControllerHolder);
    }
};  // namespace

SleepControllerHolder::SleepControllerHolder() : NameObj("オブジェスリープ管理"), mSleepControl() {
    lumacore::setClass(this, lumacore::kClassId< SleepControllerHolder >);
    MR::connectToSceneMapObjMovement(this);
}

void SleepControllerHolder::movement() {
    for (auto& item : mSleepControl) {
        item->update();
    }
}

void SleepControllerHolder::add(NameObj* /*pObj*/, const TableIter& rIter, SwitchEventListener* pListener) {
    SleepController* sleepControl = new SleepController(rIter, pListener);

    mSleepControl.push_back(sleepControl);
}

void SleepControllerHolder::initSync() {
    for (auto& item : mSleepControl) {
        item->initSync();
    }
}

namespace SleepControlFunc {
    void initSyncSleepController() {
        ::getSleepControllerHolder()->initSync();
    }

    void addSleepControl(AreaObj* pObj, const TableIter& rIter) {
        SwitchEventFunctorListener* listener;

        if (!MR::isExistStageSwitchSleep(rIter)) {
            return;
        }

        listener = new SwitchEventFunctorListener();
        listener->setOnFunctor(MR::Functor(pObj, &AreaObj::awake));
        listener->setOffFunctor(MR::Functor(pObj, &AreaObj::sleep));

        ::getSleepControllerHolder()->add(pObj, rIter, listener);
    }

    void addSleepControl(LiveActor* pActor, const TableIter& rIter) {
        SwitchEventFunctorListener* listener;

        if (!MR::isExistStageSwitchSleep(rIter)) {
            return;
        }

        if (MR::isExistStageSwitchAppear(rIter)) {
        }

        listener = new SwitchEventFunctorListener();
        listener->setOnFunctor(MR::Functor(pActor, &LiveActor::makeActorAppeared));
        listener->setOffFunctor(MR::Functor(pActor, &LiveActor::makeActorDead));

        ::getSleepControllerHolder()->add(pActor, rIter, listener);
    }
};  // namespace SleepControlFunc
