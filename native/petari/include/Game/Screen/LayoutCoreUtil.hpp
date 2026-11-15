#pragma once

#include <nebula_geometry.hpp>
#include <Game/Ui/Pane.hpp>
#include <Game/Ui/TextBox.hpp>

namespace LayoutCoreUtil {
    const char* getPaneUserData(const Ui::Pane*, char*);
    u32 setTextBoxMessage(Ui::TextBox* pTextBox, const char16_t* pMsg);
    // No message (pStr null or not found): the game's placeholder XXXXXXXX in a buffer of allocSize, or with keepLayoutText
    // the box as its layout file made it (its buffer and its text).
    void initTextBoxPane(Ui::TextBox* pTextBox, const char* pStr, u32 allocSize, bool keepLayoutText = false);
    void reallocateTextBoxMessage(Ui::TextBox* pTextBox, u32 allocSize);
};  // namespace LayoutCoreUtil

namespace MR {
    void convertScreenPosToLayoutPos(Vec2f*, const Vec2f&);
    void convertLayoutPosToScreenPos(Vec2f*, const Vec2f&);
    void convertPaneLocalPosToScreenPos(Vec2f*, const Ui::Pane*, const Vec2f&);
};  // namespace MR
