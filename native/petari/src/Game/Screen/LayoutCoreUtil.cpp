#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/CustomTagProcessor.hpp"
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>
#include <nebula_math.h>

static const char16_t sDefaultMsg[] = u"XXXXXXXX";

namespace LayoutCoreUtil {

    const char* getPaneUserData(const Ui::Pane* pPane, char* pUserData) {
        MR::copyMemory(pUserData, pPane->mUserData, 8);
        pUserData[8] = '\0';
        return pUserData;
    }

    u32 setTextBoxMessage(Ui::TextBox* pTextBox, const char16_t* pMsg) {
        char16_t stack[512];  // the common case on the stack; past it the text is made in a Text16, whole
        MR::Text16 heap;
        const u32 room = ReplaceTagProcessor::getReplaceRoom(pMsg);
        char16_t* const buffer = room <= 512 ? stack : heap.room(room);
        ReplaceTagProcessor::Replace(buffer, pMsg);
        const u32 length = static_cast< u32 >(MR::getStringLengthWithMessageTag(buffer));
        // A box that has a string buffer holds whatever is set; its buffer grows to the text.
        if (pTextBox->GetStringBufferLength() != 0 && length > pTextBox->GetStringBufferLength()) {
            pTextBox->AllocStringBuffer(length);
        }

        return pTextBox->SetString(buffer, 0, length);
    }

    void initTextBoxPane(Ui::TextBox* pTextBox, const char* pMessageId, u32 allocSize, bool keepLayoutText) {
        CustomTagProcessor* processor = new CustomTagProcessor(pTextBox);

        if (MR::isEqualStringPrefix(pTextBox->mName, "Sha")) {
            processor->mIsShadow = true;
        } else {
            processor->mIsShadow = false;
        }

        if (MR::isEqualStringPrefix(pTextBox->mName, "Txt")) {
            processor->mIsText = true;
        } else {
            processor->mIsText = false;
        }

        if (MR::isEqualStringPrefix(pTextBox->mName, "Inf")) {
            processor->mIsInfo = true;
        } else {
            processor->mIsInfo = false;
        }

        pTextBox->mpTagProcessor = processor;

        const char16_t* message = nullptr;
        if (pMessageId != nullptr) {
            message = MR::getLayoutMessageDirect(pMessageId);
        }

        if (message != nullptr) {
            reallocateTextBoxMessage(pTextBox, MR::getStringLengthWithMessageTag(message));
            setTextBoxMessage(pTextBox, message);
        } else if (keepLayoutText && pTextBox->mTextBuf != nullptr && pTextBox->mTextLen != 0) {
            // A mod's screen shows the words its layout gives (contract layout: a text pane's `text` is the words as they
            // show) until the mod sets others. Copied first: the box's buffer may be replaced as it is made room for.
            const u32 length = pTextBox->mTextLen;
            MR::Text16 layoutText;
            char16_t* const copy = layoutText.room(length + 1);
            memcpy(copy, pTextBox->mTextBuf, length * sizeof(char16_t));
            copy[length] = u'\0';
            reallocateTextBoxMessage(pTextBox, length > allocSize - 1 ? length : allocSize - 1);
            setTextBoxMessage(pTextBox, copy);
        } else {
            reallocateTextBoxMessage(pTextBox, allocSize - 1);
            const char16_t* message = ::sDefaultMsg;
            setTextBoxMessage(pTextBox, message);
        }
    }

    void reallocateTextBoxMessage(Ui::TextBox* pTextBox, u32 allocSize) {
        pTextBox->AllocStringBuffer(allocSize + 1);
    }
};  // namespace LayoutCoreUtil

namespace MR {
    void convertScreenPosToLayoutPos(Vec2f* pLayoutPos, const Vec2f& rScreenPos) {
        // The layout canvas setupDrawForLayout projects (pointer hit tests, panes following screen positions).
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            const NebulaDisplay& display = MR::getDisplay();
            pLayoutPos->x = rScreenPos.x * display.layout_width / MR::getScreenWidth() - display.layout_width * 0.5f;
            pLayoutPos->y = -(rScreenPos.y * MR::getScreenHeight() / MR::getScreenHeight() - (MR::getScreenHeight() * 0.5f)) *
                            display.vertical_fit;
            return;
        }
        pLayoutPos->x = rScreenPos.x * NebulaFrameClock::cLayoutWidth / MR::getScreenWidth() - NebulaFrameClock::cLayoutWidth * 0.5f;
        pLayoutPos->y = -(rScreenPos.y * MR::getScreenHeight() / MR::getScreenHeight() - (MR::getScreenHeight() * 0.5f));
    }

    void convertLayoutPosToScreenPos(Vec2f* pScreenPos, const Vec2f& rLayoutPos) {
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            const NebulaDisplay& display = MR::getDisplay();
            pScreenPos->x = rLayoutPos.x * MR::getScreenWidth() / display.layout_width + MR::getScreenWidth() * 0.5f;
            pScreenPos->y = MR::getScreenHeight() * 0.5f +
                            -(rLayoutPos.y / display.vertical_fit) * MR::getScreenHeight() / MR::getScreenHeight();
            return;
        }
        pScreenPos->x = rLayoutPos.x * MR::getScreenWidth() / NebulaFrameClock::cLayoutWidth + MR::getScreenWidth() * 0.5f;
        pScreenPos->y = MR::getScreenHeight() * 0.5f + -rLayoutPos.y * MR::getScreenHeight() / MR::getScreenHeight();
    }

    void convertPaneLocalPosToScreenPos(Vec2f* pScreenPos, const Ui::Pane* pPane, const Vec2f& rLocalPos) {
        Vec3f pos(rLocalPos);
        Vec3f layoutPos;
        NebulaMtxMultVec(pPane->mGlbMtx, pos, layoutPos);
        convertLayoutPosToScreenPos(pScreenPos, layoutPos);
    }
};  // namespace MR
