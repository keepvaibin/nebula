#ifdef TARGET_PC
#include "Game/Screen/LayoutAspect.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/LayoutPaneCtrl.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <cstring>
#include <nebula_game_api.h>
#include <revolution/os.h>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>

namespace {
    // How a pane follows a canvas wider or taller than the authored 608x456.
    enum {
        FitLeft = 1 << 0,  // the branch moves with the left edge
        FitRight = 1 << 1,
        FitTop = 1 << 2,
        FitBottom = 1 << 3,
        FitWidth = 1 << 4,       // the picture scales to the canvas width
        FitHeight = 1 << 5,      // the picture scales to the canvas height
        FitCover = 1 << 6,       // artwork scales uniformly until it covers the canvas
        FitFrame = 1 << 7,       // a window grows so its border keeps its distance from the edge
        FitExtendUp = 1 << 8,    // a bar grows upward to the canvas top
        FitExtendDown = 1 << 9,  // a bar grows downward to the canvas bottom
        FitSlide = 1 << 10,      // slides further in proportion; width = its 16:9 off-screen x
        FitFlipSlide = 1 << 11,  // the pane's animated vertical slide runs the other way, at every canvas
    };

    struct FitRule {
        const char* mLayout;
        const char* mPane;
        u16 mFit;
        f32 mWidth;
        f32 mHeight;
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
        {"MiiSelect", "MiiGroupA", FitSlide, 608.0f},
        {"MiiSelect", "MiiGroupB", FitSlide, 608.0f},
        // Nebula's achievement pop-up (NebulaAchievementToast) in the bottom-right corner.
        {"NebulaAchievementToast", "InfoWindowCenter", FitRight | FitBottom},
        // The file select's Quit to desktop (NebulaQuitButton): its Start button in the top-left corner, sliding in
        // from above as the file select's top buttons do.
        {"NebulaQuitButton", "StartButton", FitLeft | FitTop},
        {"NebulaQuitButton", "SButton", FitFlipSlide},
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

    struct Extent {
        f32 mWidth;
        f32 mHeight;
        f32 mHorizontalDelta;  // how much further each side edge lies than at 608
        f32 mVerticalDelta;
    };

    // The canvas setupDrawForNW4RLayout projects; the Wii's 608x456 when nothing changes.
    Extent getExtent() {
        Extent extent = {608.0f, 456.0f, 0.0f, 0.0f};
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            extent.mWidth = MR::getDisplay().canvas_width;
            extent.mHeight = 456.0f * MR::getDisplay().vertical_fit;
            extent.mHorizontalDelta = (extent.mWidth - 608.0f) * 0.5f;
            extent.mVerticalDelta = (extent.mHeight - 456.0f) * 0.5f;
        }
        return extent;
    }

    bool isNamed(const nw4r::lyt::Pane* pPane, const char* pName) {
        return strcmp(pPane->mName, pName) == 0;
    }

    // Pane::CalculateMtx skips an invisible pane and its branch, whose matrices are then an earlier frame's.
    bool isDrawn(const nw4r::lyt::Pane* pPane, const nw4r::lyt::Pane* pRoot) {
        for (s32 depth = 0; depth < 32 && pPane != nullptr; depth++) {
            if (!pPane->IsVisible()) {
                return false;
            }
            if (pPane == pRoot) {
                return true;
            }
            pPane = pPane->mpParent;
        }
        return false;
    }

    // Moves a branch's global matrices (rebuilt by CalculateMtx each frame), not the local transforms animations own.
    void translateBranch(nw4r::lyt::Pane* pPane, f32 x, f32 y, s32 depth) {
        if (depth > 16 || !pPane->IsVisible()) {
            return;
        }
        pPane->mGlbMtx._03 += x;
        pPane->mGlbMtx._13 += y;
        for (nw4r::lyt::PaneList::Iterator it = pPane->GetChildList().GetBeginIter(); it != pPane->GetChildList().GetEndIter(); ++it) {
            translateBranch(&*it, x, y, depth + 1);
        }
    }

    // Scales one row of the drawn matrix: the pane scales about the canvas centre.
    void scaleRow(nw4r::lyt::Pane* pPane, s32 row, f32 k) {
        for (s32 i = 0; i < 4; i++) {
            pPane->mGlbMtx.m[row][i] *= k;
        }
    }

    // Grows a bar about its inner edge until its outer edge is `delta` further out. A picture spans 0..-h, h/2..-h/2
    // or h..0 by its base position's row.
    void extendVertical(nw4r::lyt::Pane* pPane, f32 delta, bool up) {
        const f32 height = pPane->mSize.height;
        const s32 row = pPane->mBasePosition / 3;
        const f32 top = row == 0 ? 0.0f : row == 1 ? height * 0.5f : height;
        const f32 m11 = pPane->mGlbMtx._11;
        const f32 m13 = pPane->mGlbMtx._13;
        const f32 a = m11 * top + m13;
        const f32 b = m11 * (top - height) + m13;
        const f32 span = __builtin_fabsf(a - b);
        if (!(span > 0.0f) || delta == 0.0f) {
            return;
        }
        const f32 inner = up ? (a < b ? a : b) : (a > b ? a : b);
        const f32 k = (span + delta) / span;
        pPane->mGlbMtx._10 *= k;
        pPane->mGlbMtx._11 *= k;
        pPane->mGlbMtx._12 *= k;
        pPane->mGlbMtx._13 = inner + (m13 - inner) * k;
    }

    // Tests (NEBULA_OPTION_TEST_DUMP_LAYOUTS): the authored tree, one line per pane: depth, name, translation, size,
    // base position (0-8, row-major), visibility.
    void dumpPane(const char* pLayout, const nw4r::lyt::Pane* pPane, s32 depth) {
        if (depth > 16) {
            return;
        }
        OSReport("layout %s: %*s%s t=(%.1f,%.1f) size=(%.1f,%.1f) base=%u %s\n", pLayout, depth * 2, "", pPane->mName,
                 pPane->mTranslate.x, pPane->mTranslate.y, pPane->mSize.width, pPane->mSize.height,
                 (u32)pPane->mBasePosition, pPane->IsVisible() ? "" : "hidden");
        nw4r::lyt::PaneList& children = const_cast< nw4r::lyt::Pane* >(pPane)->GetChildList();
        for (nw4r::lyt::PaneList::Iterator it = children.GetBeginIter(); it != children.GetEndIter(); ++it) {
            dumpPane(pLayout, &*it, depth + 1);
        }
    }

    // True when the branch moved, so the copied matrices need copying again.
    bool applyFit(nw4r::lyt::Pane* pPane, const FitRule& rRule, const Extent& rExtent) {
        const f32 wide = rExtent.mWidth / 608.0f;
        const f32 tall = rExtent.mHeight / 456.0f;
        if (rRule.mFit & FitCover) {
            const f32 k = wide > tall ? wide : tall;
            scaleRow(pPane, 0, k);
            scaleRow(pPane, 1, k);
        }
        if (rRule.mFit & FitWidth) {
            scaleRow(pPane, 0, wide);
        }
        if (rRule.mFit & FitHeight) {
            scaleRow(pPane, 1, tall);
        }
        if (rRule.mFit & (FitExtendUp | FitExtendDown)) {
            extendVertical(pPane, rExtent.mVerticalDelta, (rRule.mFit & FitExtendUp) != 0);
        }
        bool moved = false;
        if ((rRule.mFit & FitFlipSlide) && pPane->mpParent != nullptr && pPane->mTranslate.y != 0.0f) {
            translateBranch(pPane, 0.0f, -2.0f * pPane->mpParent->mGlbMtx._11 * pPane->mTranslate.y, 0);
            moved = true;
        }
        if (rRule.mFit & FitFrame) {
            // Window::DrawSelf reads the size when drawing and CalculateMtx never restores it: always from the
            // authored size.
            const f32 sx = __builtin_fabsf(pPane->mGlbMtx._00);
            const f32 sy = __builtin_fabsf(pPane->mGlbMtx._11);
            if (sx > 0.0f && sy > 0.0f) {
                pPane->mSize.width = rRule.mWidth + 2.0f * rExtent.mHorizontalDelta / sx;
                pPane->mSize.height = rRule.mHeight + 2.0f * rExtent.mVerticalDelta / sy;
            }
        }
        f32 x = (rRule.mFit & FitLeft) ? -rExtent.mHorizontalDelta : (rRule.mFit & FitRight) ? rExtent.mHorizontalDelta : 0.0f;
        // A pane the game slides off the 16:9 canvas (local x reaching mWidth) must leave the wider one too.
        if (rRule.mFit & FitSlide) {
            x += pPane->mTranslate.x * rExtent.mHorizontalDelta / rRule.mWidth;
        }
        const f32 y = (rRule.mFit & FitTop) ? rExtent.mVerticalDelta : (rRule.mFit & FitBottom) ? -rExtent.mVerticalDelta : 0.0f;
        if (x == 0.0f && y == 0.0f) {
            return moved;
        }
        translateBranch(pPane, x, y, 0);
        return true;
    }

    // The cinema bars (CinemaFrame's FrameU and FrameD) stay at the edges of a taller canvas. Wait, Appear and End
    // author a 60-unit bar; Close and Open animate it to 250 to meet at the centre, so a closing bar also gains its
    // share of the extra half height.
    void fitCinemaBars(nw4r::lyt::Pane* pRoot, const Extent& rExtent) {
        if (rExtent.mVerticalDelta == 0.0f) {
            return;
        }
        for (nw4r::lyt::PaneList::Iterator owner = pRoot->GetChildList().GetBeginIter(); owner != pRoot->GetChildList().GetEndIter(); ++owner) {
            if (!isNamed(&*owner, "CinemaFrame")) {
                continue;
            }
            for (nw4r::lyt::PaneList::Iterator bar = owner->GetChildList().GetBeginIter(); bar != owner->GetChildList().GetEndIter(); ++bar) {
                const bool upper = isNamed(&*bar, "FrameU");
                if (!upper && !isNamed(&*bar, "FrameD")) {
                    continue;
                }
                const f32 height = bar->mSize.height;
                if (!(height > 0.0f)) {
                    continue;
                }
                f32 closing = (height - 60.0f) / 190.0f;
                closing = closing < 0.0f ? 0.0f : closing > 1.0f ? 1.0f : closing;
                bar->mSize.height = height + closing * rExtent.mVerticalDelta;
                translateBranch(&*bar, 0.0f, upper ? rExtent.mVerticalDelta : -rExtent.mVerticalDelta, 0);
            }
        }
    }
};  // namespace

namespace LayoutAspect {
    void extendBackings(LayoutManager* pManager) {
        if (!MR::isScreen16Per9() || !MR::getDisplay().wide) {
            return;
        }
        nw4r::lyt::Pane* pRoot = pManager->mLayout->mpRootPane;
        if (pRoot == nullptr || !isNamed(pRoot, "RootPane")) {
            return;
        }
        // The owning layout and exact names identify each backing: generic Base and PicBG panes elsewhere stay.
        const Extent extent = getExtent();
        bool safety = false;
        bool title = false;
        for (nw4r::lyt::PaneList::Iterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
            safety |= isNamed(&*it, "WiiRemoteStrap");
            title |= isNamed(&*it, "SMGTitleLogo");
        }
        for (nw4r::lyt::PaneList::Iterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
            nw4r::lyt::Pane* pPane = &*it;
            if ((safety && isNamed(pPane, "PicBG")) || (title && isNamed(pPane, "PicFlash"))) {
                pPane->mSize.width = extent.mWidth;
                pPane->mSize.height = extent.mHeight;
            } else if (isNamed(pPane, "CinemaFrame")) {
                for (nw4r::lyt::PaneList::Iterator bar = pPane->GetChildList().GetBeginIter(); bar != pPane->GetChildList().GetEndIter(); ++bar) {
                    if (isNamed(&*bar, "FrameU") || isNamed(&*bar, "FrameD")) {
                        bar->mSize.width = extent.mWidth;
                    }
                }
            } else if (isNamed(pPane, "PrologueDemo")) {
                for (nw4r::lyt::PaneList::Iterator backing = pPane->GetChildList().GetBeginIter(); backing != pPane->GetChildList().GetEndIter(); ++backing) {
                    if (isNamed(&*backing, "Base") || isNamed(&*backing, "Fade")) {
                        backing->mSize.width = extent.mWidth;
                        backing->mSize.height = extent.mHeight;
                    }
                }
            }
        }
    }

    void fit(LayoutManager* pManager) {
        nw4r::lyt::Pane* pRoot = pManager->mLayout->mpRootPane;
        if (pRoot == nullptr) {
            return;
        }
        const Extent extent = getExtent();
        if (MR::isScreen16Per9() && MR::getDisplay().wide && isNamed(pRoot, "RootPane")) {
            fitCinemaBars(pRoot, extent);
        }

        // This layout's rules and panes, looked up once.
        if (pManager->mFitRuleCount < 0) {
            if (__NebulaOption(NEBULA_OPTION_TEST_DUMP_LAYOUTS) != 0) {
                dumpPane(pManager->mLayoutName != nullptr ? pManager->mLayoutName : "?", pRoot, 0);
            }
            pManager->mFitRuleCount = 0;
            for (u32 i = 0; i < sizeof(cFitRules) / sizeof(cFitRules[0]); i++) {
                if (pManager->mLayoutName == nullptr || strcmp(cFitRules[i].mLayout, pManager->mLayoutName) != 0) {
                    continue;
                }
                if (pManager->mFitRuleCount == (s32)(sizeof(pManager->mFitPanes) / sizeof(pManager->mFitPanes[0]))) {
                    break;  // a layout's rules are contiguous and at most mFitPanes' size
                }
                if (pManager->mFitRuleCount == 0) {
                    pManager->mFitRules = &cFitRules[i];
                }
                pManager->mFitPanes[pManager->mFitRuleCount] = pRoot->FindPaneByName(cFitRules[i].mPane, true);
                pManager->mFitRuleCount++;
            }
        }
        const bool enlarged = extent.mHorizontalDelta != 0.0f || extent.mVerticalDelta != 0.0f;
        bool moved = false;
        for (s32 i = 0; i < pManager->mFitRuleCount; i++) {
            const FitRule& rule = static_cast< const FitRule* >(pManager->mFitRules)[i];
            nw4r::lyt::Pane* pPane = pManager->mFitPanes[i];
            if (pPane == nullptr || (!enlarged && !(rule.mFit & (FitFrame | FitFlipSlide))) || !isDrawn(pPane, pRoot)) {
                continue;
            }
            moved |= applyFit(pPane, rule, extent);
        }
        if (!moved) {
            return;
        }
        // The matrix copies (LayoutPaneInfo::mMtxRef) follow the moved panes, in screen units as the first copy.
        for (u32 i = 0; i < pManager->mPaneCount; i++) {
            if (pManager->mPaneInfoList[i].mMtxRef == nullptr) {
                continue;
            }
            nw4r::lyt::Pane* pPane = pManager->mPaneInfoList[i].mPaneCtrl != nullptr ? pManager->mPaneInfoList[i].mPaneCtrl->mPane
                                                                                     : pManager->findPaneByName(pManager->mPaneInfoList[i].mName);
            PSMTXCopy(pPane->mGlbMtx, pManager->mPaneInfoList[i].mMtxRef);
            if (MR::isScreen16Per9()) {
                pManager->mPaneInfoList[i].mMtxRef[0][3] *= MR::getScreenWidth() / 608.0f;
            }
        }
    }
};  // namespace LayoutAspect
#endif
