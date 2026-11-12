#include "Game/System/GameRules.hpp"

namespace {
    const s32 cPlayerHealth = 3;
    const s32 cLifeUpHealth = 6;
    const s32 cMaxCoins = 999;
    const s32 cMaxStarBits = 9999;
    const bool cDodoryuShellOnly = false;
    const bool cBegomanFireProof = false;
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
