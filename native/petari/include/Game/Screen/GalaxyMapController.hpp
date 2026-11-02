#pragma once

#include "Game/Screen/LayoutActor.hpp"

class BackButton;
class GalaxyMap;
class GalaxyMapBackground;
class GalaxyMapSelectButton;
class JUTTexture;
class PowerStarList;
struct ResTIMG;
class WipeFade;

class GalaxyMapController : public LayoutActor {
public:
    enum Mode {
        Mode_AstroMap,
        Mode_GalaxyMap,
        Mode_PowerStarList,
        Mode_NewGalaxyDiscover,
        Mode_NewTicoGalaxyDiscover,
        Mode_NewDomeDiscover,
        Mode_TicoCometPray,
        Mode_ChallengeGalaxyDiscover,
    };

    /// @brief Creates a new `GalaxyMapController`.
    GalaxyMapController();

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();

    void drawForCapture();
    void startGalaxyMap();
    void startAstroMap();
    void startNewGalaxyDiscover();
    void startNewTicoGalaxyDiscover();
    void startNewDomeDiscover();
    void startTicoCometPray();
    void startChallengeGalaxyDiscover();
    void killAllLayout();
    bool tryPushBackButton();
    bool tryChangeMode();
    bool tryAppearGalaxyMap();
    bool tryDisplayAndKeyWait();
    void setNerveAccordingToNextModeFromMap();
    void setFaderParam(NebulaColor8, int);
    void capture();

    void exeReadyModeMap();
    void exeFadeinModeMap();
    void exeAppearGalaxyMap();
    void exeDisplayGalaxyMap();
    void exeAppearAstroMap();
    void exeDisplayAstroMap();
    void exeReadyPowerStarList();
    void exeAppearPowerStarList();
    void exeDisplayPowerStarList();
    void exeDisappearPowerStarList();
    void exeNewGalaxyDiscoverPhaseAstroMap();
    void exeNewGalaxyDiscoverPhaseAppearGalaxyMap();
    void exeNewGalaxyDiscoverPhaseDisplayGalaxyMap();
    void exeNewTicoGalaxyDiscoverPhaseAstroMap();
    void exeNewDomeDiscoverPhaseAstroMap();
    void exeTicoCometFadeoutWhite();
    void exeTicoCometWaitWhite();
    void exeTicoCometPray();
    void exeChallengeGalaxyDiscoverPhaseAstroMap();
    void exeFadeout();
    void exeReadyGame();

    GalaxyMap* mMap;
    GalaxyMapSelectButton* mMapSelectButton;
    GalaxyMapBackground* mMapBackground;
    PowerStarList* mPowerStarList;
    BackButton* mBackButton;
    WipeFade* mWipe;
    JUTTexture* _38;
    s32 mMode;
    s32 mWipeFrame;
};

namespace MR {
    void startGalaxyMapLayout();
    void startAstroMapLayout();
    void startAstroMapLayoutForNewGalaxyDiscover();
    void startAstroMapLayoutForNewTicoGalaxyDiscover();
    void startAstroMapLayoutForNewDomeDiscover();
    void startGalaxyMapLayoutForTicoComet();
    void startAstroMapLayoutForChallengeGalaxyDiscover();
    bool isActiveGalaxyMapLayout();
    const ResTIMG* getGalaxyMapResTIMG();
};  // namespace MR
