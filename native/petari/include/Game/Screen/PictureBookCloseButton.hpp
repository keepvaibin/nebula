#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;

class PictureBookCloseButton : public LayoutActor {
public:
    /// @brief Creates a new `PictureBookCloseButton`.
    PictureBookCloseButton(bool);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    void disappear();
    bool trySelect();
    bool isSelected() const;
    void exeSelect();
    void exeSelected();
    void exeNotSelected();
    ButtonPaneController* getPaneCtrl() const {
        return mPaneCtrl;
    }

private:
    ButtonPaneController* mPaneCtrl;
    bool mCanCloseWithPad;
};
