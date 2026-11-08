#include "Game/Util/SystemUtil.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/NameObj/NameObj.hpp"  // complete for NameObjMethod's ABI size
#include "Game/System/AudSystemWrapper.hpp"
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
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/ResFont.hpp>

namespace MR {
    GameSystemObjHolder* getGameSystemObjHolder() {
        return SingletonHolder< GameSystem >::get()->mObjHolder;
    }
};  // namespace MR

namespace {
    GameDataTemporaryInGalaxy* getGameDataTemporaryInGalaxy() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameDataTemporaryInGalaxy;
    }

    NameObjHolder* getSceneNameObjHolder() {
        return SingletonHolder< GameSystem >::get()->mSceneController->mObjHolder;
    }
};  // namespace

namespace MR {
    Ui::Font* getFontOnCurrentLanguage() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->getMessageFont();
    }

    Ui::Font* getPictureFontNW4R() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mPictureFont;
    }

    Ui::Font* getMenuFontNW4R() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mMenuFont;
    }

    Ui::Font* getNumberFontNW4R() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mNumberFont;
    }

    Ui::Font* getCinemaFontNW4R() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mCinemaFont;
    }

    ParticleResourceHolder* getParticleResourceHolder() {
        return getGameSystemObjHolder()->mParticleResHolder;
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
        getGameSystemObjHolder()->mRandom.mSeed = getHashCode(getCurrentStageName());
    }

    void holdSceneInitialize() {
        SingletonHolder< GameSystem >::get()->mSceneController->holdSceneInitialize();
    }

    bool tryResumeSceneInitialize() {
        return SingletonHolder< GameSystem >::get()->mSceneController->resumeSceneInitialize();
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
    PlacementId* getPlayerRestartIdInfo() {
        return ::getGameDataTemporaryInGalaxy()->mPlayerRestartIdInfo;
    }

    void setPlayerRestartIdInfo(const PlacementId& rInfo) {
        ::getGameDataTemporaryInGalaxy()->setPlayerRestartIdInfo(rInfo);
    }
};  // namespace MR
