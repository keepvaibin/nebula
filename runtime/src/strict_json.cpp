// SPDX-License-Identifier: GPL-3.0-only
// strict_json.h: the parser.
#include "galaxy/strict_json.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace galaxy::json {

Value::Value() = default;
Value::Value(const Value&) = default;
Value::Value(Value&&) noexcept = default;
Value& Value::operator=(const Value&) = default;
Value& Value::operator=(Value&&) noexcept = default;
Value::~Value() = default;

const Value* Value::find(std::string_view key) const noexcept {
    if (type != Object) return nullptr;
    for (const Member& member : fields) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}
    bool parse(Value& out) {
        if (!value(out, 0)) return false;
        space();
        return i_ == s_.size();
    }

private:
    void space() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    bool literal(std::string_view word) {
        if (s_.substr(i_, word.size()) != word) return false;
        i_ += word.size();
        return true;
    }
    bool string(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            const unsigned char c = static_cast<unsigned char>(s_[i_++]);
            if (c == '"') return true;
            if (c < 0x20u || c >= 0x7fu) return false;  // the schema's texts are printable ASCII
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (i_ >= s_.size()) return false;
            const char e = s_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': case 'f': case 'n': case 'r': case 't': return false;  // control characters
                case 'u': {
                    if (i_ + 4u > s_.size()) return false;
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s_[i_++];
                        code = code * 16u + (h >= '0' && h <= '9'   ? static_cast<unsigned>(h - '0')
                                             : h >= 'a' && h <= 'f' ? static_cast<unsigned>(h - 'a' + 10)
                                             : h >= 'A' && h <= 'F' ? static_cast<unsigned>(h - 'A' + 10)
                                                                    : 0x10000u);
                    }
                    if (code < 0x20u || code >= 0x7fu) return false;
                    out.push_back(static_cast<char>(code));
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool number(Value& out) {
        const std::size_t start = i_;
        if (i_ < s_.size() && s_[i_] == '-') ++i_;
        if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) return false;
        if (s_[i_] == '0') {
            ++i_;
        } else {
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        }
        bool integer = true;
        if (i_ < s_.size() && s_[i_] == '.') {
            integer = false;
            ++i_;
            if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) return false;
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            integer = false;
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if (i_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[i_]))) return false;
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        }
        if (i_ - start > 32u) return false;
        const std::string digits(s_.substr(start, i_ - start));
        out.type = Value::Number;
        out.number = std::strtod(digits.c_str(), nullptr);
        out.integer = integer;
        return std::isfinite(out.number);
    }
    bool value(Value& out, int depth) {
        if (depth > 12) return false;
        space();
        if (i_ >= s_.size()) return false;
        const char c = s_[i_];
        if (c == '{') {
            ++i_;
            out.type = Value::Object;
            space();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return true;
            }
            for (;;) {
                space();
                std::string key;
                if (!string(key)) return false;
                for (const auto& field : out.fields) {
                    if (field.first == key) return false;
                }
                space();
                if (i_ >= s_.size() || s_[i_++] != ':') return false;
                Value item;
                if (!value(item, depth + 1)) return false;
                out.fields.push_back(Member{std::move(key), std::move(item)});
                space();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                return i_ < s_.size() && s_[i_++] == '}';
            }
        }
        if (c == '[') {
            ++i_;
            out.type = Value::Array;
            space();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return true;
            }
            for (;;) {
                Value item;
                if (!value(item, depth + 1)) return false;
                out.items.push_back(std::move(item));
                space();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                return i_ < s_.size() && s_[i_++] == ']';
            }
        }
        if (c == '"') {
            out.type = Value::String;
            return string(out.text);
        }
        if (c == 't' || c == 'f') {
            out.type = Value::Bool;
            out.boolean = c == 't';
            return literal(c == 't' ? "true" : "false");
        }
        if (c == 'n') return literal("null");
        return number(out);
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

}  // namespace

bool parse(std::string_view text, Value& out) {
    out = Value{};
    return Parser(text).parse(out);
}

}  // namespace galaxy::json
