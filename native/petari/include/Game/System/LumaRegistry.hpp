#pragma once

// LumaCore registries: every kind of thing a mod can add is a row, and the game's own
// things are rows of the same kinds, so a mod row has every option a game row has. Rows are found by kind and name
// through one hash; a later row of the same kind and name replaces the earlier one (the later mod wins, logged).

#include <nebula_mod_api.h>
#include <nebula_types.h>

namespace lumacore {
    enum RegistryKind : u16 {
        RegistryKind_Object = 0,  // a placeable object class (the factory's names)
        RegistryKind_Form,        // a player form (power-up)
        RegistryKind_PlayerState,
        RegistryKind_Area,
        RegistryKind_Camera,
        RegistryKind_Gravity,
        RegistryKind_Message,     // ACTMES_*
        RegistryKind_Attack,      // ATYPE_*
        RegistryKind_Floor,       // floor codes
        RegistryKind_Comet,
        RegistryKind_Star,
        RegistryKind_Hud,
        RegistryKind_Ride,
        RegistryKind_Item,
        RegistryKind_Projectile,
        RegistryKind_Character,
        RegistryKind_Count
    };

    struct RegistryRow {
        const char* name;           // owned by whoever added the row (the game's tables, a mod's loaded data)
        const void* data;           // the kind's row type (sections 4.x); null for a name-only row
        const NebulaPlugin* mod;    // null: the game's own
        s32 like;                   // the row it copies missing fields from, or -1
        s32 value;                  // the game's number for it (a message's, a floor code, a form id, a table index)
        u16 kind;
        u16 replacedBy;             // 0, or 1 when a later row of the same name took its place
    };

    // The kind's name in luma.json and scripts ("form", "player_state"...), and back (-1 for an unknown one).
    const char* registryKindName(u16 kind);
    s32 registryKind(const char* pName, u32 length);

    // Adds a row: its index. A row with a kind and name already present replaces it (the new index answers finds).
    // The game's rows of every kind are in before the first mod row (and before the first find or listing).
    s32 registryAdd(u16 kind, const char* pName, const void* pData, const NebulaPlugin* pMod, s32 like, s32 value);
    // The row of a kind by name: its index, or -1.
    s32 registryFind(u16 kind, const char* pName);
    // A row by index: null past the last.
    const RegistryRow* registryAt(u32 index);
    u32 registryCount();
    // How many rows of a kind are live, and the i-th of them (in the order they were added): the listing call.
    u32 registryKindCount(u16 kind);
    s32 registryKindAt(u16 kind, u32 i);

    // A generated game row: a name and its number (generated/lumacore_names.cpp).
    struct NameRow {
        const char* name;
        s32 value;
    };
}  // namespace lumacore
