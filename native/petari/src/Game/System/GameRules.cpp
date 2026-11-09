#include "Game/System/GameRules.hpp"
#include "Game/Util/Tunable.hpp"

namespace {
    NEBULA_TUNABLE(s32, cPlayerHealth, 3);
    NEBULA_TUNABLE(s32, cLifeUpHealth, 6);
    NEBULA_TUNABLE(s32, cMaxCoins, 999);
    NEBULA_TUNABLE(s32, cMaxStarBits, 9999);
    NEBULA_TUNABLE(bool, cDodoryuShellOnly, false);
    NEBULA_TUNABLE(bool, cBegomanFireProof, false);
};  // namespace

namespace GameRules {
    s32 playerHealth() {
        return ::cPlayerHealth;
    }

    s32 lifeUpHealth() {
        return ::cLifeUpHealth;
    }

    s32 maxCoins() {
        return ::cMaxCoins;
    }

    s32 maxStarBits() {
        return ::cMaxStarBits;
    }

    bool dodoryuShellOnly() {
        return ::cDodoryuShellOnly;
    }

    bool begomanFireProof() {
        return ::cBegomanFireProof;
    }
}  // namespace GameRules
