#pragma once

#include <revolution/types.h>

// Nebula's own text (its questions, buttons and achievements) in the played language. In order:
//  1. the language folder's MessageData/NebulaText.txt, from a mod, a Riivolution patch or a translation;
//  2. Nebula's own translation for that folder (localization/NebulaText.tsv), or for the language the file names;
//  3. the folder's sister language (UsFrench and EuFrench, ...), then American English.
// {galaxy:Name} in a text becomes that galaxy's name in the game's own text. docs/TRANSLATING.md has the details.
namespace NebulaText {
    // The text for `pId`, tokens expanded, into pDst (at most `capacity` units with the terminator); its length.
    s32 get(wchar_t* pDst, s32 capacity, const char* pId);
};  // namespace NebulaText
