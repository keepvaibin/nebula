#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class TexMap;
}  // namespace Ui

class ErrorMessageWindow : public LayoutActor {
public:
    enum MessageType {
        MessageType_System,
        MessageType_Game,
        MessageType_Nebula,
    };

    /// @brief Creates a new `ErrorMessageWindow`.
    ErrorMessageWindow();

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;

    bool isAnimEnd() const;
    void appearWithMessage(const char*, MessageType, const Ui::TexMap*);
    void disappear();
    void exeHide();
    void exeAppear();
    void exeDisplay();
    void exeDisappear();
    void exeDisappearBeforeAppear();

private:
    MessageType mMessageType;
    const char* mMessageId;
    const Ui::TexMap* mTexture;
};
