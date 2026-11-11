#pragma once


namespace Ui {
    class TextBox;
}  // namespace Ui

namespace MR {
    template < class T >
    struct GrowBuffer;
}  // namespace MR

// Nebula's own text (its questions, buttons and achievements) in the played language. In order:
//  1. the language folder's MessageData/NebulaText.txt, from a mod, a Riivolution patch or a translation;
//  2. Nebula's own translation for that folder (localization/NebulaText.tsv), or for the language the file names;
//  3. the folder's sister language (UsFrench and EuFrench, ...), then American English.
// {galaxy:Name} in a text becomes that galaxy's name in the game's own text. docs/TRANSLATING.md has the details.
namespace NebulaText {
    // The text for `pId`, tokens expanded, into pDst (at most `capacity` units with the terminator); its length.
    s32 get(char16_t* pDst, s32 capacity, const char* pId);
    // The units get gives for `pId`, without the terminator: get with one more than this has all of it.
    s32 length(const char* pId);
    // All of the text for `pId` in pText (which grows to hold it); pText's items.
    const char16_t* get(MR::GrowBuffer< char16_t >* pText, const char* pId);
    // Whether any of those has a text for `pId`.
    bool has(const char* pId);
    // A row's American English words as the table holds them, its {names} unfilled (the game's log, which is English);
    // null when there is no such row.
    const char16_t* english(const char* pId);
    // A talk in the game's balloon for MessageData (Nebula_<id>), its pages in the played language: AchievementLuma
    // (LumaGreeting, then LumaBoard); null for another id.
    const char16_t* getTalk(const char* pId);
    // Whether pMessage is (a page of) a talk getTalk gave.
    bool isTalk(const char16_t* pMessage);
    // TalkTextFormer, as a talk's page shows in pBox: a talk of getTalk's has its lines broken where they would run
    // past the box, as the box measures them (font, size, spacing, the player's name). Other text is left alone.
    void fitTalk(const char16_t* pMessage, Ui::TextBox* pBox);
};  // namespace NebulaText
