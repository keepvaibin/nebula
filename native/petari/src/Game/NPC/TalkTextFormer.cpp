#include "Game/NPC/TalkTextFormer.hpp"
#include <nebula_game_api.h>
#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Screen/CustomTagProcessor.hpp"
#include "Game/System/Language.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include <Game/Ui/TextBox.hpp>
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getLanguageRate_f1, "getLanguageRate.f1", 1.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_initTagProcessorRecursive_f1, "initTagProcessorRecursive.f1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_initTagProcessorRecursive_f2, "initTagProcessorRecursive.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_initTagProcessorRecursive_f3, "initTagProcessorRecursive.f3", 0.35f);
NEBULA_TUNABLE_NAMED(f32, cNum_initTagProcessorRecursive_f4, "initTagProcessorRecursive.f4", 0.9f);

namespace {
    Ui::TextBox* getTextBoxPane(LayoutActor* pActor, const char* pPaneName) {
        Ui::TextBox* pTextBox = Ui::PaneCast< Ui::TextBox >(MR::getPane(pActor, pPaneName));

        return pTextBox;
    }

    // The first text box at or under the pane (a balloon's Text00 holds its text and the text's shadow).
    Ui::TextBox* findTextBox(LayoutActor* pActor, const char* pPaneName) {
        Ui::TextBox* pTextBox = getTextBoxPane(pActor, pPaneName);
        Ui::Pane* pPane = MR::getPane(pActor, pPaneName);
        if (pTextBox != nullptr || pPane == nullptr) {
            return pTextBox;
        }
        for (Ui::Pane* pChild : pPane->GetChildList()) {
            pTextBox = findTextBox(pActor, pChild->mName);
            if (pTextBox != nullptr) {
                return pTextBox;
            }
        }
        return nullptr;
    }
};  // namespace

namespace MR {
    inline f32 getLanguageRate() {
        return getLanguage() == LanguageId_JpJapanese ? 1.0f : cNum_getLanguageRate_f1;
    }

    void initTagProcessorRecursive(LayoutActor* pActor, const char* pPaneName, s32 mode) {
        Ui::TextBox* pTextBox = ::getTextBoxPane(pActor, pPaneName);
        if (pTextBox != nullptr) {
            CustomTagProcessor* pProcessor = static_cast< CustomTagProcessor* >(pTextBox->mpTagProcessor);
            f32 rate = getLanguageRate();

            switch (mode) {
            case TalkTextForm_Whole:
                pProcessor->initAlpha(0.0f, 0.0f, 0, 0);
                break;
            case TalkTextForm_Talk:
                pProcessor->initAlpha(cNum_initTagProcessorRecursive_f1 * rate, cNum_initTagProcessorRecursive_f2, 0, 0);
                break;
            case TalkTextForm_Event:
                pProcessor->initAlpha(cNum_initTagProcessorRecursive_f3 * rate, cNum_initTagProcessorRecursive_f4, 0, 0);
                break;
            }
        }

        Ui::Pane* pPane = getPane(pActor, pPaneName);
        for (Ui::Pane* pChild : pPane->GetChildList()) {
            initTagProcessorRecursive(pActor, pChild->mName, mode);
        }
    }

    void nextStepTagProcessorRecursive(LayoutActor* pActor, const char* pPaneName) {
        Ui::TextBox* pTextBox = ::getTextBoxPane(pActor, pPaneName);
        if (pTextBox != nullptr) {
            CustomTagProcessor* pProcessor = static_cast< CustomTagProcessor* >(pTextBox->mpTagProcessor);
            pProcessor->mColorIndex = 0;
            pProcessor->mSoundIndex = 0;
            pProcessor->mAlphaCtrl.update();
        }

        Ui::Pane* pPane = getPane(pActor, pPaneName);
        for (Ui::Pane* pChild : pPane->GetChildList()) {
            nextStepTagProcessorRecursive(pActor, pChild->mName);
        }
    }

    bool isEndStepTagProcessorRecursive(const LayoutActor* pActor, const char* pPaneName, bool isEnd) {
        Ui::Pane* pTargetPane = getPane(pActor, pPaneName);
        Ui::TextBox* pTextBox = Ui::PaneCast< Ui::TextBox >(pTargetPane);
        if (pTextBox != nullptr) {
            CustomTagProcessor* pProcessor = static_cast< CustomTagProcessor* >(pTextBox->mpTagProcessor);
            isEnd &= pProcessor->mAlphaCtrl.isEnd();
        }

        Ui::Pane* pPane = getPane(pActor, pPaneName);
        for (Ui::Pane* pChild : pPane->GetChildList()) {
            isEnd &= isEndStepTagProcessorRecursive(pActor, pChild->mName, true);
        }

        return isEnd;
    }
};  // namespace MR

TalkTextFormer::TalkTextFormer(LayoutActor* pActor, const char* pPaneName) : mHostActor(pActor), mMsg(), mFormMode(), mPaneName(pPaneName) {
}

void TalkTextFormer::formMessage(const char16_t* pMessage, s32 mode) {
    mMsg = pMessage;
    mFormMode = mode;
    if (NebulaText::isTalk(pMessage)) {
        NebulaText::fitTalk(pMessage, ::findTextBox(mHostActor, mPaneName));
    }

    MR::setTextBoxMessageRecursive(mHostActor, mPaneName, mMsg);
    MR::initTagProcessorRecursive(mHostActor, mPaneName, mFormMode);
}

void TalkTextFormer::setArg(const CustomTagArg& rTag, s32 argIndex) {
    if (rTag.mArgType == CustomTagArg::Type_Int) {
        MR::setTextBoxArgNumberRecursive(mHostActor, mPaneName, rTag.mIntArg, argIndex);
    } else if (rTag.mArgType == CustomTagArg::Type_Char) {
        MR::setTextBoxArgStringRecursive(mHostActor, mPaneName, rTag.mCharArg, argIndex);
    }

    MR::initTagProcessorRecursive(mHostActor, mPaneName, mFormMode);
}

bool TalkTextFormer::nextPage() {
    const char16_t* pMessage = MR::getNextMessagePage(mMsg);
    if (pMessage != nullptr) {
        mMsg = pMessage;
        formMessage(pMessage, mFormMode);
        return true;
    }

    return false;
}

bool TalkTextFormer::hasNextPage() const {
    if (mMsg != nullptr) {
        return MR::getNextMessagePage(mMsg) != nullptr;
    }

    return false;
}

void TalkTextFormer::updateTalking() {
    MR::nextStepTagProcessorRecursive(mHostActor, mPaneName);
}

// Nebula's text speed (NEBULA_OPTION_TEXT_SPEED): 1 to 3 steps a frame, 4 the whole page at once, its pauses included.
// A skipped cutscene's pages come whole.
void TalkTextFormer::updateTalkingAtTextSpeed() {
    updateTalking();
    const u32 speed = NebulaCutsceneSkip::isSkipping() ? 4 : NebulaHost::option(NEBULA_OPTION_TEXT_SPEED);
    for (u32 extra = speed >= 4 ? 0x2000 : speed - 1; extra != 0 && !isTextAppearedAll(); extra--) {
        updateTalking();
    }
}

bool TalkTextFormer::isTextAppearedAll() const {
    if (mFormMode == TalkTextForm_Whole) {
        return true;
    }

    return MR::isEndStepTagProcessorRecursive(mHostActor, mPaneName, true);
}
