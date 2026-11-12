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
#include "Game/System/NebulaSystemSettings.hpp"
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

    u32 sFixedRandomSeed = 0;  // MR::setFixedRandomSeed's; 0: the stage's name seeds
};  // namespace

namespace MR {
    Ui::Font* getFontOnCurrentLanguage() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->getMessageFont();
    }

    Ui::Font* getPictureFont() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mPictureFont;
    }

    Ui::Font* getMenuFont() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mMenuFont;
    }

    Ui::Font* getNumberFont() {
        return SingletonHolder< GameSystem >::get()->mFontHolder->mNumberFont;
    }

    Ui::Font* getCinemaFont() {
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
        getGameSystemObjHolder()->mRandom.mSeed = sFixedRandomSeed != 0 ? sFixedRandomSeed : getHashCode(getCurrentStageName());
    }

    void setFixedRandomSeed(u32 seed) {
        sFixedRandomSeed = seed;
        if (seed != 0) {
            getGameSystemObjHolder()->mRandom.mSeed = seed;
        }
    }

    u32 getFixedRandomSeed() {
        return sFixedRandomSeed;
    }

    void holdSceneInitialize() {
        SingletonHolder< GameSystem >::get()->mSceneController->holdSceneInitialize();
    }

    bool tryResumeSceneInitialize() {
        return SingletonHolder< GameSystem >::get()->mSceneController->resumeSceneInitialize();
    }

    bool isScreen16Per9() {
        return NebulaSystemSettings::isWidescreen();
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
    }

    void stopAllSound(u32 time) {
        getGameSystemObjHolder()->mAudioSystem->stopAllSound(time);
    }

    PlacementId* getPlayerRestartIdInfo() {
        return ::getGameDataTemporaryInGalaxy()->mPlayerRestartIdInfo;
    }

    void setPlayerRestartIdInfo(const PlacementId& rInfo) {
        ::getGameDataTemporaryInGalaxy()->setPlayerRestartIdInfo(rInfo);
    }
};  // namespace MR
