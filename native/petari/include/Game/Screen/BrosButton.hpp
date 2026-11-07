#pragma once

#include "Game/Screen/LayoutActor.hpp"

class ButtonPaneController;

class BrosButton : public LayoutActor {
public:
    BrosButton(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void control();

    void appear(bool);
    void disappear();
    bool isSelected() const;
    bool isSelectedMario() const;
    void resume();
    void exeAppear();
    void exeSelect();
    void exeDecide();
    void exeDisappear();

private:
    bool mIsSelectedMario;
    ButtonPaneController* mPaneCtrl;
};
