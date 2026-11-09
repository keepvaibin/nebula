#pragma once

#include "Game/System/NerveExecutor.hpp"

class LayoutActor;
class Nerve;

namespace Ui {
    class Pane;
}  // namespace Ui

class ButtonPaneController : public NerveExecutor {
public:
    /// @brief Creates a new `ButtonPaneController`.
    ButtonPaneController(LayoutActor* pHost, const char* pPaneName, const char* pBoundingPaneName, u32 animIndex, bool);

    void update();
    void appear();
    void disappear();
    bool trySelect();
    bool isHidden() const;
    bool isPointing() const;
    bool isPointingTrigger() const;
    bool isTimingForSelectedSe() const;
    void invalidateDecide();
    void invalidateAppearance();
    bool isAppearing() const;
    bool isDisappearing() const;
    void forceToWait();
    void forceToHide();
    bool isFirstStepWait() const;
    bool isWait() const;
    bool isDecidedWait() const;
    bool isNotPointing() const;
    bool onPointing();
    void decide();
    bool isPointingPaneDirect() const;
    bool startAnimAtFirstStep(const char*);
    bool startPointingAnimAtFirstStep(const char*);
    void setNerveAtAnimStopped(const Nerve*);
    bool isPointingPane() const;
    bool tryPointing(bool);
    bool tryNotPointing(bool);
    f32 calcPointingAnimStartFrame() const;
    void exeHidden();
    void exeAppear();
    void exeWait();
    void exePointing();
    void exeNotPointing();
    void exeDecided();
    void exeDecidedWait();
    void exeDecidedToDisappear();
    void exeDisappear();

    LayoutActor* mHost;
    const char* mPaneName;
    const char* mBoundingPaneName;
    const Ui::Pane* mBoundingPane;  // found once: the pane the pointer and the menu focus test
    u32 mAnimIndex;
    bool mReleaseOnDecide;
    f32 mPointingAnimStartFrame;
    bool mIsSelected;
    bool mIsPointing;
    bool mDisappearsAfterDecide;
    bool mIsPointingExternal;
    bool mIsActive;
    const char* mAppearAnimName;
    const char* mWaitAnimName;
    const char* mDecideAnimName;
    const char* mPointingAnimName;
    const char* mNotPointingAnimName;
    const char* mDisappearAnimName;
    bool mIsNebulaFocusable;  // the D-pad and stick may highlight it (NebulaMenuFocus); off where they page the menu
};