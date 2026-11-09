#include "Game/Demo/DemoExecutorFunction.hpp"
#include "Game/Demo/DemoActionKeeper.hpp"
#include "Game/Demo/DemoExecutor.hpp"
#include "Game/Demo/DemoSubPartKeeper.hpp"
#include "Game/Demo/DemoTalkAnimCtrl.hpp"
#include "Game/Demo/DemoTimeKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace DemoExecutorFunction {
    void registerDemoActionFunction(const DemoExecutor* pExecutor, const LiveActor* pActor, const MR::FunctorBase& rFunctor, const char* pPartName) {
        pExecutor->mActionKeeper->registerFunctor(pActor, rFunctor, pPartName);
    }

    void registerDemoActionNerve(const DemoExecutor* pExecutor, const LiveActor* pActor, const Nerve* pNerve, const char* pPartName) {
        pExecutor->mActionKeeper->registerNerve(pActor, pNerve, pPartName);
    }

    bool isExistDemoPart(const DemoExecutor* pExecutor, const char* pPartName) {
        if (isExistDemoPartMain(pExecutor, pPartName)) {
            return true;
        }

        if (isExistDemoPartSub(pExecutor, pPartName)) {
            return true;
        }

        return false;
    }

    bool isExistDemoPartMain(const DemoExecutor* pExecutor, const char* pPartName) {
        DemoTimeKeeper* pTimeKeeper = pExecutor->mTimeKeeper;

        for (s32 i = 0; i < pTimeKeeper->mNumPartInfos; i++) {
            if (MR::isEqualString(pTimeKeeper->mMainPartInfos[i].mPartName, pPartName)) {
                return true;
            }
        }

        return false;
    }

    bool isExistDemoPartSub(const DemoExecutor* pExecutor, const char* pPartName) {
        DemoSubPartKeeper* pSubPartKeeper = pExecutor->mSubPartKeeper;

        for (s32 i = 0; i < pSubPartKeeper->mNumSubPartInfos; i++) {
            if (MR::isEqualString(pSubPartKeeper->mSubPartInfos[i].mSubPartName, pPartName)) {
                return true;
            }
        }

        return false;
    }

    bool isRegisteredDemoCast(const DemoExecutor* pExecutor, const LiveActor* pActor) {
        LiveActorGroup* pGroup = pExecutor->mGroup;

        for (s32 i = 0; i < pGroup->getObjNum(); i++) {
            if (MR::isSame(pGroup->getActor(i), pActor)) {
                return true;
            }
        }

        return false;
    }

    bool isRegisteredDemoActionAppear(const DemoExecutor* pExecutor, const LiveActor* pActor) {
        return pExecutor->mActionKeeper->isRegisteredDemoActionAppear(pActor);
    }

    bool isRegisteredDemoActionFunctor(const DemoExecutor* pExecutor, const LiveActor* pActor) {
        return pExecutor->mActionKeeper->isRegisteredDemoActionFunctor(pActor);
    }

    bool isRegisteredDemoActionNerve(const DemoExecutor* pExecutor, const LiveActor* pActor) {
        return pExecutor != nullptr && pExecutor->mActionKeeper->isRegisteredDemoActionNerve(pActor);
    }

    // A demo no sheet has (a null executor) takes nothing from these.
    bool tryCreateDemoTalkAnimCtrlForActor(DemoExecutor* pExecutor, LiveActor* pActor, const char* pFileBaseName, const char* pStartPartName) {
        if (pExecutor == nullptr) {
            return false;
        }

        DemoTalkAnimCtrl* pTalkAnimCtrl = new DemoTalkAnimCtrl(pActor, pExecutor->mName, pStartPartName);

        pTalkAnimCtrl->initForActor(pFileBaseName);
        pExecutor->addTalkAnimCtrl(pTalkAnimCtrl);

        return true;
    }

    // The scene's talk animation and its camera (pFileBaseName.canm) share the base name.
    bool tryCreateDemoTalkAnimCtrlForScene(DemoExecutor* pExecutor, LiveActor* pActor, const TableIter& rIter, const char* pFileBaseName,
                                           const char* pStartPartName, s32 cameraStartStep, s32 cameraEndStep) {
        if (pExecutor == nullptr) {
            return false;
        }

        DemoTalkAnimCtrl* pTalkAnimCtrl = new DemoTalkAnimCtrl(pActor, pExecutor->mName, pStartPartName);

        pTalkAnimCtrl->initForScene(pFileBaseName, pFileBaseName, rIter);
        pTalkAnimCtrl->mCameraStartStep = cameraStartStep;
        pTalkAnimCtrl->mCameraEndStep = cameraEndStep;
        pExecutor->addTalkAnimCtrl(pTalkAnimCtrl);

        return true;
    }

    void registerDemoTalkMessageCtrl(DemoExecutor* pExecutor, LiveActor* pActor, TalkMessageCtrl* pTalkCtrl) {
        if (pExecutor != nullptr) {
            pExecutor->addTalkMessageCtrl(pActor, pTalkCtrl);
        }
    }

    void setDemoTalkMessageCtrl(DemoExecutor* pExecutor, const LiveActor* pActor, TalkMessageCtrl* pTalkCtrl) {
        if (pExecutor != nullptr) {
            pExecutor->setTalkMessageCtrl(pActor, pTalkCtrl);
        }
    }

    void pauseTimeKeepDemo(DemoExecutor* pExecutor) {
        if (pExecutor != nullptr) {
            pExecutor->pause();
        }
    }

    void resumeTimeKeepDemo(DemoExecutor* pExecutor) {
        if (pExecutor != nullptr) {
            pExecutor->resume();
        }
    }

    bool isPauseTimeKeepDemo(const DemoExecutor* pExecutor) {
        return pExecutor != nullptr && pExecutor->mTimeKeeper->mIsPaused;
    }
};  // namespace DemoExecutorFunction
