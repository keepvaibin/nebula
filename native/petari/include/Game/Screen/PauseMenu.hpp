#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;
class LuigiLetter;
class SysInfoWindow;
class NebulaPauseSettingsButton;

class PauseMenu : public LayoutActor {
public:
    /// @brief Creates a new `PauseMenu`.
    PauseMenu();

    /// @brief Intializes the `PauseMenu` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter);

    /// @brief Draws the `PauseMenu` to the screen.
    virtual void draw() const;

    virtual void appear();
    virtual void kill();
    virtual void control();

    void updateStarPane();
    void startPaneAnimWithoutButton(const char*);
    bool isPaneAnimStoppedWithoutButton() const;
    void forceToWaitAllButton();
    void exeSelecting();
    void exeDecided();
    void exeDisappear();
    void exeConfirm();
    void exeGameDataSave();
    void exeLuigiLetter();

    ButtonPaneController* _20;
    ButtonPaneController* _24;
    Vec2f _28;
    SysInfoWindow* _30;
    LuigiLetter* mLuigiLetter;
    ButtonPaneController* _38;
    Vec2f mStageTitleOffset;
    Vec2f mCoinNumPos;
    Vec2f mStarPieceNumPos;
    bool _54;
    NebulaPauseSettingsButton* mSettingsButton;  // Nebula's settings menu, from the lower band
};
