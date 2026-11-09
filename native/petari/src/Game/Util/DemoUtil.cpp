#include "Game/Util/DemoUtil.hpp"
#include "Game/Demo/DemoDirector.hpp"
#include "Game/Demo/DemoExecutor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Demo/DemoPlayerKeeper.hpp"
#include "Game/Demo/DemoStartRequestUtil.hpp"
#include "Game/NPC/TalkDirector.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/TalkUtil.hpp"

namespace {
    TalkDirector* getTalkDirector() {
        return MR::getSceneObj< TalkDirector >(SceneObj_TalkDirector);
    }
};  // namespace

namespace MR {
    bool tryRegisterDemoCast(LiveActor* pActor, const TableIter& rIter) {
        return DemoFunction::getDemoDirector()->registerDemoCast(pActor, rIter);
    }

    void registerDemoActionFunctor(const LiveActor* pActor, const MR::FunctorBase& rFunctor, const char* pName) {
        DemoFunction::registerDemoActionFunctorFunction(pActor, rFunctor, pName);
    }

    void registerDemoActionNerve(const LiveActor* pActor, const Nerve* pNerve, const char* pName) {
        DemoFunction::registerDemoActionNerveFunction(pActor, pNerve, pName);
    }

    bool tryRegisterDemoActionFunctor(const LiveActor* pActor, const MR::FunctorBase& rFunctor, const char* pName) {
        if (!DemoFunction::isRegisteredDemoActionFunctor(pActor)) {
            return false;
        }

        registerDemoActionFunctor(pActor, rFunctor, pName);

        return true;
    }

    bool tryRegisterDemoActionNerve(const LiveActor* pActor, const Nerve* pNerve, const char* pName) {
        if (!isRegisteredDemoActionNerve(pActor)) {
            return false;
        }

        registerDemoActionNerve(pActor, pNerve, pName);

        return true;
    }

    bool tryRegisterDemoCast(LiveActor* pActor, const char* pName, const TableIter& rIter) {
        return DemoFunction::getDemoDirector()->registerDemoCast(pActor, pName, rIter);
    }

    void registerDemoCast(LiveActor* pActor, const char* pName, const TableIter& rIter) {
        DemoFunction::getDemoDirector()->registerDemoCast(pActor, pName, rIter);
    }

    void initTalkAnimDemoCast(LiveActor* pActor, const TableIter& rIter, const char* pDemoName, const char* pFileBaseName) {
        DemoFunction::getDemoDirector()->registerDemoCast(pActor, pDemoName, rIter);
        DemoFunction::tryCreateDemoTalkAnimCtrlForActorDirect(pActor, pDemoName, pFileBaseName, nullptr);
    }

    void registerDemoActionFunctorDirect(const LiveActor* pActor, const MR::FunctorBase& rFunctor, const char* pDemoName, const char* pPartName) {
        DemoFunction::registerDemoActionFunctorFunction(pActor, rFunctor, pDemoName, pPartName);
    }

    bool tryRegisterDemoActionFunctorDirect(const LiveActor* pActor, const MR::FunctorBase& rFunctor, const char* pDemoName, const char* pPartName) {
        if (!DemoFunction::isRegisteredDemoActionFunctor(pActor, pDemoName)) {
            return false;
        }

        DemoFunction::registerDemoActionFunctorFunction(pActor, rFunctor, pDemoName, pPartName);

        return true;
    }

    bool tryStartDemoRegistered(LiveActor* pActor, const char* pName) {
        return DemoFunction::tryStartDemoRegistered(pActor, pName);
    }

    bool tryStartDemoRegisteredMarioPuppetable(LiveActor* pActor, const char* pName) {
        return DemoFunction::tryStartDemoRegisteredMarioPuppetable(pActor, pName);
    }

    void registerDemoSimpleCastAll(LiveActor* pActor) {
        DemoFunction::registerDemoSimpleCastAllFunction(pActor);
    }

    void registerDemoSimpleCastAll(LayoutActor* pLayoutActor) {
        DemoFunction::registerDemoSimpleCastAllFunction(pLayoutActor);
    }

    void registerDemoSimpleCastAll(NameObj* pObj) {
        DemoFunction::registerDemoSimpleCastAllFunction(pObj);
    }

    bool isDemoCast(const LiveActor* pActor, const char* pName) {
        DemoExecutor* executor = DemoFunction::findDemoExecutor(pActor);

        if (executor == nullptr) {
            return false;
        }

        if (pName != nullptr) {
            return DemoFunction::isRegisteredDemoCast(pActor, pName);
        }

        return DemoFunction::isDemoCast(executor, pActor);
    }

    bool isRegisteredDemoActionAppear(const LiveActor* pActor) {
        return DemoFunction::isRegisteredDemoActionAppear(pActor);
    }

    bool isRegisteredDemoActionNerve(const LiveActor* pActor) {
        return DemoFunction::isRegisteredDemoActionNerve(pActor);
    }

    bool tryStartDemo(LiveActor* pActor, const char* pName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoStartRequestUtil::startDemoSystem(pActor, pName, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_Frame,
                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep, nullptr);

        return true;
    }

    bool tryStartDemo(LayoutActor* pActor, const char* pName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoStartRequestUtil::startDemoSystem(pActor, pName, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_Frame,
                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep, nullptr);

        return true;
    }

    bool tryStartDemoWithoutCinemaFrame(LiveActor* pActor, const char* pName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoStartRequestUtil::startDemoSystem(pActor, pName, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_None,
                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep, nullptr);

        return true;
    }

    bool tryStartDemoWithoutCinemaFrameValidStarPointer(LiveActor* pActor, const char* pName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoStartRequestUtil::startDemoSystem(pActor, pName, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_None,
                                              DemoStartInfo::StarPointerType_DemoWithStarPointer, DemoStartInfo::DeleteEffectType_Keep, nullptr);

        return true;
    }

    bool tryStartDemoWithoutCinemaFrameValidHandPointerFinger(NameObj* pObj, const char* pName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoStartRequestUtil::startDemoSystem(pObj, pName, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_None,
                                              DemoStartInfo::StarPointerType_DemoWithHandPointerFinger, DemoStartInfo::DeleteEffectType_Keep, nullptr);

        return true;
    }

    bool tryStartDemoWithoutCinemaFrameValidHandPointerFinger(LayoutActor* pActor, const char* pName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoStartRequestUtil::startDemoSystem(pActor, pName, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_None,
                                              DemoStartInfo::StarPointerType_DemoWithHandPointerFinger, DemoStartInfo::DeleteEffectType_Keep, nullptr);

        return true;
    }

    bool tryStartDemoMarioPuppetable(LiveActor* pActor, const char* pName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoStartRequestUtil::startDemoSystem(pActor, pName, MR::MovementControlType_DemoMarioPuppetable, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_Frame,
                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep, nullptr);

        return true;
    }

    bool tryStartDemoMarioPuppetableWithoutCinemaFrame(LiveActor* pActor, const char* pName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoStartRequestUtil::startDemoSystem(pActor, pName, MR::MovementControlType_DemoMarioPuppetable, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_None,
                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep, nullptr);

        return true;
    }

    bool tryStartTimeKeepDemo(NameObj* pObj, const char* pName, const char* pPartName) {
        if (!canStartDemo()) {
            return false;
        }

        startTimeKeepDemo(pObj, pName, pPartName);

        return true;
    }

    bool tryStartTimeKeepDemoMarioPuppetable(NameObj* pObj, const char* pName, const char* pPartName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoExecutor* executor = DemoFunction::findDemoExecutor(pName);

        // A demo with no sheet in this stage does not start.
        if (executor == nullptr) {
            return false;
        }

        if (pPartName != nullptr) {
            executor->startDemoSystemPart(pPartName, MR::MovementControlType_DemoMarioPuppetable);
        } else {
            bool isExistPosName = executor->mPlayerKeeper != nullptr && executor->mPlayerKeeper->isExistPosName();

            DemoStartRequestUtil::startDemoSystem(pObj, pName, MR::MovementControlType_DemoMarioPuppetable, DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                  DemoStartInfo::StarPointerType_Demo, isExistPosName ? DemoStartInfo::DeleteEffectType_OneTimeEmitter : DemoStartInfo::DeleteEffectType_Keep, nullptr);
        }

        return true;
    }

    bool tryStartTimeKeepDemoMarioPuppetable(LiveActor* pActor, const char* pName, const char* pPartName) {
        if (!canStartDemo()) {
            return false;
        }

        DemoExecutor* executor = DemoFunction::findDemoExecutor(pName);

        if (executor == nullptr) {
            return false;
        }

        if (pPartName != nullptr) {
            executor->startDemoSystemPart(pPartName, MR::MovementControlType_DemoMarioPuppetable);
        } else {
            bool isExistPosName = executor->mPlayerKeeper != nullptr && executor->mPlayerKeeper->isExistPosName();

            DemoStartRequestUtil::startDemoSystem(pActor, pName, MR::MovementControlType_DemoMarioPuppetable, DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                  DemoStartInfo::StarPointerType_Demo, isExistPosName ? DemoStartInfo::DeleteEffectType_OneTimeEmitter : DemoStartInfo::DeleteEffectType_Keep, nullptr);
        }

        return true;
    }

    bool requestStartDemo(LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve, const Nerve* pCannotStartDemoNerve) {
        return DemoStartRequestUtil::requestStartDemo(pActor, pName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable,
                                                      DemoStartInfo::CinemaFrameType_Frame, DemoStartInfo::StarPointerType_Demo,
                                                      DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartDemoWithoutCinemaFrame(LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve,
                                            const Nerve* pCannotStartDemoNerve) {
        return DemoStartRequestUtil::requestStartDemo(pActor, pName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable,
                                                      DemoStartInfo::CinemaFrameType_None, DemoStartInfo::StarPointerType_Demo,
                                                      DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartDemoWithoutCinemaFrame(LayoutActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve,
                                            const Nerve* pCannotStartDemoNerve) {
        return DemoStartRequestUtil::requestStartDemo(pActor, pName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_Demo, DemoStartInfo::DemoType_Programmable,
                                                      DemoStartInfo::CinemaFrameType_None, DemoStartInfo::StarPointerType_Demo,
                                                      DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartDemoMarioPuppetable(LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve, const Nerve* pCannotStartDemoNerve) {
        return DemoStartRequestUtil::requestStartDemo(pActor, pName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_DemoMarioPuppetable, DemoStartInfo::DemoType_Programmable,
                                                      DemoStartInfo::CinemaFrameType_Frame, DemoStartInfo::StarPointerType_Demo,
                                                      DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartDemoMarioPuppetable(NerveExecutor* pExecutor, LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve,
                                         const Nerve* pCannotStartDemoNerve) {
        return DemoStartRequestUtil::requestStartDemo(pExecutor, pActor, pName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_DemoMarioPuppetable,
                                                      DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_Frame,
                                                      DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartDemoMarioPuppetableWithoutCinemaFrame(LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve,
                                                           const Nerve* pCannotStartDemoNerve) {
        return DemoStartRequestUtil::requestStartDemo(pActor, pName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_DemoMarioPuppetable, DemoStartInfo::DemoType_Programmable,
                                                      DemoStartInfo::CinemaFrameType_None, DemoStartInfo::StarPointerType_Demo,
                                                      DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartTimeKeepDemo(LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve, const Nerve* pCannotStartDemoNerve,
                                  const char* pPartName) {
        return DemoStartRequestUtil::requestStartTimeKeepDemo(pActor, pName, pPartName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_Demo,
                                                              DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartTimeKeepDemo(NameObj* pObj, const char* pName, const char* pPartName) {
        return DemoStartRequestUtil::requestStartTimeKeepDemo(pObj, pName, pPartName, MR::MovementControlType_Demo, DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartTimeKeepDemoMarioPuppetable(LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve,
                                                 const Nerve* pCannotStartDemoNerve, const char* pPartName) {
        return DemoStartRequestUtil::requestStartTimeKeepDemo(pActor, pName, pPartName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_DemoMarioPuppetable,
                                                              DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartTimeKeepDemoMarioPuppetable(NerveExecutor* pExecutor, LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve,
                                                 const Nerve* pCannotStartDemoNerve, const char* pPartName) {
        return DemoStartRequestUtil::requestStartTimeKeepDemo(pExecutor, pActor, pName, pPartName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_DemoMarioPuppetable,
                                                              DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartTimeKeepDemoMarioPuppetable(NameObj* pObj, const char* pName, const char* pPartName) {
        return DemoStartRequestUtil::requestStartTimeKeepDemo(pObj, pName, pPartName, MR::MovementControlType_DemoMarioPuppetable, DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartTimeKeepDemoWithoutCinemaFrame(LiveActor* pActor, const char* pName, const Nerve* pCanStartDemoNerve,
                                                    const Nerve* pCannotStartDemoNerve, const char* pPartName) {
        return DemoStartRequestUtil::requestStartTimeKeepDemo(pActor, pName, pPartName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_Demo,
                                                              DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_None,
                                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartDemoRegistered(LiveActor* pActor, const Nerve* pCanStartDemoNerve, const Nerve* pCannotStartDemoNerve, const char* pPartName) {
        DemoExecutor* executor = DemoFunction::findDemoExecutor(pActor);

        if (executor == nullptr) {
            return false;
        }

        return DemoStartRequestUtil::requestStartTimeKeepDemo(pActor, executor->mName, pPartName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_Demo,
                                                              DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    bool requestStartDemoRegisteredMarioPuppetable(LiveActor* pActor, const Nerve* pCanStartDemoNerve, const Nerve* pCannotStartDemoNerve,
                                                   const char* pPartName) {
        DemoExecutor* executor = DemoFunction::findDemoExecutor(pActor);

        if (executor == nullptr) {
            return false;
        }

        return DemoStartRequestUtil::requestStartTimeKeepDemo(pActor, executor->mName, pPartName, pCanStartDemoNerve, pCannotStartDemoNerve, MR::MovementControlType_DemoMarioPuppetable,
                                                              DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep);
    }

    void endDemo(NameObj* pObj, const char* pName) {
        DemoFunction::getDemoDirector()->endDemo(pObj, pName, false);
    }

    void endDemoWaitCameraInterpolating(NameObj* pObj, const char* pName) {
        DemoFunction::getDemoDirector()->endDemo(pObj, pName, true);
    }

    void initDemoSheetTalkAnim(LiveActor* pActor, const TableIter& rIter, const char* pDemoName, const char* pFileBaseName, TalkMessageCtrl* pTalkCtrl) {
        tryInitDemoSheetTalkAnim(pActor, rIter, pDemoName, pFileBaseName, pTalkCtrl);
    }

    void initDemoSheetTalkAnimWithMessageName(LiveActor* pActor, const TableIter& rIter, const char* pDemoName, const char* pFileBaseName,
                                              const char* pMessageName) {
        TalkMessageCtrl* talkCtrl = MR::createTalkCtrl(pActor, rIter, pMessageName, Vec3f(0.0f, 0.0f, 0.0f), nullptr);

        MR::onRootNodeAutomatic(talkCtrl);
        MR::tryInitDemoSheetTalkAnim(pActor, rIter, pDemoName, pFileBaseName, talkCtrl);
    }

    void initDemoSheetTalkAnimFunctor(LiveActor* pActor, const TableIter& rIter, const char* pDemoName, const char* pFileBaseName,
                                      TalkMessageCtrl* pTalkCtrl, const MR::FunctorBase& rFunctor) {
        if (tryInitDemoSheetTalkAnim(pActor, rIter, pDemoName, pFileBaseName, pTalkCtrl)) {
            DemoFunction::registerDemoActionFunctorFunction(pActor, rFunctor, pDemoName, nullptr);
        }
    }

    // pFileBaseName names the talk animation files and, without pTalkCtrl, the talk message too.
    bool tryInitDemoSheetTalkAnim(LiveActor* pActor, const TableIter& rIter, const char* pDemoName, const char* pFileBaseName,
                                  TalkMessageCtrl* pTalkCtrl) {
        if (tryRegisterDemoCast(pActor, pDemoName, rIter)) {
            if (pTalkCtrl == nullptr) {
                pTalkCtrl = MR::createTalkCtrl(pActor, rIter, pFileBaseName, Vec3f(0.0f, 0.0f, 0.0f), nullptr);

                MR::onRootNodeAutomatic(pTalkCtrl);
            }

            DemoFunction::registerDemoTalkMessageCtrlDirect(pActor, pTalkCtrl, pDemoName);
            DemoFunction::tryCreateDemoTalkAnimCtrlForSceneDirect(pActor, pDemoName, rIter, pFileBaseName, nullptr, 0, 0);

            return true;
        }

        return false;
    }

    void startTimeKeepDemo(NameObj* pObj, const char* pName, const char* pPartName) {
        if (pPartName != nullptr) {
            DemoExecutor* executor = DemoFunction::findDemoExecutor(pName);

            if (executor != nullptr) {
                executor->startDemoSystemPart(pPartName, MR::MovementControlType_Demo);
            }
        } else {
            DemoStartRequestUtil::startDemoSystem(pObj, pName, MR::MovementControlType_Demo, DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                  DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep, nullptr);
        }
    }

    void startTimeKeepDemoMarioPuppetable(NameObj* pObj, const char* pName, const char* pPartName) {
        DemoExecutor* executor = DemoFunction::findDemoExecutor(pName);

        if (executor == nullptr) {
            return;
        }

        if (pPartName != nullptr) {
            executor->startDemoSystemPart(pPartName, MR::MovementControlType_DemoMarioPuppetable);
        } else {
            bool isExistPosName = executor->mPlayerKeeper != nullptr && executor->mPlayerKeeper->isExistPosName();

            DemoStartRequestUtil::startDemoSystem(pObj, pName, MR::MovementControlType_DemoMarioPuppetable, DemoStartInfo::DemoType_TimeKeep, DemoStartInfo::CinemaFrameType_Frame,
                                                  DemoStartInfo::StarPointerType_Demo, isExistPosName ? DemoStartInfo::DeleteEffectType_OneTimeEmitter : DemoStartInfo::DeleteEffectType_Keep, nullptr);
        }
    }

    bool isDemoExist(const char* pName) {
        return DemoFunction::getDemoDirector()->isExistTimeKeepDemo(pName);
    }

    bool isDemoActive() {
        return DemoFunction::getDemoDirector()->mIsActive;
    }

    bool isDemoActive(const char* pName) {
        DemoExecutor* pExecutor = DemoFunction::getDemoDirector()->mExecutor;

        if (pExecutor != nullptr) {
            if (isName(pExecutor, pName)) {
                return true;
            }
        }

        const char* pDemoName = DemoFunction::getDemoDirector()->getCurrentDemoName();

        if (pDemoName == nullptr) {
            return false;
        }

        return isEqualString(pDemoName, pName);
    }

    bool canStartDemo() {
        if (isDemoActive()) {
            return false;
        }

        if (isPlayerDead()) {
            return false;
        }

        if (GameSceneFunction::isExecStageClearDemo()) {
            return false;
        }

        return !isPlayerConfrontDeath();
    }

    bool isTimeKeepDemoActive() {
        if (!DemoFunction::getDemoDirector()->mIsActive) {
            return false;
        }

        return DemoFunction::getDemoDirector()->mExecutor != nullptr;
    }

    bool isDemoActiveRegistered(const LiveActor* pActor) {
        DemoExecutor* executor = DemoFunction::findDemoExecutor(pActor);

        if (executor == nullptr) {
            return false;
        }

        return DemoFunction::getDemoDirector()->mExecutor == executor;
    }

    bool isDemoPartExist(const LiveActor* pActor, const char* pName) {
        DemoExecutor* executor = DemoFunction::findDemoExecutor(pActor);

        if (executor != nullptr) {
            return DemoFunction::isExistDemoPart(executor, pName);
        }

        return false;
    }

    bool isDemoLastStep() {
        return DemoFunction::isDemoLastPartLastStep();
    }

    bool isDemoPartActive(const char* pName) {
        return DemoFunction::isDemoPartActiveFunction(pName);
    }

    bool isDemoPartStep(const char* pName, s32 step) {
        if (!isDemoPartActive(pName)) {
            return false;
        }

        return getDemoPartStep(pName) == step;
    }

    bool isDemoPartFirstStep(const char* pName) {
        if (!isDemoPartActive(pName)) {
            return false;
        }

        return getDemoPartStep(pName) == 0;
    }

    bool isDemoPartLastStep(const char* pName) {
        if (!isDemoPartActive(pName)) {
            return false;
        }

        return getDemoPartStep(pName) == getDemoPartTotalStep(pName) - 1;
    }

    bool isDemoPartLessEqualStep(const char* pName, s32 step) {
        if (!isDemoPartActive(pName)) {
            return false;
        }

        return getDemoPartStep(pName) <= step;
    }

    bool isDemoPartGreaterStep(const char* pName, s32 step) {
        if (!isDemoPartActive(pName)) {
            return false;
        }

        return getDemoPartStep(pName) > step;
    }

    s32 getDemoPartTotalStep(const char* pName) {
        return DemoFunction::getDemoPartTotalStepFunction(pName);
    }

    f32 calcDemoPartStepRate(const char* pName) {
        const s32 total = getDemoPartTotalStep(pName);

        return total > 0 ? static_cast< f32 >(getDemoPartStep(pName)) / total : 0.0f;
    }

    s32 getDemoPartStep(const char* pName) {
        return DemoFunction::getDemoPartStepFunction(pName);
    }

    void pauseTimeKeepDemo(LiveActor* pActor) {
        DemoFunction::pauseTimeKeepDemo(pActor);
    }

    void resumeTimeKeepDemo(LiveActor* pActor) {
        DemoFunction::resumeTimeKeepDemo(pActor);
    }

    bool isPowerStarGetDemoActive() {
        return GameSceneFunction::isExecStageClearDemo();
    }

    const char* getCurrentDemoPartNameMain(const char* pName) {
        return DemoFunction::getCurrentDemoPartNameMain(pName);
    }

    void startTalkingSequenceWithoutCinemaFrame(NameObj* pObj) {
        DemoStartRequestUtil::startDemoSystem(pObj, "会話", MR::MovementControlType_StopEnemy, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_None,
                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep, nullptr);
    }

    void endTalkingSequence(NameObj* pObj) {
        endDemo(pObj, "会話");
    }

    bool isSystemTalking() {
        if (!isExistSceneObj(SceneObj_TalkDirector)) {
            return false;
        }

        return ::getTalkDirector()->isSystemTalking();
    }

    bool isNormalTalking() {
        if (!isExistSceneObj(SceneObj_TalkDirector)) {
            return false;
        }

        return ::getTalkDirector()->isNormalTalking();
    }

    LiveActor* getTalkingActor() {
        if (!isExistSceneObj(SceneObj_TalkDirector)) {
            return nullptr;
        }

        return ::getTalkDirector()->getTalkingActor();
    }

    bool isDemoPartTalk(const char* pName) {
        return DemoFunction::isDemoPartTalk(pName);
    }

    void startTalkingSequence(NameObj* pObj) {
        DemoStartRequestUtil::startDemoSystem(pObj, "会話", MR::MovementControlType_StopEnemy, DemoStartInfo::DemoType_Programmable, DemoStartInfo::CinemaFrameType_Frame,
                                              DemoStartInfo::StarPointerType_Demo, DemoStartInfo::DeleteEffectType_Keep, nullptr);
    }
};  // namespace MR
