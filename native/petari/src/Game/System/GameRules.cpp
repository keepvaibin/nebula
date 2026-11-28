#include "Game/System/GameRules.hpp"
#include "Game/System/GameDataConst.hpp"
#include "Game/System/GameDataFunction.hpp"

#include "Game/System/GameDataGalaxyStorage.hpp"

namespace {
    const s32 cPlayerHealth = 88;
    const s32 cLifeUpHealth = 99;
    const s32 cMaxCoins = 999;
    const s32 cMaxStarBits = 9999;
    const bool cDodoryuShellOnly = false;
    const bool cBegomanFireProof = false;
//GameDataFunction::addPlayerLeft(99);
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
