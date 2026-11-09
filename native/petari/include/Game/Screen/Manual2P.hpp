#pragma once

#include "Game/Screen/LayoutActor.hpp"

class BackButton;
class ButtonPaneController;

class Manual2P : public LayoutActor {
public:
    /// @brief Creates a new `Manual2P`.
    /// @param pName The pointer to the null-terminated name of the object.
    Manual2P(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    bool isClosed() const;
    void exeAppear();
    void exeWait();
    void exeScrollRight();
    void exeScrollRightAfter();
    void exeScrollLeft();
    void exeScrollLeftAfter();
    void exeDisappear();
    void reflectPageIndex();
    bool checkSelectedBackButton();

private:
    s32 mPageIndex;
    s32 mPageNum;
    ButtonPaneController* mLeftPaneCtrl;
    ButtonPaneController* mRightPaneCtrl;
    bool mCanScrollLeft;  // not on the first page
    bool mCanScrollRight;  // not on the last page
    BackButton* mBackButton;
};
