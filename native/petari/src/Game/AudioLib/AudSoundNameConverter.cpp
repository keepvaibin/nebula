#include "Game/AudioLib/AudSoundNameConverter.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Sound/SoundTable.hpp"
#include <nebula_module.h>

namespace {
    // The prefixes of the game's sound categories: sound effects ("SE_" then two letters), then the other kinds.
    const char* const cSeCategoryPrefix[] = {"SY", "PV", "PM", "BV", "BM", "OJ", "AT", "DM", "EV", "EM", "SV", "SM", "RS"};
    const char* const cOtherCategoryPrefix[] = {"HO", "BG", "MB", "ST"};

    bool hasPrefix(const char* pName, const char* const* pPrefixes, u32 count) {
        for (u32 i = 0; i < count; i++) {
            if (pName[0] == pPrefixes[i][0] && pName[1] == pPrefixes[i][1]) {
                return true;
            }
        }
        return false;
    }

    // A name of one of the game's categories (others are never looked up in the game's table).
    bool isGameCategory(const char* pName) {
        if (pName[0] == 'S' && pName[1] == 'E') {
            return pName[2] != '\0' && hasPrefix(pName + 3, cSeCategoryPrefix, sizeof(cSeCategoryPrefix) / sizeof(cSeCategoryPrefix[0]));
        }
        return hasPrefix(pName, cOtherCategoryPrefix, sizeof(cOtherCategoryPrefix) / sizeof(cOtherCategoryPrefix[0]));
    }
}  // namespace

AudSoundNameConverter::AudSoundNameConverter() {
    init();
}

SoundID AudSoundNameConverter::getSoundID(const char* pName) const {
    if (NebulaMods::sHasSoundEffects) {
        const u32 modded = NebulaMods::soundEffectSlow(pName);
        if (modded != static_cast< u32 >(-1)) {
            return modded;
        }
    }

    // A name of none of the game's categories isn't one of its sounds: a mod's track, or nothing.
    if (isGameCategory(pName)) {
        const u32 id = mIndex.find(pName);
        if (id != MR::NameIndex::cNoValue) {
            return id;
        }
    }
    return NebulaMods::trackId(pName);
}

void AudSoundNameConverter::init() {
    const SoundNameTable* pTable = SoundNameTable::getInstance();
    u32 count = 0;
    for (u8 section = 0; section < 3; section++) {
        for (u8 group = 0; group < pTable->getNumGroups_inSection(section); group++) {
            count += pTable->getNumItems_inGroup(section, group);
        }
    }

    mIndex.reset(count);
    u32 heldTwice = 0;
    for (u8 section = 0; section < 3; section++) {
        for (u8 group = 0; group < pTable->getNumGroups_inSection(section); group++) {
            for (u16 i = 0; i < pTable->getNumItems_inGroup(section, group); i++) {
                const SoundID id(section, group, i);
                if (!mIndex.add(pTable->getName(id), id)) {
                    heldTwice++;
                }
            }
        }
    }
#if defined(NEBULA_DEV)
    if (heldTwice != 0) {
        nebula_log("sound names: %u of %u held twice (the first is used)\n", heldTwice, count);
    }
#else
    (void)heldTwice;
#endif
}
