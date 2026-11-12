// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/mod_consent.h"

#include <algorithm>
#include <cstdio>

namespace galaxy::mods {
namespace {

constexpr std::uint64_t kFnvBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void fnv(std::uint64_t& hash, std::string_view bytes) {
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= kFnvPrime;
    }
    hash ^= 0u;  // the 0 byte after each part
    hash *= kFnvPrime;
}

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// The stored answer for the declaration as it is now: NotAsked when none, or when it was given for another digest.
Answer answer_of(const ModInfo& mod, const galaxy::luma::Permission& permission, bool* changed) {
    const auto found = mod.permission_answers.find(permission.name);
    if (found == mod.permission_answers.end()) {
        if (changed != nullptr) *changed = false;
        return Answer::NotAsked;
    }
    const bool same = found->second.digest == permission_digest(permission);
    if (changed != nullptr) *changed = !same;
    if (!same) return Answer::NotAsked;
    return found->second.granted ? Answer::Granted : Answer::Declined;
}

PermissionFacts facts_of(const ModInfo& mod, const galaxy::luma::Permission& permission) {
    PermissionFacts out;
    out.name = permission.name;
    out.row = "Permission." + permission.name;
    out.text_row = out.row + ".Text";
    for (const std::string& host : permission.hosts) {
        out.hosts += (out.hosts.empty() ? "" : ", ") + host;
    }
    out.why = permission.why;
    out.answer = answer_of(mod, permission, &out.changed);
    out.required = permission.name == "native" ||
                   std::find(mod.required_permissions.begin(), mod.required_permissions.end(), permission.name) !=
                       mod.required_permissions.end();
    return out;
}

}  // namespace

std::string permission_digest(const galaxy::luma::Permission& permission) {
    std::vector<std::string> hosts;
    hosts.reserve(permission.hosts.size());
    for (const std::string& host : permission.hosts) hosts.push_back(lower_ascii(host));
    std::sort(hosts.begin(), hosts.end());
    std::uint64_t hash = kFnvBasis;
    fnv(hash, permission.name);
    for (const std::string& host : hosts) fnv(hash, host);
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

ModConsent consent_facts(const ModInfo& mod) {
    ModConsent out;
    out.code = !mod.code_token.empty();
    out.code_changed = mod.code_changed;
    out.code_answer = mod.code_allowed ? Answer::Granted : (mod.code_declined ? Answer::Declined : Answer::NotAsked);
    if (mod.luma == nullptr) return out;
    // `native` first when declared (its row is the warning one), then the manifest's order.
    for (const galaxy::luma::Permission& permission : mod.luma->permissions) {
        if (permission.name == "native") out.permissions.push_back(facts_of(mod, permission));
    }
    for (const galaxy::luma::Permission& permission : mod.luma->permissions) {
        if (permission.name != "native") out.permissions.push_back(facts_of(mod, permission));
    }
    return out;
}

bool consent_needed(const ModInfo& mod) {
    if (mod.code_token.empty()) return false;
    // Not now turned the mod off (consent 2.3); on again means the player turned it on, which asks again (consent 2.1
    // "Turned on", test 6402). Off, it waits and is not asked.
    if (mod.code_declined) return mod.enabled;
    if (!mod.code_allowed) return true;
    if (mod.luma == nullptr) return false;
    for (const galaxy::luma::Permission& permission : mod.luma->permissions) {
        if (answer_of(mod, permission, nullptr) == Answer::NotAsked) return true;
    }
    return false;
}

void record_code_answer(ModInfo& mod, bool granted) {
    mod.code_allowed = granted && !mod.code_token.empty();
    mod.code_declined = !granted && !mod.code_token.empty();
    mod.code_changed = false;
}

bool record_answer(ModInfo& mod, std::string_view permission, bool granted) {
    if (mod.luma != nullptr) {
        for (const galaxy::luma::Permission& declared : mod.luma->permissions) {
            if (declared.name == permission) {
                mod.permission_answers[declared.name] = {granted, permission_digest(declared)};
                return true;
            }
        }
    }
    std::fprintf(stderr, "[mods] %s: an answer for the permission %.*s, which it does not declare (a Nebula bug)\n",
                 mod.id.c_str(), static_cast<int>(permission.size()), permission.data());
    return false;
}

std::vector<std::string> granted_permissions(const ModInfo& mod) {
    std::vector<std::string> out;
    if (mod.luma == nullptr) return out;
    for (const galaxy::luma::Permission& permission : mod.luma->permissions) {
        if (answer_of(mod, permission, nullptr) == Answer::Granted) out.push_back(permission.name);
    }
    return out;
}

}  // namespace galaxy::mods
