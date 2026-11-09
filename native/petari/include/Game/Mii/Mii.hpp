#pragma once

// Miis: the player's library (the host's, nebula_game_api.h mii_count and mii_record, any number of them), the six
// default Miis, and the checks the game makes of a Mii. A Mii is the host's NebulaMii: every part, colour, scale and position as
// a number, the names UTF-16, the create ID in the Mii data format.

#include <NebulaGpu.h>
#include <nebula_game_api.h>
#include <nebula_types.h>

namespace Mii {
    enum { NameLength = 10, DefaultCount = 6, FavoriteColorCount = 12 };

    typedef NebulaMii Character;

    // A Mii's create ID: four bytes of type bits and time, four of the device that made it.
    struct Id {
        u8 data[8];
    };

    enum Expression {
        ExpressionNormal,
        ExpressionSmile,
        ExpressionAnger,
        ExpressionSorrow,
        ExpressionSurprise,
        ExpressionBlink,
        ExpressionOpenMouth,
        ExpressionCount
    };

    inline u32 expressionBit(Expression expression) {
        return 1u << expression;
    }

    // Where a Mii comes from: the player's library, or the six defaults (a file without a Mii of the player's).
    enum Source { SourceLibrary, SourceDefault };

    // A Mii of a source by its index.
    struct Ref {
        Source source;
        u32 index;
    };

    // The library's slots (some may be empty); a slot keeps its Mii while the game runs, so saves find it by its ID.
    u32 libraryCount();
    // The Mii a source holds at `index` when it is one the game takes (Mii::isValid).
    bool character(const Ref& rRef, Character* pOut);
    // The library slot holding the Mii of `rId`.
    bool find(const Id& rId, u32* pIndex);
    // Changes each time the library changes (Miis imported while the game runs).
    u32 generation();

    const Id& idOf(const Character& rMii);
    bool isValidId(const Id& rId);
    bool isTemporaryId(const Id& rId);
    bool isSpecialId(const Id& rId);
    // The same Mii: both valid and not temporary, and equal.
    bool isSameId(const Id& rA, const Id& rB);
    // Every part and value in range, a name, a real birthday; a special Mii must be marked local-only.
    bool isValid(const Character& rMii);

    // The name with its terminator.
    void copyName(const Character& rMii, char16_t* pOut);
    NebulaColor8 favoriteColor(u32 color);
    NebulaColor8 skinColor(const Character& rMii);
}  // namespace Mii
