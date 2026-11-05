#pragma once

#include <nebula_geometry.hpp>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>

namespace LayoutCoreUtil {
    const char* getPaneUserData(const nw4r::lyt::Pane*, char*);
    u16 setTextBoxMessage(nw4r::lyt::TextBox* pTextBox, const char16_t* pMsg);
    void initTextBoxPane(nw4r::lyt::TextBox* pTextBox, const char* pStr, u32 allocSize);
    void reallocateTextBoxMessage(nw4r::lyt::TextBox* pTextBox, u32 allocSize);
};  // namespace LayoutCoreUtil

namespace MR {
    void convertScreenPosToLayoutPos(Vec2f*, const Vec2f&);
    void convertLayoutPosToScreenPos(Vec2f*, const Vec2f&);
    void convertPaneLocalPosToScreenPos(Vec2f*, const nw4r::lyt::Pane*, const Vec2f&);
};  // namespace MR
