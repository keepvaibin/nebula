// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Strict JSON (RFC 8259) for the files Nebula writes itself and reads back: the benchmark's results and the
// diagnostics report's schema check. One value, no duplicate keys, nesting at most 12 deep, and texts of printable
// ASCII only (a control character, an escape for one, or a byte above 0x7E is refused), so whatever passes is safe
// to show or send as it is.

#include <string>
#include <string_view>
#include <vector>

namespace galaxy::json {

// An object's entry. Not std::pair<std::string, Value>: a pair may not be instantiated while Value is incomplete
// (libstdc++ checks it inside Value's own definition), where a vector of a declared struct may.
struct Member;

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0.0;
    bool integer = false;  // no fraction, no exponent
    std::string text;
    std::vector<Value> items;
    std::vector<Member> fields;

    Value();
    Value(const Value&);
    Value(Value&&) noexcept;
    Value& operator=(const Value&);
    Value& operator=(Value&&) noexcept;
    ~Value();

    // An object's member named `key`; null when this is not an object or has no such member.
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;
    // The number, or `otherwise` when this is not a number.
    [[nodiscard]] double number_or(double otherwise) const noexcept { return type == Number ? number : otherwise; }
};

struct Member {
    std::string first;
    Value second;
};

// The whole of `text` as one value; false (and `out` unspecified) for anything else.
[[nodiscard]] bool parse(std::string_view text, Value& out);

}  // namespace galaxy::json
