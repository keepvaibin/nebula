#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/Ui/DrawInfo.hpp"

class BackButton;
class GalaxyMap;
class GalaxyMapBackground;
class GalaxyMapSelectButton;
class GameTexture;
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

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;
    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void kill() override;

    void prepareCapture();
    void recordCapture() const;
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
    GameTexture* mCaptureTexture;
    s32 mMode;
    s32 mWipeFrame;
    // The capture (P-214): the step decides it and readies the layouts (prepareCapture, mCaptureDrawInfo); the next draw
    // records it into mCaptureTexture (recordCapture). mCaptureRecorded is the draw's own mark of what it recorded.
    Ui::DrawInfo mCaptureDrawInfo;
    bool mCaptureDue;
    u32 mCaptureAsked;
    mutable u32 mCaptureRecorded;
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
