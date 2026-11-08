#pragma once


class GameSystemObjHolder;
class PlacementId;
class NameObj;
class ParticleResourceHolder;

namespace MR {
    class FunctorBase;
};  // namespace MR

namespace Ui {
    class Font;
}  // namespace Ui

namespace MR {
    GameSystemObjHolder* getGameSystemObjHolder();
    Ui::Font* getFontOnCurrentLanguage();
    Ui::Font* getPictureFontNW4R();
    Ui::Font* getMenuFontNW4R();
    Ui::Font* getNumberFontNW4R();
    Ui::Font* getCinemaFontNW4R();
    ParticleResourceHolder* getParticleResourceHolder();
    void callMethodAllSceneNameObj(void (NameObj::*)());
    void suspendAllSceneNameObj();
    void resumeAllSceneNameObj();
    void syncWithFlagsAllSceneNameObj();
    void setRandomSeedFromStageName();
    // Scene init waits here for the scenario select; false while it has not got here.
    void holdSceneInitialize();
    bool tryResumeSceneInitialize();
    bool isScreen16Per9();
    void initSceneMessage();
    void destroySceneMessage();
    void resetSystemAndGameStatus();
    void stopAllSound(u32);
    PlacementId* getPlayerRestartIdInfo();
    void setPlayerRestartIdInfo(const PlacementId&);
};  // namespace MR
