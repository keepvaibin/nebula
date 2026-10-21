// SPDX-License-Identifier: GPL-3.0-only
// A small XML reader for mod descriptions (mod_xml.h).

#include "galaxy/mod_xml.h"

#include <cctype>
#include <cstdlib>

namespace galaxy::mods {
namespace {

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}

    std::optional<XmlNode> document(std::string* error) {
        if (text_.substr(0, 3) == "\xEF\xBB\xBF") pos_ = 3;
        skip_misc();
        if (!at('<')) return fail(error, "no root element");
        std::optional<XmlNode> root = element(error, 0);
        if (!root) return std::nullopt;
        skip_misc();
        if (pos_ != text_.size()) return fail(error, "content after the root element");
        return root;
    }

private:
    std::optional<XmlNode> fail(std::string* error, const char* why) {
        if (error != nullptr) {
            // The line of the failure, for the mod report.
            std::size_t line = 1;
            for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) line += text_[i] == '\n' ? 1u : 0u;
            *error = std::string(why) + " (line " + std::to_string(line) + ")";
        }
        return std::nullopt;
    }

    bool at(char c) const { return pos_ < text_.size() && text_[pos_] == c; }
    bool starts(std::string_view s) const { return text_.substr(pos_, s.size()) == s; }

    void skip_space() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    // Whitespace, comments, declarations and processing instructions between elements.
    void skip_misc() {
        for (;;) {
            skip_space();
            if (starts("<!--")) {
                const std::size_t end = text_.find("-->", pos_ + 4);
                pos_ = end == std::string_view::npos ? text_.size() : end + 3;
            } else if (starts("<?")) {
                const std::size_t end = text_.find("?>", pos_ + 2);
                pos_ = end == std::string_view::npos ? text_.size() : end + 2;
            } else if (starts("<!")) {
                // A DOCTYPE (an internal subset's brackets included).
                int depth = 0;
                while (pos_ < text_.size()) {
                    const char c = text_[pos_++];
                    if (c == '[') ++depth;
                    if (c == ']') --depth;
                    if (c == '>' && depth <= 0) break;
                }
            } else {
                return;
            }
        }
    }

    std::string name() {
        const std::size_t start = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '/' || c == '=') break;
            ++pos_;
        }
        return std::string(text_.substr(start, pos_ - start));
    }

    static std::string decode(std::string_view raw) {
        std::string out;
        out.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '&') {
                out.push_back(raw[i]);
                continue;
            }
            const std::size_t end = raw.find(';', i);
            if (end == std::string_view::npos) {
                out.push_back('&');
                continue;
            }
            const std::string_view entity = raw.substr(i + 1, end - i - 1);
            unsigned long code = 0;
            bool known = true;
            if (entity == "amp") code = '&';
            else if (entity == "lt") code = '<';
            else if (entity == "gt") code = '>';
            else if (entity == "quot") code = '"';
            else if (entity == "apos") code = '\'';
            else if (entity.size() > 1 && entity[0] == '#') {
                const bool hex = entity[1] == 'x' || entity[1] == 'X';
                code = std::strtoul(std::string(entity.substr(hex ? 2 : 1)).c_str(), nullptr, hex ? 16 : 10);
            } else {
                known = false;
            }
            if (!known) {
                out.push_back('&');
                continue;
            }
            // As UTF-8.
            if (code < 0x80) {
                out.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            i = end;
        }
        return out;
    }

    std::optional<XmlNode> element(std::string* error, int depth) {
        if (depth > 256) return fail(error, "elements nested too deeply");
        ++pos_;  // '<'
        XmlNode node;
        node.name = name();
        if (node.name.empty()) return fail(error, "an element without a name");
        for (;;) {
            skip_space();
            if (starts("/>")) {
                pos_ += 2;
                return node;
            }
            if (at('>')) {
                ++pos_;
                break;
            }
            std::string key = name();
            if (key.empty()) return fail(error, "a malformed attribute");
            skip_space();
            if (!at('=')) return fail(error, "an attribute without a value");
            ++pos_;
            skip_space();
            if (!at('"') && !at('\'')) return fail(error, "an unquoted attribute value");
            const char quote = text_[pos_++];
            const std::size_t end = text_.find(quote, pos_);
            if (end == std::string_view::npos) return fail(error, "an unterminated attribute value");
            node.attributes.emplace_back(std::move(key), decode(text_.substr(pos_, end - pos_)));
            pos_ = end + 1;
        }
        // Content: child elements; text, comments and CDATA skipped.
        for (;;) {
            if (pos_ >= text_.size()) return fail(error, "an unterminated element");
            if (starts("</")) {
                pos_ += 2;
                const std::string closing = name();
                skip_space();
                if (!at('>') || closing != node.name) return fail(error, "a mismatched closing tag");
                ++pos_;
                return node;
            }
            if (starts("<![CDATA[")) {
                const std::size_t end = text_.find("]]>", pos_);
                pos_ = end == std::string_view::npos ? text_.size() : end + 3;
            } else if (starts("<!--") || starts("<?") || starts("<!")) {
                skip_misc();
            } else if (at('<')) {
                std::optional<XmlNode> child = element(error, depth + 1);
                if (!child) return std::nullopt;
                node.children.push_back(std::move(*child));
            } else {
                const std::size_t next = text_.find('<', pos_);
                pos_ = next == std::string_view::npos ? text_.size() : next;
            }
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

std::string XmlNode::attribute(std::string_view key, std::string_view fallback) const {
    for (const auto& [k, v] : attributes) {
        if (k == key) return v;
    }
    return std::string(fallback);
}

bool XmlNode::has_attribute(std::string_view key) const {
    for (const auto& entry : attributes) {
        if (entry.first == key) return true;
    }
    return false;
}

unsigned long long XmlNode::attribute_uint(std::string_view key, unsigned long long fallback) const {
    if (!has_attribute(key)) return fallback;
    const std::string value = attribute(key);
    const char* s = value.c_str();
    while (std::isspace(static_cast<unsigned char>(*s))) ++s;
    if (*s == '\0') return fallback;
    const bool hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    return std::strtoull(hex ? s + 2 : s, nullptr, hex ? 16 : 10);
}

long long XmlNode::attribute_int(std::string_view key, long long fallback) const {
    if (!has_attribute(key)) return fallback;
    const std::string value = attribute(key);
    const char* s = value.c_str();
    while (std::isspace(static_cast<unsigned char>(*s))) ++s;
    if (*s == '\0') return fallback;
    const bool negative = *s == '-';
    if (negative || *s == '+') ++s;
    const bool hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    const long long v = static_cast<long long>(std::strtoull(hex ? s + 2 : s, nullptr, hex ? 16 : 10));
    return negative ? -v : v;
}

bool XmlNode::attribute_bool(std::string_view key, bool fallback) const {
    if (!has_attribute(key)) return fallback;
    const std::string value = attribute(key);
    const char c = value.empty() ? '\0' : value[0];
    return c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y';
}

const XmlNode* XmlNode::child(std::string_view child_name) const {
    for (const XmlNode& c : children) {
        if (c.name == child_name) return &c;
    }
    return nullptr;
}

std::optional<XmlNode> parse_xml(std::string_view text, std::string* error) {
    return Reader(text).document(error);
}

}  // namespace galaxy::mods
