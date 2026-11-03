#pragma once

#include "Game/Screen/SimpleLayout.hpp"
#include "Game/System/NerveExecutor.hpp"

class TriggerChecker;

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
    void exeDecide();
    void exeDead();

private:
    TitleSequenceProductSub::LogoLayout* mLogoLayout;
    SimpleLayout* mPressStartLayout;
    TriggerChecker* mAButtonChecker;
    TriggerChecker* mBButtonChecker;
};

namespace TitleSequenceProductSub {
    class LogoLayout : public SimpleLayout {
    public:
        /// @brief Creates a new `LogoLayout`.
        LogoLayout();
    };
};  // namespace TitleSequenceProductSub
