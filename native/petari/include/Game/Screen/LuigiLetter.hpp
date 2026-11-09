#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class TexMap;
}  // namespace Ui

class IconAButton;

class LuigiLetter : public LayoutActor {
public:
    /// @brief Creates a new `LuigiLetter`.
    LuigiLetter(bool, const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void movement() override;

    void exeAppear();
    void exeWait();
    void exeEnd();

private:
    Ui::TexMap* mTexture;
    IconAButton* mAButtonIcon;
    const char* mGalaxyName;
    bool mIsForTalk;
};

namespace MR {
    LuigiLetter* createLuigiLetterForTalk();
    LuigiLetter* createLuigiLetterForMenu();
};  // namespace MR
