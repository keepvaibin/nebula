#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;

class BackButton : public LayoutActor {
public:
    BackButton(const char* pName, bool connectToDecoration);

    virtual ~BackButton() override;
    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    void disappear();
    bool isHidden() const;
    bool isAppearing() const;
    bool isDisappearing() const;
    bool isPointing() const;
    // Chosen since it last appeared.
    bool isSelected() const {
        return mIsSelected;
    }

    ButtonPaneController* mPaneCtrl;
    bool mIsSelected;
};
