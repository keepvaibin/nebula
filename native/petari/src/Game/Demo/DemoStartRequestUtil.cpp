#include "Game/Demo/DemoStartRequestUtil.hpp"
#include "Game/Demo/DemoDirector.hpp"
#include "Game/Demo/DemoExecutor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Demo/DemoStartRequestHolder.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util.hpp"

namespace {
    void setDemoStartInfoCommon(DemoStartInfo* pStartInfo, const char* pDemoName, const char* pPartName, MR::MovementControlType movementType,
                                DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                                DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        pStartInfo->mDemoName = pDemoName;
        pStartInfo->mDemoType = demoType;
        pStartInfo->mMovementType = movementType;
        pStartInfo->mFrameType = frameType;
        pStartInfo->mPointerType = pointerType;
        pStartInfo->mDeleteEffectType = deleteEffectType;
        pStartInfo->mDemoExecutor = DemoFunction::findDemoExecutor(pDemoName);
        pStartInfo->mPartName = pPartName;
    }

    // A request that cannot start yet: pStartInfo (its starter set) completed and queued under pRequester.
    template < typename Requester >
    void queueStartDemo(DemoStartInfo* pStartInfo, Requester* pRequester, const char* pDemoName, const char* pPartName,
                        MR::MovementControlType movementType, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                        DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        DemoStartRequestHolder* requestHolder = DemoFunction::getDemoDirector()->mStartRequestHolder;
        ::setDemoStartInfoCommon(pStartInfo, pDemoName, pPartName, movementType, demoType, frameType, pointerType, deleteEffectType);
        requestHolder->registerStartDemoInfo(*pStartInfo);
        requestHolder->pushRequest(pRequester, pDemoName);
    }

    void startStarPointerMode(DemoStartInfo::StarPointerType pointerType, NameObj* pDemoStarter) {
        switch (pointerType) {
        case DemoStartInfo::StarPointerType_DemoWithStarPointer:
            MR::startStarPointerModeDemoWithStarPointer(pDemoStarter);
            break;
        case DemoStartInfo::StarPointerType_DemoWithHandPointerFinger:
            MR::startStarPointerModeDemoWithHandPointerFinger(pDemoStarter);
            break;
        default:
            MR::startStarPointerModeDemo(pDemoStarter);
            break;
        }
    }
};  // namespace

namespace DemoStartRequestUtil {
    void startDemoSystem(NameObj* pObj, const char* pDemoName, MR::MovementControlType movementType, DemoStartInfo::DemoType demoType,
                         DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                         DemoStartInfo::DeleteEffectType deleteEffectType, const char* pPartName) {
        MR::getSceneNameObjMovementController()->requestStopSceneFor(movementType, pObj);
        bool isCinemaFrame = frameType == DemoStartInfo::CinemaFrameType_Frame;
        if (demoType == DemoStartInfo::DemoType_TimeKeep) {
            DemoFunction::getDemoDirector()->startDemoTimeKeep(pObj, pDemoName, movementType, isCinemaFrame, pPartName);
        } else {
            DemoFunction::getDemoDirector()->startDemoProgrammable(pObj, pDemoName, isCinemaFrame, movementType);
        }

        MR::deactivateDefaultGameLayout();

        if (isCinemaFrame) {
            MR::tryScreenToFrameCinemaFrame();
        }

        ::startStarPointerMode(pointerType, pObj);

        MR::requestMovementOn(pObj);
        MR::pauseOffCameraDirector();
        MR::pauseOffLensFlare();

        if (deleteEffectType == DemoStartInfo::DeleteEffectType_OneTimeEmitter) {
            MR::Effect::forceDeleteAllOneTimeEmitter();
        }

        if (MR::isMarioRemoteDemo(movementType)) {
            MarioAccess::readyRemoteDemo();
        }

        MR::sendMsgToAllLiveActor(ACTMES_START_DEMO, nullptr);
    }

    void startDemoSystem(LiveActor* pActor, const char* pDemoName, MR::MovementControlType movementType, DemoStartInfo::DemoType demoType,
                         DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                         DemoStartInfo::DeleteEffectType deleteEffectType, const char* pPartName) {
        startDemoSystem(static_cast< NameObj* >(pActor), pDemoName, movementType, demoType, frameType, pointerType, deleteEffectType, pPartName);
        MR::requestMovementOn(pActor);
    }

    void startDemoSystem(LayoutActor* pActor, const char* pDemoName, MR::MovementControlType movementType, DemoStartInfo::DemoType demoType,
                         DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                         DemoStartInfo::DeleteEffectType deleteEffectType, const char* pPartName) {
        startDemoSystem(static_cast< NameObj* >(pActor), pDemoName, movementType, demoType, frameType, pointerType, deleteEffectType, pPartName);
        MR::requestMovementOn(pActor);
    }

    // Each request starts the demo now when one can start, else queues it; the starter takes pDemoNerve now or when it starts,
    // and pNoDemoNerve while it waits.
    bool requestStartDemo(LiveActor* pActor, const char* pDemoName, const Nerve* pDemoNerve, const Nerve* pNoDemoNerve,
                          MR::MovementControlType movementType, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                          DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pActor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, movementType, demoType, frameType, pointerType, deleteEffectType, nullptr);
            if (MR::isRegisteredEffect(pActor, nullptr)) {
                MR::pauseOffEffectAll(pActor);
            }

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pActor->setNerve(pNoDemoNerve);
        }

        DemoStartInfo startInfo;
        startInfo.mActor = pActor;
        startInfo.mDemoNerve = pDemoNerve;
        ::queueStartDemo(&startInfo, pActor, pDemoName, nullptr, movementType, demoType, frameType, pointerType, deleteEffectType);

        return false;
    }

    bool requestStartDemo(LayoutActor* pActor, const char* pDemoName, const Nerve* pDemoNerve, const Nerve* pNoDemoNerve,
                          MR::MovementControlType movementType, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                          DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pActor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, movementType, demoType, frameType, pointerType, deleteEffectType, nullptr);
            if (MR::isRegisteredEffect(pActor, nullptr)) {
                MR::pauseOffEffectAll(pActor);
            }

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pActor->setNerve(pNoDemoNerve);
        }

        DemoStartInfo startInfo;
        startInfo.mLayout = pActor;
        startInfo.mDemoNerve = pDemoNerve;
        ::queueStartDemo(&startInfo, pActor, pDemoName, nullptr, movementType, demoType, frameType, pointerType, deleteEffectType);

        return false;
    }

    bool requestStartDemo(NerveExecutor* pExecutor, LiveActor* pActor, const char* pDemoName, const Nerve* pDemoNerve, const Nerve* pNoDemoNerve,
                          MR::MovementControlType movementType, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                          DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pExecutor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, movementType, demoType, frameType, pointerType, deleteEffectType, nullptr);

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pExecutor->setNerve(pNoDemoNerve);
        }

        DemoStartInfo startInfo;
        startInfo.mNerveExecutor = pExecutor;
        startInfo.mDemoNerve = pDemoNerve;
        startInfo.mNerveExecutorHost = pActor;
        ::queueStartDemo(&startInfo, pExecutor, pDemoName, nullptr, movementType, demoType, frameType, pointerType, deleteEffectType);

        return false;
    }

    bool requestStartTimeKeepDemo(LiveActor* pActor, const char* pDemoName, const char* pPartName, const Nerve* pDemoNerve,
                                  const Nerve* pNoDemoNerve, MR::MovementControlType movementType, DemoStartInfo::DemoType demoType,
                                  DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                                  DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pActor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, movementType, demoType, frameType, pointerType, deleteEffectType, pPartName);

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pActor->setNerve(pNoDemoNerve);
        }

        DemoStartInfo startInfo;
        startInfo.mActor = pActor;
        startInfo.mDemoNerve = pDemoNerve;
        ::queueStartDemo(&startInfo, pActor, pDemoName, pPartName, movementType, demoType, frameType, pointerType, deleteEffectType);

        return false;
    }

    bool requestStartTimeKeepDemo(NerveExecutor* pExecutor, LiveActor* pActor, const char* pDemoName, const char* pPartName,
                                  const Nerve* pDemoNerve, const Nerve* pNoDemoNerve, MR::MovementControlType movementType,
                                  DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                                  DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pExecutor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, movementType, demoType, frameType, pointerType, deleteEffectType, pPartName);

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pExecutor->setNerve(pNoDemoNerve);
        }

        DemoStartInfo startInfo;
        startInfo.mNerveExecutor = pExecutor;
        startInfo.mDemoNerve = pDemoNerve;
        startInfo.mNerveExecutorHost = pActor;
        ::queueStartDemo(&startInfo, pExecutor, pDemoName, pPartName, movementType, demoType, frameType, pointerType, deleteEffectType);

        return false;
    }

    bool requestStartTimeKeepDemo(NameObj* pObj, const char* pDemoName, const char* pPartName, MR::MovementControlType movementType,
                                  DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                                  DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            startDemoSystem(pObj, pDemoName, movementType, demoType, frameType, pointerType, deleteEffectType, pPartName);

            return true;
        }

        DemoStartInfo startInfo;
        startInfo.mNameObj = pObj;
        ::queueStartDemo(&startInfo, pObj, pDemoName, pPartName, movementType, demoType, frameType, pointerType, deleteEffectType);

        return false;
    }

    // Starts a queued request.
    void startDemo(const DemoStartInfo* pStartInfo) {
        NameObj* demoStarter = getDemoStarter(*pStartInfo);
        MR::getSceneNameObjMovementController()->requestStopSceneFor(pStartInfo->mMovementType, demoStarter);
        MR::deactivateDefaultGameLayout();
        if (pStartInfo->mFrameType == DemoStartInfo::CinemaFrameType_Frame) {
            MR::tryScreenToFrameCinemaFrame();
        }

        ::startStarPointerMode(pStartInfo->mPointerType, demoStarter);

        requestMovementOn(pStartInfo);

        if (pStartInfo->mDemoNerve != nullptr) {
            setNerveToStarter(pStartInfo);
        }

        MR::pauseOffCameraDirector();
        MR::pauseOffLensFlare();

        if (pStartInfo->mDeleteEffectType == DemoStartInfo::DeleteEffectType_OneTimeEmitter) {
            MR::Effect::forceDeleteAllOneTimeEmitter();
        }

        if (pStartInfo->mActor != nullptr && MR::isRegisteredEffect(pStartInfo->mActor, nullptr)) {
            MR::pauseOffEffectAll(pStartInfo->mActor);
        }

        if (pStartInfo->mLayout != nullptr && MR::isRegisteredEffect(pStartInfo->mLayout, nullptr)) {
            MR::pauseOffEffectAll(pStartInfo->mLayout);
        }

        if (MR::isMarioRemoteDemo(pStartInfo->mMovementType)) {
            MarioAccess::readyRemoteDemo();
        }

        if (pStartInfo->mDemoType == DemoStartInfo::DemoType_TimeKeep) {
            NameObj* obj = pStartInfo->mActor;
            if (obj == nullptr) {
                obj = DemoFunction::getDemoDirector()->mStartRequestHolder->mProxyObj;
            }

            DemoFunction::getDemoDirector()->startDemoExecutor(obj, pStartInfo->mDemoName, pStartInfo->mMovementType, nullptr);
        }

        startDemoExecutorIfExist(*pStartInfo);
        MR::sendMsgToAllLiveActor(ACTMES_START_DEMO, nullptr);
    }

    void startDemo(DemoStartRequestHolder* pStartRequestHolder) {
        if (pStartRequestHolder->getCurrentInfo() != nullptr) {
            startDemo(pStartRequestHolder->getCurrentInfo());
        }
    }

    bool popStartDemoRequest(DemoStartRequestHolder* pStartRequestHolder) {
        if (pStartRequestHolder->getCurrentInfo() == nullptr) {
            return false;
        }

        pStartRequestHolder->popRequest();
        return true;
    }

    bool isExistStartDemoRequest(const DemoStartRequestHolder* pStartRequestHolder) {
        return pStartRequestHolder->isExistRequest();
    }

    NameObj* getDemoStarter(const DemoStartInfo& rStartInfo) {
        if (rStartInfo.mActor != nullptr) {
            return rStartInfo.mActor;
        }

        if (rStartInfo.mLayout != nullptr) {
            return rStartInfo.mLayout;
        }

        if (rStartInfo.mNameObj != nullptr) {
            return rStartInfo.mNameObj;
        }

        if (rStartInfo.mNerveExecutor != nullptr) {
            return rStartInfo.mNerveExecutorHost;
        }

        return nullptr;
    }

    void setNerveToStarter(const DemoStartInfo* pStartInfo) {
        if (pStartInfo->mActor != nullptr) {
            pStartInfo->mActor->setNerve(pStartInfo->mDemoNerve);
            return;
        }

        if (pStartInfo->mLayout != nullptr) {
            pStartInfo->mLayout->setNerve(pStartInfo->mDemoNerve);
            return;
        }

        if (pStartInfo->mNerveExecutor != nullptr) {
            pStartInfo->mNerveExecutor->setNerve(pStartInfo->mDemoNerve);
        }
    }

    void requestMovementOn(const DemoStartInfo* pStartInfo) {
        if (pStartInfo->mActor != nullptr) {
            MR::requestMovementOn(pStartInfo->mActor);
        }

        if (pStartInfo->mLayout != nullptr) {
            MR::requestMovementOn(pStartInfo->mLayout);
        }

        if (pStartInfo->mNameObj != nullptr) {
            MR::requestMovementOn(pStartInfo->mNameObj);
        }

        if (pStartInfo->mNerveExecutorHost != nullptr) {
            MR::requestMovementOn(pStartInfo->mNerveExecutorHost);
        }
    }

    bool isEmpty(const DemoStartInfo* pStartInfo) {
        return pStartInfo->mActor == nullptr && pStartInfo->mLayout == nullptr && pStartInfo->mNerveExecutor == nullptr &&
               pStartInfo->mNameObj == nullptr;
    }

    bool startDemoExecutorIfExist(const DemoStartInfo& rStartInfo) {
        if (rStartInfo.mDemoExecutor == nullptr) {
            return false;
        }

        if (rStartInfo.mPartName != nullptr) {
            rStartInfo.mDemoExecutor->startPart(getDemoStarter(rStartInfo), rStartInfo.mDemoName, rStartInfo.mPartName, rStartInfo.mMovementType);
        } else {
            rStartInfo.mDemoExecutor->start(getDemoStarter(rStartInfo), rStartInfo.mDemoName, rStartInfo.mMovementType);
        }

        return true;
    }
};  // namespace DemoStartRequestUtil
