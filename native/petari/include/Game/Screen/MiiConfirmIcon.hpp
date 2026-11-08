#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class TexMap;
}  // namespace Ui

class MiiConfirmIcon : public LayoutActor {
public:
    /// @brief Creates a new `MiiConfirmIcon`.
    /// @param pName The pointer to the null-terminated name of the object.
    MiiConfirmIcon(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void appear();

    void appear(Ui::TexMap*, const char16_t*);
    void disappear();
    void exeAppear();
    void exeWait();
    void exeDisappear();
    bool isDisappear() const;
};
