#pragma once

// A tag in a message's UTF-16 text: cMessageTagEscape, a unit holding the tag's group (high byte) and its size in bytes
// from the escape on (low byte), the tag's number, then its data (MessageEditorMessageTag reads it).
const char16_t cMessageTagEscape = 0x1A;

enum MessageTagGroup {
    MessageTagGroup_Display = 1,
    MessageTagGroup_Sound = 2,
    MessageTagGroup_Picture = 3,
    MessageTagGroup_FontSize = 4,
    MessageTagGroup_Localize = 5,
    MessageTagGroup_Number = 6,
    MessageTagGroup_String = 7,
    MessageTagGroup_SubMessage = 8,
    MessageTagGroup_RaceTime = 9,
    MessageTagGroup_Font = 10,
    MessageTagGroup_System = 255,
};

// MessageTagGroup_Display's tag for the next page.
const char16_t cMessageTagNextPage = 1;

// A tag's second unit.
constexpr char16_t makeMessageTagHead(u32 group, u32 bytes) {
    return static_cast< char16_t >(group << 8 | bytes);
}

// A tag's size in units, from its second unit.
constexpr s32 getMessageTagUnits(char16_t head) {
    return (head & 0xFF) / 2;
}

// The second unit of the next page's tag (escape, head, tag number).
const char16_t cMessageTagNextPageHead = makeMessageTagHead(MessageTagGroup_Display, 6);
