#pragma once

#include <revolution/types.h>

namespace nw4r {
    namespace lyt {
        class TextBox;
    };
};  // namespace nw4r

// Nebula's own text (its questions, buttons and achievements) in the played language. In order:
//  1. the language folder's MessageData/NebulaText.txt, from a mod, a Riivolution patch or a translation;
//  2. Nebula's own translation for that folder (localization/NebulaText.tsv), or for the language the file names;
//  3. the folder's sister language (UsFrench and EuFrench, ...), then American English.
// {galaxy:Name} in a text becomes that galaxy's name in the game's own text. docs/TRANSLATING.md has the details.
namespace NebulaText {
    // The text for `pId`, tokens expanded, into pDst (at most `capacity` units with the terminator); its length.
    s32 get(wchar_t* pDst, s32 capacity, const char* pId);
    // Whether any of those has a text for `pId`.
    bool has(const char* pId);
    // A talk in the game's balloon for MessageData (Nebula_<id>), its pages in the played language: AchievementLuma
    // (LumaGreeting, then LumaBoard); null for another id.
    const wchar_t* getTalk(const char* pId);
    // Whether pMessage is (a page of) a talk getTalk gave.
    bool isTalk(const wchar_t* pMessage);
    // TalkTextFormer, as a talk's page shows in pBox: a talk of getTalk's has its lines broken where they would run
    // past the box, as the box measures them (font, size, spacing, the player's name). Other text is left alone.
    void fitTalk(const wchar_t* pMessage, nw4r::lyt::TextBox* pBox);
};  // namespace NebulaText
