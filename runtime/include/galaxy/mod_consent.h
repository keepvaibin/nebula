// SPDX-License-Identifier: GPL-3.0-only
// A mod's consent facts (contracts/consent.md, facts half): its code and the permissions its luma.json declares, with
// the player's stored answers. The Mods page's question (J's screen half) reads and records through this; the start
// passes granted_permissions to luma_load_mod. A ModInfo is changed only on the thread that owns the mod list; the
// caller saves the list with save_mod_settings after recording, as the Mods page does for every setting.
#pragma once

#include "galaxy/luma.h"
#include "galaxy/mods.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::mods {

enum class Answer : std::uint8_t { NotAsked, Granted, Declined };

struct PermissionFacts {
    std::string name;      // "net.internet"
    std::string row;       // "Permission.net.internet": the short name's row
    std::string text_row;  // "Permission.net.internet.Text": the line's row; {hosts} to fill
    std::string hosts;     // net.internet: the hosts joined by ", "; "*" alone -> the row Permission.net.internet.Any
    std::string why;       // the mod's own line
    Answer answer = Answer::NotAsked;
    bool changed = false;   // answered before, for a declaration that differs now (asked again: plan 6.4)
    bool required = false;  // the entry library imports its service without LUMA_OPTIONAL; `native` always
};

// One question per mod: its code and its permissions together.
struct ModConsent {
    bool code = false;          // it has code for this platform
    bool code_changed = false;  // answered before, for other libraries than these
    Answer code_answer = Answer::NotAsked;
    std::vector<PermissionFacts> permissions;  // in the manifest's order, `native` first when declared
};

// 16 lowercase hex digits: FNV-1a 64 over the name, a 0 byte, then each host lowercased (A-Z), sorted in byte order,
// each followed by a 0 byte (consent 1.2). `why` is not in it.
[[nodiscard]] std::string permission_digest(const galaxy::luma::Permission& permission);

// The mod's code and declared permissions with their answers.
[[nodiscard]] ModConsent consent_facts(const ModInfo& mod);
// True when it has code whose answer is NotAsked for its current token, or code Granted with a declared permission
// NotAsked (changed ones included): the screen asks before the mod starts. Never for a mod without code; for code the
// player said Not now to, only while the mod is on (Not now turns it off; turning it on asks again).
[[nodiscard]] bool consent_needed(const ModInfo& mod);
// Records the player's answer for the mod's code (its current token).
void record_code_answer(ModInfo& mod, bool granted);
// Records the player's answer for one permission, for its declaration as shown now. False: the mod does not declare it
// (a caller bug, logged).
bool record_answer(ModInfo& mod, std::string_view permission, bool granted);
// The names to pass to luma_load_mod: Granted answers for the declarations as they are now, in the manifest's order.
[[nodiscard]] std::vector<std::string> granted_permissions(const ModInfo& mod);

}  // namespace galaxy::mods
