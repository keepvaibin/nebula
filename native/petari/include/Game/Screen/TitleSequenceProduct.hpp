#pragma once

#include "Game/Screen/SimpleLayout.hpp"
#include "Game/System/NerveExecutor.hpp"

class NebulaPaneKit;
class TriggerChecker;
namespace Ui {
    class Pane;
    class TextBox;
};  // namespace Ui

namespace TitleSequenceProductSub {
    class LogoLayout;
};  // namespace TitleSequenceProductSub

class TitleSequenceProduct : public NerveExecutor {
public:
    /// @brief Creates a new `TitleSequenceProduct`.
    TitleSequenceProduct();

    void appear();
    void kill();
    bool isActive() const;
    void updateButtonReaction(TriggerChecker* pButtonChecker, const char* pAnimName);
    void updatePressStartReaction();
    void exeBgmPrepare();
    void exeLogoFadein();
    void exeLogoWait();
    void exeLogoDisplay();
    void exeUpdateNotice();
    void exeDecide();
    void exeDead();

private:
    void decide();
    void initVersion();
    void stepVersion(f32 target);

    TitleSequenceProductSub::LogoLayout* mLogoLayout;
    SimpleLayout* mPressStartLayout;
    TriggerChecker* mAButtonChecker;
    TriggerChecker* mBButtonChecker;
    // Nebula's version, small at the bottom right while the title waits for a press (null when its panes are missing).
    SimpleLayout* mVersionLayout;
    NebulaPaneKit* mVersionKit;
    Ui::Pane* mVersionRoot;
    Ui::TextBox* mVersionText;
    f32 mVersionAlpha;
};

namespace TitleSequenceProductSub {
    class LogoLayout : public SimpleLayout {
    public:
        /// @brief Creates a new `LogoLayout`.
        LogoLayout();
    };
};  // namespace TitleSequenceProductSub
