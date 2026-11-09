#pragma once

#include "Game/Screen/SimpleLayout.hpp"

class YesNoController;

class YesNoLayout : public SimpleLayout {
public:
    /// @brief Creates a new `YesNoLayout`.
    YesNoLayout();

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void setInformationMessage(const char*);
    void setSelectMessage(const char*, const char*);
    void exeSelecting();

    YesNoController* mController;
    bool mHasInformationMessage;
};
