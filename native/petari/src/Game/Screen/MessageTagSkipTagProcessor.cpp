#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#ifdef TARGET_PC
#include <nebula_endian.h>
#include <revolution/os.h>
#endif

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

#ifdef TARGET_PC
// The slot holds the string's console address, big-endian as the console stored the pointer: the bytes travel with the
// message text as the pointer's did, and the first is nonzero once set (RMGK01 tests it).
void MessageEditorMessageTag::setParamString(const wchar_t* pString) const {
    nebula_store_be32(getParamPtr(0), pString != nullptr ? NEBULA_CONSOLE_ADDRESS(pString) : 0u);
}

const wchar_t* MessageEditorMessageTag::getParamString() const {
    const u32 address = nebula_load_be32(getParamPtr(0));
    return address != 0 ? static_cast< const wchar_t* >(OSPhysicalToCached(address - OS_BASE_CACHED)) : nullptr;
}
#endif

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
