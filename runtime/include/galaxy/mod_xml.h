// SPDX-License-Identifier: GPL-3.0-only
// A small XML reader for mod descriptions (Riivolution's patch and config files): elements and their attributes;
// text, comments, declarations and CDATA are skipped. The five named entities and numeric references are decoded.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::mods {

struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<XmlNode> children;

    // An attribute's value (`fallback` without it).
    [[nodiscard]] std::string attribute(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] bool has_attribute(std::string_view key) const;
    // As pugixml reads them (Riivolution's files were written for it): decimal, or hex after 0x; true for a first
    // character of 1, t, T, y or Y.
    [[nodiscard]] unsigned long long attribute_uint(std::string_view key, unsigned long long fallback) const;
    [[nodiscard]] long long attribute_int(std::string_view key, long long fallback) const;
    [[nodiscard]] bool attribute_bool(std::string_view key, bool fallback) const;
    // The first child of that name; null without one.
    [[nodiscard]] const XmlNode* child(std::string_view child_name) const;
};

// The document's root element; `error` says why there is none.
[[nodiscard]] std::optional<XmlNode> parse_xml(std::string_view text, std::string* error);

}  // namespace galaxy::mods
