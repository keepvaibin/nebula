#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include <string.h>

namespace {
    // A string argument's slot in the text has the console pointer's 4 bytes: it holds a token naming the string in
    // this table (0: none). Each distinct string keeps its entry; a full table reuses the oldest, and a token to a
    // reused entry reads as no string. The game's arguments are long-lived (messages, galaxy names, a few buffers).
    const u32 kParamStringCount = 1024;
    const wchar_t* sParamStrings[kParamStringCount];
    u32 sParamStringGenerations[kParamStringCount];
    u32 sParamStringUsed;
    u32 sParamStringNext;

    u32 paramStringToken(u32 index) {
        return 1 + index + sParamStringGenerations[index] * kParamStringCount;
    }

    u32 nameParamString(const wchar_t* pString) {
        if (pString == nullptr) {
            return 0;
        }
        for (u32 i = 0; i < sParamStringUsed; i++) {
            if (sParamStrings[i] == pString) {
                return paramStringToken(i);
            }
        }
        u32 index;
        if (sParamStringUsed < kParamStringCount) {
            index = sParamStringUsed++;
        } else {
            index = sParamStringNext;
            sParamStringNext = (sParamStringNext + 1) % kParamStringCount;
            sParamStringGenerations[index]++;
        }
        sParamStrings[index] = pString;
        return paramStringToken(index);
    }

    const wchar_t* findParamString(u32 token) {
        if (token == 0) {
            return nullptr;
        }
        const u32 index = (token - 1) % kParamStringCount;
        return index < sParamStringUsed && paramStringToken(index) == token ? sParamStrings[index] : nullptr;
    }
}  // namespace

MessageEditorMessageTag::MessageEditorMessageTag(const nw4r::ut::PrintContext< wchar_t >* pContext) : mMessage(pContext->str) {
}

MessageEditorMessageTag::MessageEditorMessageTag(const wchar_t* pMessage) : mMessage(pMessage) {
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

#ifdef TARGET_PC
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
#else
u8 MessageEditorMessageTag::getParam8(int index) const {
    const u8* pParam = reinterpret_cast< const u8* >(mMessage) + index;
    return pParam[4];
}

u16 MessageEditorMessageTag::getParam16(int index) const {
    const u16* pParam = reinterpret_cast< const u16* >(mMessage) + index;
    return pParam[2];
}

u32 MessageEditorMessageTag::getParam32(int index) const {
    const u32* pParam = reinterpret_cast< const u32* >(mMessage) + index;
    return pParam[1];
}
#endif

wchar_t* MessageEditorMessageTag::getParamPtr(int index) const {
    const u8* pParam = reinterpret_cast< const u8* >(mMessage) + index + 4;
    return const_cast< wchar_t* >(reinterpret_cast< const wchar_t* >(pParam));
}

// The token travels with the message text as the console's pointer did.
void MessageEditorMessageTag::setParamString(const wchar_t* pString) const {
    const u32 token = nameParamString(pString);
    memcpy(getParamPtr(0), &token, sizeof(token));
}

const wchar_t* MessageEditorMessageTag::getParamString() const {
    u32 token;
    memcpy(&token, getParamPtr(0), sizeof(token));
    return findParamString(token);
}

MessageTagSkipTagProcessor::MessageTagSkipTagProcessor() : nw4r::ut::TagProcessorBase< wchar_t >() {
}

nw4r::ut::TagProcessorBase< wchar_t >::Operation MessageTagSkipTagProcessor::CalcRect(nw4r::ut::Rect* pRect, u16 code, ContextType* pPrintContext) {
    if (code != 0x1A) {
        return nw4r::ut::TagProcessorBase< wchar_t >::CalcRect(pRect, code, pPrintContext);
    } else {
        return skipTag(pRect, pPrintContext, false);
    }
}

nw4r::ut::TagProcessorBase< wchar_t >::Operation MessageTagSkipTagProcessor::Process(u16 code, ContextType* pPrintContext) {
    if (code != 0x1A) {
        return nw4r::ut::TagProcessorBase< wchar_t >::Process(code, pPrintContext);
    } else {
        return skipTag(nullptr, pPrintContext, false);
    }
}

nw4r::ut::TagProcessorBase< wchar_t >::Operation MessageTagSkipTagProcessor::skipTag(nw4r::ut::Rect* pRect, ContextType* pPrintContext, bool param3) {
    MessageEditorMessageTag tag(pPrintContext);
    pPrintContext->str += tag.getSkipLength();
    return OPERATION_DEFAULT;
}
