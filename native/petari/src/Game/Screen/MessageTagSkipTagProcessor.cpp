#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/ByteView.hpp"
#include "Game/Util/StringUtil.hpp"
#include <string.h>

namespace {
    // A string argument's slot in the text has 4 bytes: it holds a token naming the string in
    // this table (0: none). Each distinct string keeps its entry. The game's arguments are long-lived (messages,
    // galaxy names, a few buffers).
    MR::GrowBuffer< const char16_t* > sParamStrings;
    u32 sParamStringUsed;

    u32 nameParamString(const char16_t* pString) {
        if (pString == nullptr) {
            return 0;
        }
        for (u32 i = 0; i < sParamStringUsed; i++) {
            if (sParamStrings[i] == pString) {
                return i + 1;
            }
        }
        const char16_t** const ppStrings = sParamStrings.room(sParamStringUsed + 1);
        ppStrings[sParamStringUsed] = pString;
        return ++sParamStringUsed;
    }

    const char16_t* findParamString(u32 token) {
        if (token == 0 || token > sParamStringUsed) {
            return nullptr;
        }
        return sParamStrings[token - 1];
    }
}  // namespace

MessageEditorMessageTag::MessageEditorMessageTag(const Ui::PrintContext< char16_t >* pContext) : mMessage(pContext->str) {
}

MessageEditorMessageTag::MessageEditorMessageTag(const char16_t* pMessage) : mMessage(pMessage) {
}

u32 MessageEditorMessageTag::getTagLength() const {
    // A tag whose length byte is under 2 (a corrupt or edited message) skips nothing, not 4 billion.
    const u32 length = reinterpret_cast< const u8* >(mMessage)[0];
    return length >= 2 ? length - 2 : 0;
}

u32 MessageEditorMessageTag::getSkipLength() const {
    return getTagLength() / 2;
}

s32 MessageEditorMessageTag::getParamLength() const {
    // A tag too short to hold parameters has none.
    const s32 length = reinterpret_cast< const u8* >(mMessage)[0];
    return length >= 6 ? length - 6 : 0;
}

// Natively the parameters are host-order 16-bit units (MessageData converts the file's text that way, and the game
// builds its own tags so): byte i is the high byte of unit i / 2 for even i, the low byte for odd i, and a 32-bit
// parameter is two units, high first.
u8 MessageEditorMessageTag::getParam8(int index) const {
    const u16 unit = reinterpret_cast< const u16* >(mMessage)[2 + index / 2];
    return (u8)((index & 1) ? unit : unit >> 8);
}

u16 MessageEditorMessageTag::getParam16(int index) const {
    const u16* pParam = reinterpret_cast< const u16* >(mMessage) + index;
    return pParam[2];
}

u32 MessageEditorMessageTag::getParam32(int index) const {
    const u16* pParam = reinterpret_cast< const u16* >(mMessage) + index * 2;
    return ((u32)pParam[2] << 16) | pParam[3];
}

char16_t* MessageEditorMessageTag::getParamPtr(int index) const {
    const u8* pParam = reinterpret_cast< const u8* >(mMessage) + index + 4;
    return const_cast< char16_t* >(reinterpret_cast< const char16_t* >(pParam));
}

// The token travels with the message text.
void MessageEditorMessageTag::setParamString(const char16_t* pString) const {
    const u32 token = nameParamString(pString);
    memcpy(getParamPtr(0), &token, sizeof(token));
}

const char16_t* MessageEditorMessageTag::getParamString() const {
    u32 token;
    memcpy(&token, getParamPtr(0), sizeof(token));
    return findParamString(token);
}

s32 MessageTagView::getGroup() const {
    return isRuns() ? mRecord->mGroup : mInText.getGroup();
}

int MessageTagView::getTag() const {
    return isRuns() ? mRecord->mTag : mInText.getTag();
}

u8 MessageTagView::getParam8(int index) const {
    if (!isRuns()) {
        return mInText.getParam8(index);
    }
    const u32 unit = static_cast< u32 >(index) / 2;
    if (index < 0 || unit >= mRecord->mParamNum) {
        return 0;
    }
    const u16 value = mText->getParams(*mRecord)[unit];
    return static_cast< u8 >((index & 1) ? value : value >> 8);
}

u16 MessageTagView::getParam16(int index) const {
    if (!isRuns()) {
        return mInText.getParam16(index);
    }
    return index >= 0 && static_cast< u32 >(index) < mRecord->mParamNum ? mText->getParams(*mRecord)[index] : 0;
}

s32 MessageTagView::getParamLength() const {
    return isRuns() ? static_cast< s32 >(mRecord->mParamNum * sizeof(u16)) : mInText.getParamLength();
}

s32 MessageTagView::getNumber() const {
    if (!isRuns()) {
        return readBytes< s32 >(mInText.getParamPtr(0));
    }
    return mRecord->mParamNum >= 2 ? readBytes< s32 >(mText->getParams(*mRecord)) : 0;
}

u32 MessageTagView::getString(MR::Text16& rDst) const {
    const u32 num = isRuns() ? mRecord->mStringNum : 0;
    char16_t* const pDst = rDst.room(num * 2 + 1);
    u32 units = 0;
    for (u32 i = 0; i < num; i++) {
        const char32_t code = mText->getString(*mRecord)[i];
        if (code > 0xFFFF) {
            const u32 bits = static_cast< u32 >(code) - 0x10000;
            pDst[units++] = static_cast< char16_t >(0xD800 | ((bits >> 10) & 0x3FF));
            pDst[units++] = static_cast< char16_t >(0xDC00 | (bits & 0x3FF));
        } else {
            pDst[units++] = static_cast< char16_t >(code);
        }
    }
    pDst[units] = u'\0';
    return units;
}

MessageTagSkipTagProcessor::MessageTagSkipTagProcessor() : Ui::TagProcessorBase< char16_t >() {
}

Ui::TagProcessorBase< char16_t >::Operation MessageTagSkipTagProcessor::CalcRect(Ui::Rect* pRect, u16 code, ContextType* pPrintContext) {
    if (code != 0x1A) {
        return Ui::TagProcessorBase< char16_t >::CalcRect(pRect, code, pPrintContext);
    } else {
        return skipTag(pRect, pPrintContext, false);
    }
}

Ui::TagProcessorBase< char16_t >::Operation MessageTagSkipTagProcessor::Process(u16 code, ContextType* pPrintContext) {
    if (code != 0x1A) {
        return Ui::TagProcessorBase< char16_t >::Process(code, pPrintContext);
    } else {
        return skipTag(nullptr, pPrintContext, false);
    }
}

Ui::TagProcessorBase< char16_t >::Operation MessageTagSkipTagProcessor::skipTag(Ui::Rect* /*pRect*/, ContextType* pPrintContext, bool /*param3*/) {
    MessageEditorMessageTag tag(pPrintContext);
    pPrintContext->str += tag.getSkipLength();
    return OPERATION_DEFAULT;
}
