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

    ButtonPaneController* mUpperButton;  // pane NBackUp
    ButtonPaneController* mLowerButton;  // pane NBackDown
    SysInfoWindow* mConfirmWindow;  // the yes/no confirm
    LuigiLetter* mLuigiLetter;
    ButtonPaneController* mLetterButton;
    Vec2f mStageTitleOffset;
    Vec2f mCoinNumPos;
    Vec2f mStarPieceNumPos;
    bool mWasDefaultLayoutActive;  // restored on kill
    NebulaPauseSettingsButton* mSettingsButton;  // Nebula's settings menu, from the lower band
};
