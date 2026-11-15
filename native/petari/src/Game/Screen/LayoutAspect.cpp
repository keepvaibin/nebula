#include "Game/Screen/LayoutAspect.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>
#include <nebula_game_api.h>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Pane.hpp>

namespace {
    using namespace Ui;

    // How a pane follows a canvas wider or taller than the authored 608x456 (Game/Ui/Anchor.hpp).
    enum {
        FitLeft = AnchorLeft,
        FitRight = AnchorRight,
        FitTop = AnchorTop,
        FitBottom = AnchorBottom,
        FitWidth = AnchorWidth,
        FitHeight = AnchorHeight,
        FitCover = AnchorCover,
        FitFrame = AnchorFrame,
        FitExtendUp = AnchorExtendUp,
        FitExtendDown = AnchorExtendDown,
        FitSlide = AnchorSlide,
        FitFlipSlide = AnchorFlipSlide,
    };

    struct FitRule {
        const char* mLayout;
        const char* mPane;
        u16 mFit;
        f32 mWidth = 0.0f;
        f32 mHeight = 0.0f;
    };

    // From the authored pane trees: location-adjusted panes in the outer part of the canvas follow their edge,
    // full-canvas fills span it, menus keep their centred composition. Layouts the game positions every frame
    // (balloons, map icons, pointers) are absent.
    const FitRule cFitRules[] = {
        {"AllStarList", "PicBG", FitWidth | FitHeight},
        {"AllStarList", "PicBelt1", FitWidth},
        {"AllStarList", "PicBelt2", FitWidth},
        {"AllStarList", "PicBelt3", FitWidth},
        {"AllStarList", "PicBelt4Base", FitWidth},
        {"AllStarList", "PicTitleBase", FitWidth | FitExtendUp},
        {"AllStarList", "PicButtonbase", FitWidth | FitExtendDown},
        {"BackButton", "Back", FitLeft | FitBottom},
        {"BallGuidance", "BallGuidance", FitRight},  // PlayerActionGuidance's name for it
        {"BallGuidanceReplace", "BallGuidance", FitRight},
        {"BatteryInfo", "BatteryInfo", FitLeft},
        {"BrosButton", "BrosButton", FitRight},
        {"CameraInfo", "CameraInfo", FitRight},
        {"CameraInfo", "Cross", FitRight},
        {"CoinCounter", "CoinCounter", FitRight | FitBottom},
        {"CometScreenFilter", "PicRedComet", FitWidth | FitHeight},
        {"CometScreenFilter", "PicBlueComet", FitWidth | FitHeight},
        {"CometScreenFilter", "PicYellowComet", FitWidth | FitHeight},
        {"CometScreenFilter", "PicPurpleComet", FitWidth | FitHeight},
        {"CometScreenFilter", "PicWhiteComet", FitWidth | FitHeight},
        {"FileSelect", "StartButton", FitRight | FitBottom},
        {"FileSelect", "CopyButton", FitTop},
        {"FileSelect", "MiiButton", FitTop},
        {"FileSelect", "DeleteButton", FitTop},
        {"FileInfo", "FileInfo", FitTop},
        {"FileSelect", "P2ManualButton", FitRight},
        {"GalaxyDetail", "BG", FitWidth | FitHeight},
        {"GalaxyInfo", "InfoWindowUp", FitRight | FitTop},
        {"GalaxySelect", "Star", FitLeft | FitTop},
        {"GalaxySelect", "StartButton", FitRight | FitBottom},
        {"GameOver", "Color", FitWidth | FitHeight},
        {"HitPointMeter", "MoveMeter", FitRight | FitTop},
        {"LuigiLetter", "BG", FitWidth | FitHeight},
        {"MapButton", "MapList", FitRight | FitBottom},
        {"MapGalaxyBg", "PicSmoke", FitCover},
        {"MapGalaxyBg", "PicStar", FitCover},
        {"MapGalaxyBg", "PicGrid", FitCover},
        {"MapGrandGalaxy", "Counter", FitLeft | FitTop},
        {"MapTitle", "PicTitle", FitWidth | FitExtendUp},
        {"MapTitle", "TextTitle", FitLeft | FitTop},
        {"MapTitle", "Guidance", FitRight | FitTop},
        {"MiiSelect", "BG", FitWidth | FitHeight},
        {"MiiSelect", "MiiWindow", FitSlide, 660.0f},
        {"MiiSelect", "Text", FitSlide, 660.0f},
        {"MiiSelect", "MiiPosition", FitSlide, 660.0f},
        {"MiiSelect", "Arrow", FitSlide, 660.0f},
        {"MiiSelect", "MiiGroupA", FitSlide, NebulaFrameClock::cLayoutWidth},
        {"MiiSelect", "MiiGroupB", FitSlide, NebulaFrameClock::cLayoutWidth},
        // Nebula's achievement pop-up (NebulaAchievementToast) in the bottom-right corner.
        {"NebulaAchievementToast", "InfoWindowCenter", FitRight | FitBottom},
        // The file select's Quit and Settings (NebulaFileSelectButton): its Start button in the top-left
        // and top-right corners, sliding in from above as the file select's top buttons do.
        {"NebulaQuitButton", "StartButton", FitLeft | FitTop},
        {"NebulaQuitButton", "SButton", FitFlipSlide},
        {"NebulaSettingsButton", "StartButton", FitRight | FitTop},
        {"NebulaSettingsButton", "SButton", FitFlipSlide},
        {"P2Manual", "BG", FitWidth | FitHeight},
        {"PauseMenu", "BG", FitWidth | FitHeight},
        {"PauseMenu", "PicStagebase", FitWidth | FitExtendUp},
        {"PauseMenu", "PicStagebase1", FitWidth | FitExtendDown},
        // The pause menu's counters sit where the HUD's do, so they follow the same edges.
        {"PauseMenu", "Star", FitLeft},
        {"PauseMenu", "Mario", FitLeft},
        {"PauseMenu", "Coin", FitRight},
        {"PauseMenu", "Piece", FitRight},
        {"PeachLetter", "BG", FitWidth | FitHeight},
        {"PictureBook", "Base", FitWidth | FitHeight},
        {"PictureBook", "PicToneDown", FitWidth | FitHeight},
        {"PlayerLeft", "PlayerLeft", FitLeft | FitBottom},
        {"PointerGuidance", "WinPointFrame1", FitFrame, 720.0f, 378.0f},
        {"PointerGuidance", "WinPointFrame2", FitFrame, 720.0f, 365.0f},
        {"PointerGuidance", "WinGuide1", FitTop},
        {"PointerGuidance", "WinGuide2", FitTop},
        {"PointerGuidance", "Guide1", FitTop},
        {"PointerGuidance", "Guide2", FitTop},
        {"PointerGuidance", "NPointerGuide", FitTop},
        {"PrologueStarSteward", "FrameUp", FitWidth | FitExtendUp},
        {"PrologueStarSteward", "FrameDown", FitWidth | FitExtendDown},
        {"PrologueStarSteward", "FadeOut", FitWidth | FitHeight},
        {"PurpleCoinCounter", "PCoinCounter", FitLeft | FitBottom},
        {"RetryButton", "BG", FitWidth | FitHeight},
        {"ScenarioSelect", "BG", FitWidth | FitHeight},
        {"ScenarioSelect", "FrameU", FitWidth | FitExtendUp},
        {"ScenarioSelect", "FrameD", FitWidth | FitExtendDown},
        {"SpinGuidance", "SpinGuidance", FitRight},  // PlayerActionGuidance's name for it
        {"SpinGuidanceReplace", "SpinGuidance", FitRight},
        {"StaffRollPicture", "PicBG", FitWidth | FitHeight},
        {"StaffRollTitle", "PicFlash", FitWidth | FitHeight},
        {"StarCounter", "StarCounter", FitLeft | FitTop},
        {"StarPieceCounter", "StarPieceCounter", FitRight | FitBottom},
        {"StarPieceCounter", "TSPiece", FitBottom},
        {"StarPieceTargetCounter", "SPTargetCounter", FitRight},
        {"SuddenDeathMeter", "MoveMeter", FitRight | FitTop},
        {"SysInfoWindow", "BG", FitWidth | FitHeight},
        {"SysInfoWindowMini", "BG", FitWidth | FitHeight},
        {"TiltGuidance", "TiltGuidance", FitRight},  // the game's name for it
        {"TiltGuidanceReplace", "TiltGuidance", FitRight},
    };

    bool isNamed(const Pane* pPane, const char* pName) {
        return MR::isEqualString(pPane->mName, pName);
    }

    void add(LayoutManager* pManager, Pane* pPane, u32 fit, f32 width, f32 height) {
        if (pPane == nullptr) {
            return;
        }
        Anchor anchor;
        anchor.mpPane = pPane;
        anchor.mFit = fit;
        anchor.mWidth = width;
        anchor.mHeight = height;
        pManager->mAnchors.push_back(anchor);
    }

    // In the order they apply: the cinema bars, the backings, then the layout's rules in table order (an offset
    // moves the screen matrices the later ones read).
    void resolve(LayoutManager* pManager) {
        pManager->mAnchors.clear();
        pManager->mAnchorsResolved = true;
        Pane* pRoot = pManager->mLayout->mpRootPane;
        if (pRoot == nullptr) {
            return;
        }
        // The owning layout and exact names identify each backing: generic Base and PicBG panes elsewhere stay.
        if (isNamed(pRoot, "RootPane")) {
            bool title = false;
            for (Pane* pChild : pRoot->GetChildList()) {
                title |= isNamed(pChild, "SMGTitleLogo");
            }
            for (Pane* pPane : pRoot->GetChildList()) {
                if (isNamed(pPane, "CinemaFrame")) {
                    for (Pane* pBar : pPane->GetChildList()) {
                        if (isNamed(pBar, "FrameU")) {
                            add(pManager, pBar, AnchorCinemaUpper, 0.0f, 0.0f);
                        } else if (isNamed(pBar, "FrameD")) {
                            add(pManager, pBar, AnchorCinemaLower, 0.0f, 0.0f);
                        }
                    }
                }
            }
            for (Pane* pPane : pRoot->GetChildList()) {
                if (title && isNamed(pPane, "PicFlash")) {
                    add(pManager, pPane, AnchorFill, 0.0f, 0.0f);
                } else if (isNamed(pPane, "PrologueDemo")) {
                    for (Pane* pBacking : pPane->GetChildList()) {
                        if (isNamed(pBacking, "Base") || isNamed(pBacking, "Fade")) {
                            add(pManager, pBacking, AnchorFill, 0.0f, 0.0f);
                        }
                    }
                }
            }
        }
        for (u32 i = 0; pManager->mLayoutName != nullptr && i < sizeof(cFitRules) / sizeof(cFitRules[0]); i++) {
            const FitRule& rRule = cFitRules[i];
            if (MR::isEqualString(rRule.mLayout, pManager->mLayoutName)) {
                add(pManager, pRoot->FindPaneByName(rRule.mPane, true), rRule.mFit, rRule.mWidth, rRule.mHeight);
            }
        }
        if (pManager->mAnchors.size() != 0) {
            pManager->mLayout->SetAnchors(pManager->mAnchors.begin(), static_cast< u32 >(pManager->mAnchors.size()));
        }
    }

    // The canvas setupDrawForLayout projects; 608x456 when nothing changes.
    Canvas getCanvas() {
        Canvas canvas = {NebulaFrameClock::cLayoutWidth, NebulaFrameClock::cScreenHeight, 0.0f, 0.0f, 1.0f, 1.0f, false};
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            canvas.mWide = true;
            canvas.mWidth = MR::getDisplay().layout_width;
            canvas.mHeight = NebulaFrameClock::cScreenHeight * MR::getDisplay().vertical_fit;
            canvas.mHorizontalDelta = (canvas.mWidth - NebulaFrameClock::cLayoutWidth) * 0.5f;
            canvas.mVerticalDelta = (canvas.mHeight - NebulaFrameClock::cScreenHeight) * 0.5f;
        }
        canvas.mScaleX = canvas.mWidth / NebulaFrameClock::cLayoutWidth;
        canvas.mScaleY = canvas.mHeight / NebulaFrameClock::cScreenHeight;
        return canvas;
    }
};  // namespace

namespace LayoutAspect {
    void prepare(LayoutManager* pManager) {
        if (!pManager->mAnchorsResolved) {
            resolve(pManager);
        }
        if (pManager->mAnchors.size() != 0) {
            pManager->mLayout->SetCanvas(getCanvas());
        }
    }
};  // namespace LayoutAspect
