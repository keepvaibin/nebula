// LumaCore registries (LumaRegistry.hpp).
#include "Game/System/LumaRegistry.hpp"
#include "Game/System/LumaHost.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_module.h>

namespace {
    const char* const cKindNames[lumacore::RegistryKind_Count] = {
        "object", "form", "player_state", "area", "camera", "gravity", "message", "attack",
        "floor", "comet", "star", "hud", "ride", "item", "projectile", "character",
    };

    lumacore::RegistryRow* sRows = nullptr;
    u32 sRowCount = 0;
    u32 sRowRoom = 0;

    // Open addressing over (kind, name): the slot holds the row index + 1 (0: empty). Kept under half full.
    u32* sSlots = nullptr;
    u32 sSlotCount = 0;  // a power of two

    // Per kind: its live rows in order (a replaced row's place goes to its replacement).
    s32* sKindRows[lumacore::RegistryKind_Count];
    u32 sKindCount[lumacore::RegistryKind_Count];
    u32 sKindRoom[lumacore::RegistryKind_Count];

    u32 hashOf(u16 kind, const char* pName) {
        u32 h = 2166136261u ^ kind;
        for (const char* c = pName; *c != '\0'; c++) {
            h = (h ^ static_cast< u8 >(*c)) * 16777619u;
        }
        return h;
    }

    void insertSlot(u32 row) {
        const lumacore::RegistryRow& r = sRows[row];
        for (u32 i = hashOf(r.kind, r.name) & (sSlotCount - 1);; i = (i + 1) & (sSlotCount - 1)) {
            if (sSlots[i] == 0) {
                sSlots[i] = row + 1;
                return;
            }
        }
    }

    void growSlots() {
        const u32 count = sSlotCount == 0 ? 1024 : sSlotCount * 2;
        delete[] sSlots;
        sSlots = new u32[count];
        for (u32 i = 0; i < count; i++) {
            sSlots[i] = 0;
        }
        sSlotCount = count;
        for (u32 row = 0; row < sRowCount; row++) {
            if (sRows[row].replacedBy == 0) {
                insertSlot(row);
            }
        }
    }

    // The slot holding (kind, name), or -1.
    s32 slotOf(u16 kind, const char* pName) {
        if (sSlotCount == 0) {
            return -1;
        }
        for (u32 i = hashOf(kind, pName) & (sSlotCount - 1);; i = (i + 1) & (sSlotCount - 1)) {
            if (sSlots[i] == 0) {
                return -1;
            }
            const lumacore::RegistryRow& r = sRows[sSlots[i] - 1];
            if (r.kind == kind && MR::isEqualString(r.name, pName)) {
                return static_cast< s32 >(i);
            }
        }
    }
}  // namespace

namespace lumacore {
    const char* registryKindName(u16 kind) {
        return kind < RegistryKind_Count ? cKindNames[kind] : "";
    }

    s32 registryKind(const char* pName, u32 length) {
        for (u16 k = 0; k < RegistryKind_Count; k++) {
            const char* const n = cKindNames[k];
            u32 i = 0;
            while (i < length && n[i] != '\0' && n[i] == pName[i]) {
                i++;
            }
            if (i == length && n[i] == '\0') {
                return k;
            }
        }
        return -1;
    }

    s32 registryAdd(u16 kind, const char* pName, const void* pData, const NebulaPlugin* pMod, s32 like) {
        if (kind >= RegistryKind_Count || pName == nullptr || pName[0] == '\0') {
            return -1;
        }
        if (sRowCount == sRowRoom) {
            const u32 room = sRowRoom == 0 ? 1024 : sRowRoom * 2;
            RegistryRow* grown = new RegistryRow[room];
            for (u32 i = 0; i < sRowCount; i++) {
                grown[i] = sRows[i];
            }
            delete[] sRows;
            sRows = grown;
            sRowRoom = room;
        }
        if ((sRowCount + 1) * 2 > sSlotCount) {
            growSlots();
        }
        const u32 row = sRowCount++;
        sRows[row] = {pName, pData, pMod, like, kind, 0};
        const s32 slot = slotOf(kind, pName);
        if (slot >= 0) {
            // The later row wins: it takes the earlier one's place in finds and in the kind's listing.
            const u32 old = sSlots[slot] - 1;
            sRows[old].replacedBy = 1;
            sSlots[slot] = row + 1;
            nebula_log("mods: %s/%s from %s replaces the one from %s\n", cKindNames[kind], pName,
                       pMod != nullptr ? pMod->id : "the game",
                       sRows[old].mod != nullptr ? sRows[old].mod->id : "the game");
            for (u32 i = 0; i < sKindCount[kind]; i++) {
                if (sKindRows[kind][i] == static_cast< s32 >(old)) {
                    sKindRows[kind][i] = static_cast< s32 >(row);
                }
            }
            return static_cast< s32 >(row);
        }
        insertSlot(row);
        if (sKindCount[kind] == sKindRoom[kind]) {
            const u32 room = sKindRoom[kind] == 0 ? 64 : sKindRoom[kind] * 2;
            s32* grown = new s32[room];
            for (u32 i = 0; i < sKindCount[kind]; i++) {
                grown[i] = sKindRows[kind][i];
            }
            delete[] sKindRows[kind];
            sKindRows[kind] = grown;
            sKindRoom[kind] = room;
        }
        sKindRows[kind][sKindCount[kind]++] = static_cast< s32 >(row);
        return static_cast< s32 >(row);
    }

    s32 registryFind(u16 kind, const char* pName) {
        if (kind >= RegistryKind_Count || pName == nullptr) {
            return -1;
        }
        const s32 slot = slotOf(kind, pName);
        return slot < 0 ? -1 : static_cast< s32 >(sSlots[slot] - 1);
    }

    const RegistryRow* registryAt(u32 index) {
        return index < sRowCount ? &sRows[index] : nullptr;
    }

    u32 registryCount() {
        return sRowCount;
    }

    u32 registryKindCount(u16 kind) {
        return kind < RegistryKind_Count ? sKindCount[kind] : 0;
    }

    s32 registryKindAt(u16 kind, u32 i) {
        return kind < RegistryKind_Count && i < sKindCount[kind] ? sKindRows[kind][i] : -1;
    }

    // Scripts name a row as "<kind>/<name>" ("form/Gold Flower").
    s32 registryIndex(const char* pName) {
        if (pName == nullptr) {
            return -1;
        }
        u32 slash = 0;
        while (pName[slash] != '\0' && pName[slash] != '/') {
            slash++;
        }
        if (pName[slash] != '/') {
            return -1;
        }
        const s32 kind = registryKind(pName, slash);
        return kind < 0 ? -1 : registryFind(static_cast< u16 >(kind), pName + slash + 1);
    }
}  // namespace lumacore
