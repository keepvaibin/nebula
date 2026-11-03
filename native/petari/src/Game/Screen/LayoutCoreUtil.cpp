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

    const char* getPaneUserData(const nw4r::lyt::Pane* pPane, char* pUserData) {
        MR::copyMemory(pUserData, pPane->mUserData, 8);
        pUserData[8] = '\0';
        return pUserData;
    }

    u16 setTextBoxMessage(nw4r::lyt::TextBox* pTextBox, const char16_t* pMsg) {
        char16_t buffer[512];
        ReplaceTagProcessor::Replace(buffer, pMsg);
        return pTextBox->SetString(buffer, 0, MR::getStringLengthWithMessageTag(buffer));
    }

    void initTextBoxPane(nw4r::lyt::TextBox* pTextBox, const char* pMessageId, u32 allocSize) {
        CustomTagProcessor* processor = new CustomTagProcessor(pTextBox);

        if (strncmp(pTextBox->mName, "Sha", 3) == 0) {
            processor->mIsShadow = true;
        } else {
            processor->mIsShadow = false;
        }

        if (strncmp(pTextBox->mName, "Txt", 3) == 0) {
            processor->mIsText = true;
        } else {
            processor->mIsText = false;
        }

        if (strncmp(pTextBox->mName, "Inf", 3) == 0) {
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
        } else {
            reallocateTextBoxMessage(pTextBox, allocSize - 1);
            const char16_t* message = ::sDefaultMsg;
            setTextBoxMessage(pTextBox, message);
        }
    }

    void reallocateTextBoxMessage(nw4r::lyt::TextBox* pTextBox, u32 allocSize) {
        pTextBox->AllocStringBuffer(allocSize + 1);
    }
};  // namespace LayoutCoreUtil

namespace MR {
    void convertScreenPosToLayoutPos(TVec2f* pLayoutPos, const TVec2f& rScreenPos) {
        // The layout canvas setupDrawForNW4RLayout projects (pointer hit tests, panes following screen positions).
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            const NebulaDisplay& display = MR::getDisplay();
            pLayoutPos->x = rScreenPos.x * display.layout_width / MR::getScreenWidth() - display.layout_width * 0.5f;
            pLayoutPos->y = -(rScreenPos.y * MR::getScreenHeight() / MR::getScreenHeight() - (MR::getScreenHeight() * 0.5f)) *
                            display.vertical_fit;
            return;
        }
        pLayoutPos->x = rScreenPos.x * NebulaFrameClock::cLayoutWidth / MR::getScreenWidth() - 304.0f;
        pLayoutPos->y = -(rScreenPos.y * MR::getScreenHeight() / MR::getScreenHeight() - (MR::getScreenHeight() * 0.5f));
    }

    void convertLayoutPosToScreenPos(TVec2f* pScreenPos, const TVec2f& rLayoutPos) {
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

    void convertPaneLocalPosToScreenPos(TVec2f* pScreenPos, const nw4r::lyt::Pane* pPane, const TVec2f& rLocalPos) {
        TVec3f pos(rLocalPos);
        TVec3f layoutPos;
        NebulaMtxMultVec(pPane->mGlbMtx, pos, layoutPos);
        convertLayoutPosToScreenPos(pScreenPos, layoutPos);
    }
};  // namespace MR
