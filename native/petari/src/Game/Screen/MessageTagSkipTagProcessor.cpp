#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/Util/Array.hpp"
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
    return reinterpret_cast< const u8* >(mMessage)[0] - 2;
}

u32 MessageEditorMessageTag::getSkipLength() const {
    return getTagLength() / 2;
}

s32 MessageEditorMessageTag::getParamLength() const {
    return reinterpret_cast< const u8* >(mMessage)[0] - 6;
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

Ui::TagProcessorBase< char16_t >::Operation MessageTagSkipTagProcessor::skipTag(Ui::Rect* pRect, ContextType* pPrintContext, bool param3) {
    MessageEditorMessageTag tag(pPrintContext);
    pPrintContext->str += tag.getSkipLength();
    return OPERATION_DEFAULT;
}
