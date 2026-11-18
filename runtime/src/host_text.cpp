// SPDX-License-Identifier: GPL-3.0-only
// The host's own texts and the language they are shown in (galaxy/host_text.h).
#include "galaxy/host_text.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <iterator>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace galaxy::host_text {
namespace {

#include "host_text_table.inc"

char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

// A tag's parts: its language, its script (four letters) and its region (two letters or three digits). "pt_BR.UTF-8",
// "zh-Hant-TW", "es-419", "sr-Latn@latin".
struct Tag {
    std::string_view language;
    std::string_view script;
    std::string_view region;
};

Tag parse(std::string_view system) noexcept {
    system = system.substr(0, std::min(system.find('.'), system.find('@')));
    Tag tag;
    std::size_t at = 0u;
    for (int part = 0; at <= system.size(); ++part) {
        const std::size_t end = std::min(system.find_first_of("-_", at), system.size());
        const std::string_view piece = system.substr(at, end - at);
        if (part == 0) {
            tag.language = piece;
        } else if (piece.size() == 4u && tag.script.empty() && tag.region.empty()) {
            tag.script = piece;
        } else if ((piece.size() == 2u || piece.size() == 3u) && tag.region.empty()) {
            tag.region = piece;
        }
        if (end == system.size()) break;
        at = end + 1u;
    }
    return tag;
}

const char* english_of(std::string_view region) noexcept {
    for (const std::string_view american : {"", "US", "CA", "PR", "PH"}) {
        if (same(region, american)) return "UsEnglish";
    }
    return "EuEnglish";
}

}  // namespace

const char* language_of(GameLanguage setting, std::string_view system) noexcept {
    const Tag tag = parse(system);
    const bool canada = same(tag.region, "CA");
    const bool spain = same(tag.region, "ES");
    // The setting's language, in the column of the system's region where the language has two.
    switch (setting) {
    case GameLanguage::Japanese: return "JpJapanese";
    case GameLanguage::English: return english_of(same(tag.language, "en") ? tag.region : std::string_view{});
    case GameLanguage::German: return "EuGerman";
    case GameLanguage::French: return canada ? "UsFrench" : "EuFrench";
    case GameLanguage::Spanish: return spain ? "EuSpanish" : "UsSpanish";
    case GameLanguage::Italian: return "EuItalian";
    case GameLanguage::Dutch: return "EuDutch";
    case GameLanguage::Auto: break;
    }
    static constexpr std::array<std::pair<std::string_view, const char*>, 7> kOne = {{{"ja", "JpJapanese"},
                                                                                      {"de", "EuGerman"},
                                                                                      {"it", "EuItalian"},
                                                                                      {"nl", "EuDutch"},
                                                                                      {"pt", "UsPortuguese"},
                                                                                      {"ko", "KrKorean"},
                                                                                      {"en", nullptr}}};
    for (const auto& [code, column] : kOne) {
        if (same(tag.language, code)) return column != nullptr ? column : english_of(tag.region);
    }
    if (same(tag.language, "fr")) return canada ? "UsFrench" : "EuFrench";
    if (same(tag.language, "es")) return spain ? "EuSpanish" : "UsSpanish";
    if (same(tag.language, "zh")) {
        const bool traditional = same(tag.script, "Hant") ||
                                 (tag.script.empty() && (same(tag.region, "TW") || same(tag.region, "HK") ||
                                                         same(tag.region, "MO")));
        return traditional ? "TwTradChinese" : "CnSimpChinese";
    }
    return "UsEnglish";
}

std::string system_tag() {
#if defined(_WIN32)
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) <= 0) {
        return {};
    }
    std::string tag;
    for (const wchar_t* c = name; *c != L'\0'; ++c) tag += static_cast<char>(*c < 0x80 ? *c : '?');
    return tag;
#elif defined(__APPLE__)
    std::string tag;
    if (CFArrayRef const preferred = CFLocaleCopyPreferredLanguages()) {
        if (CFArrayGetCount(preferred) > 0) {
            const auto first = static_cast<CFStringRef>(CFArrayGetValueAtIndex(preferred, 0));
            char buffer[64]{};
            if (CFStringGetCString(first, buffer, sizeof(buffer), kCFStringEncodingUTF8)) tag = buffer;
        }
        CFRelease(preferred);
    }
    return tag;
#else
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* const value = std::getenv(name);
        if (value != nullptr && value[0] != '\0') return value;
    }
    return {};
#endif
}

std::string_view translated(std::string_view english, std::string_view language) noexcept {
    std::size_t column = std::size(kLanguages);
    for (std::size_t i = 0; i < std::size(kLanguages); ++i) {
        if (language == kLanguages[i]) column = i;
    }
    if (column == std::size(kLanguages)) return english;
    for (const auto& row : kRows) {
        if (english == row[0]) return row[column][0] != '\0' ? std::string_view(row[column]) : english;
    }
    return english;
}

std::string format(std::string_view text, std::initializer_list<std::string_view> arguments) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{' && i + 2u < text.size() && text[i + 1u] >= '0' && text[i + 1u] <= '9' && text[i + 2u] == '}') {
            const std::size_t index = static_cast<std::size_t>(text[i + 1u] - '0');
            if (index < arguments.size()) {
                out += *(arguments.begin() + index);
                i += 2u;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

}  // namespace galaxy::host_text
