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
    void setDemoStartInfoCommon(DemoStartInfo* pStartInfo, const char* pDemoName, const char* pChar2, s32 a4, DemoStartInfo::DemoType demoType,
                                DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                                DemoStartInfo::DeleteEffectType deleteEffectType);

};  // namespace

namespace DemoStartRequestUtil {
    void startDemoSystem(NameObj* pObj, const char* pDemoName, s32 a3, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                         DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType, const char* pChar2) {
        MR::getSceneNameObjMovementController()->requestStopSceneFor(MR::MovementControlType(a3), pObj);
        bool isCinemaFrame = frameType == DemoStartInfo::CinemaFrameType_0;
        if (demoType == DemoStartInfo::DemoType_TimeKeep) {
            DemoFunction::getDemoDirector()->startDemoTimeKeep(pObj, pDemoName, a3, isCinemaFrame, pChar2);
        } else {
            DemoFunction::getDemoDirector()->startDemoProgrammable(pObj, pDemoName, isCinemaFrame, a3);
        }

        MR::deactivateDefaultGameLayout();

        if (isCinemaFrame) {
            MR::tryScreenToFrameCinemaFrame();
        }

        switch (pointerType) {
        case DemoStartInfo::StarPointerType_1:
            MR::startStarPointerModeDemoWithStarPointer(pObj);
            break;
        case DemoStartInfo::StarPointerType_2:
            MR::startStarPointerModeDemoWithHandPointerFinger(pObj);
            break;
        default:
            MR::startStarPointerModeDemo(pObj);
            break;
        }

        MR::requestMovementOn(pObj);
        MR::pauseOffCameraDirector();
        MR::pauseOffLensFlare();

        if (deleteEffectType == DemoStartInfo::DeleteEffectType_1) {
            MR::Effect::forceDeleteAllOneTimeEmitter();
        }

        // inline?
        bool remoteDemo = false;
        if (MR::MovementControlType(a3) == MR::MovementControlType_2 || MR::MovementControlType(a3) == MR::MovementControlType_3) {
            remoteDemo = true;
        }

        if (remoteDemo) {
            MarioAccess::readyRemoteDemo();
        }

        MR::sendMsgToAllLiveActor(ACTMES_START_DEMO, nullptr);
    }

    void startDemoSystem(LiveActor* pActor, const char* pDemoName, s32 a3, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                         DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType, const char* pChar2) {
        startDemoSystem(static_cast< NameObj* >(pActor), pDemoName, a3, demoType, frameType, pointerType, deleteEffectType, pChar2);
        MR::requestMovementOn(pActor);
    }

    void startDemoSystem(LayoutActor* pActor, const char* pDemoName, s32 a3, DemoStartInfo::DemoType demoType,
                         DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                         DemoStartInfo::DeleteEffectType deleteEffectType, const char* pChar2) {
        startDemoSystem(static_cast< NameObj* >(pActor), pDemoName, a3, demoType, frameType, pointerType, deleteEffectType, pChar2);
        MR::requestMovementOn(pActor);
    }

    bool requestStartDemo(LiveActor* pActor, const char* pDemoName, const Nerve* pDemoNerve, const Nerve* pNoDemoNerve, s32 a5,
                          DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                          DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pActor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, a5, demoType, frameType, pointerType, deleteEffectType, nullptr);
            if (MR::isRegisteredEffect(pActor, nullptr)) {
                MR::pauseOffEffectAll(pActor);
            }

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pActor->setNerve(pNoDemoNerve);
        }

        DemoStartRequestHolder* requestHolder = DemoFunction::getDemoDirector()->mStartRequestHolder;

        DemoStartInfo startInfo;
        startInfo.mActor = pActor;
        startInfo.mDemoNerve = pDemoNerve;
        ::setDemoStartInfoCommon(&startInfo, pDemoName, nullptr, a5, demoType, frameType, pointerType, deleteEffectType);

        requestHolder->registerStartDemoInfo(startInfo);

        requestHolder->pushRequest(pActor, pDemoName);

        return false;
    }

    bool requestStartDemo(LayoutActor* pActor, const char* pDemoName, const Nerve* pDemoNerve, const Nerve* pNoDemoNerve, s32 a5,
                          DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                          DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pActor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, a5, demoType, frameType, pointerType, deleteEffectType, nullptr);
            if (MR::isRegisteredEffect(pActor, nullptr)) {
                MR::pauseOffEffectAll(pActor);
            }

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pActor->setNerve(pNoDemoNerve);
        }

        DemoStartRequestHolder* requestHolder = DemoFunction::getDemoDirector()->mStartRequestHolder;

        DemoStartInfo startInfo;
        startInfo.mLayout = pActor;
        startInfo.mDemoNerve = pDemoNerve;
        ::setDemoStartInfoCommon(&startInfo, pDemoName, nullptr, a5, demoType, frameType, pointerType, deleteEffectType);

        requestHolder->registerStartDemoInfo(startInfo);

        requestHolder->pushRequest(pActor, pDemoName);

        return false;
    }

    bool requestStartDemo(NerveExecutor* pExecutor, LiveActor* pActor, const char* pDemoName, const Nerve* pDemoNerve, const Nerve* pNoDemoNerve,
                          s32 a6, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                          DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pExecutor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, a6, demoType, frameType, pointerType, deleteEffectType, nullptr);

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pExecutor->setNerve(pNoDemoNerve);
        }

        DemoStartRequestHolder* requestHolder = DemoFunction::getDemoDirector()->mStartRequestHolder;

        DemoStartInfo startInfo;
        startInfo.mNerveExecutor = pExecutor;
        startInfo.mDemoNerve = pDemoNerve;
        startInfo.mNerveExecutorHost = pActor;
        ::setDemoStartInfoCommon(&startInfo, pDemoName, nullptr, a6, demoType, frameType, pointerType, deleteEffectType);

        requestHolder->registerStartDemoInfo(startInfo);

        requestHolder->pushRequest(pExecutor, pDemoName);

        return false;
    }

    bool requestStartTimeKeepDemo(LiveActor* pActor, const char* pDemoName, const char* pChar, const Nerve* pDemoNerve, const Nerve* pNoDemoNerve,
                                  s32 a5, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                                  DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pActor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, a5, demoType, frameType, pointerType, deleteEffectType, pChar);

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pActor->setNerve(pNoDemoNerve);
        }

        DemoStartRequestHolder* requestHolder = DemoFunction::getDemoDirector()->mStartRequestHolder;

        DemoStartInfo startInfo;
        startInfo.mActor = pActor;
        startInfo.mDemoNerve = pDemoNerve;
        ::setDemoStartInfoCommon(&startInfo, pDemoName, pChar, a5, demoType, frameType, pointerType, deleteEffectType);

        requestHolder->registerStartDemoInfo(startInfo);

        requestHolder->pushRequest(pActor, pDemoName);

        return false;
    }

    bool requestStartTimeKeepDemo(NerveExecutor* pExecutor, LiveActor* pActor, const char* pDemoName, const char* pChar, const Nerve* pDemoNerve,
                                  const Nerve* pNoDemoNerve, s32 a5, DemoStartInfo::DemoType demoType, DemoStartInfo::CinemaFrameType frameType,
                                  DemoStartInfo::StarPointerType pointerType, DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            if (pDemoNerve != nullptr) {
                pExecutor->setNerve(pDemoNerve);
            }

            startDemoSystem(pActor, pDemoName, a5, demoType, frameType, pointerType, deleteEffectType, pChar);

            return true;
        }

        if (pNoDemoNerve != nullptr) {
            pExecutor->setNerve(pNoDemoNerve);
        }

        DemoStartRequestHolder* requestHolder = DemoFunction::getDemoDirector()->mStartRequestHolder;

        DemoStartInfo startInfo;
        startInfo.mNerveExecutor = pExecutor;
        startInfo.mDemoNerve = pDemoNerve;
        startInfo.mNerveExecutorHost = pActor;
        ::setDemoStartInfoCommon(&startInfo, pDemoName, pChar, a5, demoType, frameType, pointerType, deleteEffectType);

        requestHolder->registerStartDemoInfo(startInfo);

        requestHolder->pushRequest(pExecutor, pDemoName);

        return false;
    }

    bool requestStartTimeKeepDemo(NameObj* pObj, const char* pDemoName, const char* pChar, s32 a5, DemoStartInfo::DemoType demoType,
                                  DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                                  DemoStartInfo::DeleteEffectType deleteEffectType) {
        if (MR::canStartDemo()) {
            startDemoSystem(pObj, pDemoName, a5, demoType, frameType, pointerType, deleteEffectType, pChar);

            return true;
        }

        DemoStartRequestHolder* requestHolder = DemoFunction::getDemoDirector()->mStartRequestHolder;

        DemoStartInfo startInfo;
        startInfo.mNameObj = pObj;
        ::setDemoStartInfoCommon(&startInfo, pDemoName, pChar, a5, demoType, frameType, pointerType, deleteEffectType);

        requestHolder->registerStartDemoInfo(startInfo);

        requestHolder->pushRequest(pObj, pDemoName);

        return false;
    }

    void startDemo(const DemoStartInfo* pStartInfo) {
        NameObj* demoStarter = getDemoStarter(*pStartInfo);
        MR::getSceneNameObjMovementController()->requestStopSceneFor(MR::MovementControlType(pStartInfo->mMovementType), demoStarter);
        MR::deactivateDefaultGameLayout();
        if (pStartInfo->mFrameType == DemoStartInfo::CinemaFrameType_0) {
            MR::tryScreenToFrameCinemaFrame();
        }

        switch (pStartInfo->mPointerType) {
        case DemoStartInfo::StarPointerType_1:
            MR::startStarPointerModeDemoWithStarPointer(demoStarter);
            break;
        case DemoStartInfo::StarPointerType_2:
            MR::startStarPointerModeDemoWithHandPointerFinger(demoStarter);
            break;
        default:
            MR::startStarPointerModeDemo(demoStarter);
            break;
        }

        requestMovementOn(pStartInfo);

        if (pStartInfo->mDemoNerve != nullptr) {
            setNerveToStarter(pStartInfo);
        }

        MR::pauseOffCameraDirector();
        MR::pauseOffLensFlare();

        if (pStartInfo->mDeleteEffectType == DemoStartInfo::DeleteEffectType_1) {
            MR::Effect::forceDeleteAllOneTimeEmitter();
        }

        if (pStartInfo->mActor != nullptr && MR::isRegisteredEffect(pStartInfo->mActor, nullptr)) {
            MR::pauseOffEffectAll(pStartInfo->mActor);
        }

        if (pStartInfo->mLayout != nullptr && MR::isRegisteredEffect(pStartInfo->mLayout, nullptr)) {
            MR::pauseOffEffectAll(pStartInfo->mLayout);
        }

        bool remoteDemo = MR::MovementControlType(pStartInfo->mMovementType) == MR::MovementControlType_2 ||
                          MR::MovementControlType(pStartInfo->mMovementType) == MR::MovementControlType_3;

        if (remoteDemo) {
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

};  // namespace DemoStartRequestUtil

namespace {
    inline void setDemoStartInfoCommon(DemoStartInfo* pStartInfo, const char* pDemoName, const char* pChar2, s32 a4, DemoStartInfo::DemoType demoType,
                                       DemoStartInfo::CinemaFrameType frameType, DemoStartInfo::StarPointerType pointerType,
                                       DemoStartInfo::DeleteEffectType deleteEffectType) {
        pStartInfo->mDemoName = pDemoName;
        pStartInfo->mDemoType = demoType;
        pStartInfo->mMovementType = a4;
        pStartInfo->mFrameType = frameType;
        pStartInfo->mPointerType = pointerType;
        pStartInfo->mDeleteEffectType = deleteEffectType;
        pStartInfo->mDemoExecutor = DemoFunction::findDemoExecutor(pDemoName);
        pStartInfo->mPartName = pChar2;
    }
};  // namespace

namespace DemoStartRequestUtil {
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
        if (pStartInfo->mActor != nullptr) {
            return false;
        }

        if (pStartInfo->mLayout != nullptr) {
            return false;
        }

        if (pStartInfo->mNerveExecutor != nullptr) {
            return false;
        }

        if (pStartInfo->mNameObj != nullptr) {
            return false;
        }

        return true;
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
