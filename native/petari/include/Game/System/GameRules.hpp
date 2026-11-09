#pragma once

#include <nebula_types.h>

// The game's rule numbers that several files read: one tunable each, kept here so they agree ("GameRules.<name>" in
// luma.json's tunables). Numbers only one file reads are tunables of that file.
namespace GameRules {
    // The health and most health a player starts with (3).
    s32 playerHealth();
    // The most health a Life Up gives (6); 0: it gives none.
    s32 lifeUpHealth();
    // The most coins in a stage (999).
    s32 maxCoins();
    // The most star bits kept in the save (9999).
    s32 maxStarBits();
    // Only a shell hurts Dodoryu, the burrowing boss; the game: any player hit.
    bool dodoryuShellOnly();
    // Begoman spinners survive fire floors; the game: they burn.
    bool begomanFireProof();
}  // namespace GameRules
