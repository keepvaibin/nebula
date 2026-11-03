#include "Game/Util/SystemUtil.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/NameObj/NameObj.hpp"  // complete for NameObjMethod's ABI size
#include "Game/System/AudSystemWrapper.hpp"
#include "Game/System/FunctionAsyncExecutor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/System/RenderMode.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <nw4r/lyt/layout.h>
#include <nw4r/ut/ResFont.h>

namespace MR {
    GameSystemObjHolder* getGameSystemObjHolder() {
        return SingletonHolder< GameSystem >::get()->mObjHolder;
    }
};  // namespace MR

namespace {
    FunctionAsyncExecutor* getFunctionAsyncExecutor() {
        return MR::getGameSystemObjHolder()->mFunctionAsyncExecutor;
    }

    GameDataTemporaryInGalaxy* getGameDataTemporaryInGalaxy() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameDataTemporaryInGalaxy;
    }

    NameObjHolder* getSceneNameObjHolder() {
        return SingletonHolder< GameSystem >::get()->mSceneController->mObjHolder;
    }
};  // namespace

namespace MR {
    nw4r::ut::Font* getFontOnCurrentLanguage() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->getMessageFont();
    }

    nw4r::ut::Font* getPictureFontNW4R() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mPictureFont;
    }

    nw4r::ut::Font* getMenuFontNW4R() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mMenuFont;
    }

    nw4r::ut::Font* getNumberFontNW4R() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mNumberFont;
    }

    nw4r::ut::Font* getCinemaFontNW4R() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mCinemaFont;
    }

    ParticleResourceHolder* getParticleResourceHolder() {
        return getGameSystemObjHolder()->mParticleResHolder;
    }

    void requestChangeArchivePlayer(bool isPlayerMario) {
        GameSystemFunction::requestChangeArchivePlayer(isPlayerMario);
    }

    void waitEndChangeArchivePlayer() {
        GameSystemFunction::waitEndChangeArchivePlayer();
    }

    void callMethodAllSceneNameObj(NameObjMethod pMethod) {
        ::getSceneNameObjHolder()->callMethodAllObj(pMethod);
    }

    void suspendAllSceneNameObj() {
        ::getSceneNameObjHolder()->suspendAllObj();
    }

    void resumeAllSceneNameObj() {
        ::getSceneNameObjHolder()->resumeAllObj();
    }

    void syncWithFlagsAllSceneNameObj() {
        ::getSceneNameObjHolder()->syncWithFlags();
    }

    void setRandomSeedFromStageName() {
        u32 seed = getHashCode(getCurrentStageName());

        getGameSystemObjHolder()->mRandom.mSeed = seed;
    }

    void clearFileLoaderRequestFileInfo(bool param1) {
        getGameSystemObjHolder()->clearRequestFileInfo(param1);
    }

    void startFunctionAsyncExecute(const MR::FunctorBase& rFunc, const char* pThreadName) {
        ::getFunctionAsyncExecutor()->start(rFunc, pThreadName);
    }

    bool startFunctionAsyncExecuteOnMainThread(const MR::FunctorBase& rFunc, const char* pThreadName) {
        return ::getFunctionAsyncExecutor()->startOnMainThread(rFunc, pThreadName);
    }

    void waitForEndFunctionAsyncExecute(const char* pThreadName) {
        ::getFunctionAsyncExecutor()->waitForEnd(pThreadName);
    }

    bool isEndFunctionAsyncExecute(const char* pThreadName) {
        return ::getFunctionAsyncExecutor()->isEnd(pThreadName);
    }

    bool tryEndFunctionAsyncExecute(const char* pThreadName) {
        if (isEndFunctionAsyncExecute(pThreadName)) {
            waitForEndFunctionAsyncExecute(pThreadName);

            return true;
        }

        return false;
    }

    namespace {
        GameEvent sSceneInitializeResume;
    }

    void waitSceneInitializeResume() {
        sSceneInitializeResume.wait();
    }

    bool tryResumeSceneInitialize() {
        if (!sSceneInitializeResume.isWaiting()) {
            return false;
        }

        sSceneInitializeResume.signal();

        return true;
    }

    bool isScreen16Per9() {
        return isAspectRatioFlag16Per9();
    }

    void initSceneMessage() {
        getGameSystemObjHolder()->mMessageHolder->initSceneData();
    }

    void destroySceneMessage() {
        getGameSystemObjHolder()->mMessageHolder->destroySceneData();
    }

    void resetSystemAndGameStatus() {
        GameDataFunction::resetAllGameData();
        GameSystemFunction::resetAllControllerRumble();
        resetGlobalTimer();
    }

    void stopAllSound(u32 param1) {
        getGameSystemObjHolder()->mAudioSystem->stopAllSound(param1);
    }

    // The output is 60 Hz: no PAL television to suggest PAL60 for.
    JMapIdInfo* getPlayerRestartIdInfo() {
        return ::getGameDataTemporaryInGalaxy()->mPlayerRestartIdInfo;
    }

    void setPlayerRestartIdInfo(const JMapIdInfo& rInfo) {
        ::getGameDataTemporaryInGalaxy()->setPlayerRestartIdInfo(rInfo);
    }
};  // namespace MR
