// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/luma.h"

#include <cstdio>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <variant>

namespace galaxy::luma {
namespace {

// JSON as RFC 8259 has it; what a person might get wrong gives no value, never a crash. Nesting stops at a depth no manifest
// needs.
struct Value;
using Object = std::vector<std::pair<std::string, Value>>;
using Array = std::vector<Value>;

struct Value {
    std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> data;

    [[nodiscard]] const std::string* string() const noexcept { return std::get_if<std::string>(&data); }
    [[nodiscard]] const double* number() const noexcept { return std::get_if<double>(&data); }
    [[nodiscard]] const Array* array() const noexcept {
        const auto* held = std::get_if<std::shared_ptr<Array>>(&data);
        return held != nullptr ? held->get() : nullptr;
    }
    [[nodiscard]] const Object* object() const noexcept {
        const auto* held = std::get_if<std::shared_ptr<Object>>(&data);
        return held != nullptr ? held->get() : nullptr;
    }
    [[nodiscard]] const Value* get(std::string_view key) const noexcept {
        const Object* fields = object();
        if (fields == nullptr) return nullptr;
        for (const auto& [name, value] : *fields) {
            if (name == key) return &value;
        }
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {
        if (text_.size() >= 3u && text_.substr(0, 3) == "\xEF\xBB\xBF") text_.remove_prefix(3);
    }

    [[nodiscard]] std::optional<Value> whole() {
        std::optional<Value> value = parse(0);
        space();
        if (!value || at_ != text_.size()) return std::nullopt;
        return value;
    }

private:
    static constexpr int kDeepest = 32;

    void space() noexcept {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\r' || text_[at_] == '\n')) ++at_;
    }

    bool word(std::string_view expected) noexcept {
        if (text_.substr(at_, expected.size()) != expected) return false;
        at_ += expected.size();
        return true;
    }

    std::optional<std::uint32_t> hex4() noexcept {
        if (at_ + 4u > text_.size()) return std::nullopt;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[at_++];
            value <<= 4u;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return std::nullopt;
        }
        return value;
    }

    static void put_utf8(std::string& out, std::uint32_t c) {
        if (c < 0x80u) {
            out += static_cast<char>(c);
        } else if (c < 0x800u) {
            out += static_cast<char>(0xC0u | (c >> 6u));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        } else if (c < 0x10000u) {
            out += static_cast<char>(0xE0u | (c >> 12u));
            out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        } else {
            out += static_cast<char>(0xF0u | (c >> 18u));
            out += static_cast<char>(0x80u | ((c >> 12u) & 0x3Fu));
            out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        }
    }

    std::optional<std::string> string() {
        if (at_ >= text_.size() || text_[at_] != '"') return std::nullopt;
        ++at_;
        std::string out;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20u) return std::nullopt;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) return std::nullopt;
            const char escape = text_[at_++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::optional<std::uint32_t> c1 = hex4();
                if (!c1) return std::nullopt;
                std::uint32_t code = *c1;
                if (code >= 0xD800u && code < 0xDC00u) {
                    if (!word("\\u")) return std::nullopt;
                    const std::optional<std::uint32_t> c2 = hex4();
                    if (!c2 || *c2 < 0xDC00u || *c2 >= 0xE000u) return std::nullopt;
                    code = 0x10000u + ((code - 0xD800u) << 10u) + (*c2 - 0xDC00u);
                } else if (code >= 0xDC00u && code < 0xE000u) {
                    return std::nullopt;
                }
                put_utf8(out, code);
                break;
            }
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<Value> parse(int depth) {
        if (depth > kDeepest) return std::nullopt;
        space();
        if (at_ >= text_.size()) return std::nullopt;
        const char c = text_[at_];
        Value value;
        if (c == '{') {
            ++at_;
            auto fields = std::make_shared<Object>();
            space();
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
            } else {
                while (true) {
                    space();
                    std::optional<std::string> key = string();
                    if (!key) return std::nullopt;
                    space();
                    if (at_ >= text_.size() || text_[at_++] != ':') return std::nullopt;
                    std::optional<Value> field = parse(depth + 1);
                    if (!field) return std::nullopt;
                    fields->emplace_back(std::move(*key), std::move(*field));
                    space();
                    if (at_ >= text_.size()) return std::nullopt;
                    const char next = text_[at_++];
                    if (next == '}') break;
                    if (next != ',') return std::nullopt;
                }
            }
            value.data = std::move(fields);
            return value;
        }
        if (c == '[') {
            ++at_;
            auto items = std::make_shared<Array>();
            space();
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
            } else {
                while (true) {
                    std::optional<Value> item = parse(depth + 1);
                    if (!item) return std::nullopt;
                    items->push_back(std::move(*item));
                    space();
                    if (at_ >= text_.size()) return std::nullopt;
                    const char next = text_[at_++];
                    if (next == ']') break;
                    if (next != ',') return std::nullopt;
                }
            }
            value.data = std::move(items);
            return value;
        }
        if (c == '"') {
            std::optional<std::string> s = string();
            if (!s) return std::nullopt;
            value.data = std::move(*s);
            return value;
        }
        if (word("true")) {
            value.data = true;
            return value;
        }
        if (word("false")) {
            value.data = false;
            return value;
        }
        if (word("null")) return value;
        const std::size_t start = at_;
        if (at_ < text_.size() && text_[at_] == '-') ++at_;
        const std::size_t digits = at_;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
        if (at_ == digits) return std::nullopt;
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            const std::size_t fraction = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            if (at_ == fraction) return std::nullopt;
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) ++at_;
            const std::size_t power = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            if (at_ == power) return std::nullopt;
        }
        value.data = std::strtod(std::string(text_.substr(start, at_ - start)).c_str(), nullptr);
        return value;
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

// A script's name: letters, digits, _ and -, in folders split by /; never empty, absolute or climbing out.
bool script_name_ok(std::string_view name) {
    if (name.empty() || name.size() > 128 || name.front() == '/' || name.back() == '/') return false;
    char last = '/';
    for (const char c : name) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!plain && !(c == '/' && last != '/')) return false;
        last = c;
    }
    return true;
}

std::string text_field(const Value& value, std::string_view key) {
    const Value* field = value.get(key);
    const std::string* text = field != nullptr ? field->string() : nullptr;
    return text != nullptr ? *text : std::string();
}

// Lower-case letters, digits and dashes, and none of the names Windows keeps for itself: it names a folder on every device.
bool valid_id(std::string_view id) {
    if (id.empty() || id.size() > 64u || id.front() == '-' || id.back() == '-') return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    if (id == "con" || id == "prn" || id == "aux" || id == "nul") return false;
    if (id.size() == 4u && (id.starts_with("com") || id.starts_with("lpt")) && id[3] >= '0' && id[3] <= '9') return false;
    return true;
}

Text read_text(const Value* value) {
    Text out;
    if (value == nullptr) return out;
    if (const std::string* text = value->string()) {
        out.fallback = *text;
        return out;
    }
    if (const Object* fields = value->object()) {
        for (const auto& [language, text] : *fields) {
            if (const std::string* s = text.string()) {
                if (language == "default") out.fallback = *s;
                else out.languages[language] = *s;
            }
        }
    }
    return out;
}

std::vector<std::string> string_list(const Value* value) {
    std::vector<std::string> out;
    const Array* items = value != nullptr ? value->array() : nullptr;
    if (items == nullptr) return out;
    for (const Value& item : *items) {
        if (const std::string* text = item.string()) out.push_back(*text);
    }
    return out;
}

std::vector<Dependency> dependency_list(const Value* value, std::string_view version_key) {
    std::vector<Dependency> out;
    const Array* items = value != nullptr ? value->array() : nullptr;
    if (items == nullptr) return out;
    for (const Value& item : *items) {
        Dependency dependency{text_field(item, "id"), text_field(item, version_key)};
        if (!dependency.id.empty()) out.push_back(std::move(dependency));
    }
    return out;
}

When read_when(const Value* value) {
    When when;
    const Object* fields = value != nullptr ? value->object() : nullptr;
    if (fields == nullptr) return when;
    for (const auto& [key, field] : *fields) {
        if (key == "version") {
            when.versions = string_list(&field);
        } else if (key == "language") {
            when.languages = string_list(&field);
        } else if (key == "platform") {
            when.platforms = string_list(&field);
        } else if (key == "option") {
            if (const Object* options = field.object()) {
                for (const auto& [option, choices] : *options) when.options[option] = string_list(&choices);
            }
        } else if (key == "mods") {
            when.mods = string_list(&field);
        } else if (key == "not" || key == "any") {
            std::vector<When>& list = key == "not" ? when.not_any : when.any_of;
            if (const Array* items = field.array()) {
                for (const Value& item : *items) list.push_back(read_when(&item));
            } else {
                list.push_back(read_when(&field));
            }
            for (const When& one : list) when.unknown = when.unknown || one.unknown;
        } else {
            when.unknown = true;
        }
    }
    return when;
}

// A path that stays in the mod's folder. A control character (a NUL ends the path where the system reads it) or a part
// of dots and spaces past "." (Windows drops a part's last dots and spaces: ".. " is "..") is refused too.
bool path_stays_inside(const std::string& path) {
    if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) return false;
    if (std::any_of(path.begin(), path.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20u || c == 0x7F; })) return false;
    std::size_t at = 0;
    while (at <= path.size()) {
        std::size_t end = path.find_first_of("/\\", at);
        if (end == std::string::npos) end = path.size();
        const std::string_view part = std::string_view(path).substr(at, end - at);
        if (!part.empty() && part != "." && part.find_first_not_of(". ") == std::string_view::npos) return false;
        at = end + 1u;
    }
    return true;
}

bool contains(const std::vector<std::string>& list, std::string_view wanted) {
    return std::find(list.begin(), list.end(), wanted) != list.end();
}

// A manifest's number as a whole one: a fraction is cut and a value past the type's range stops at its end (a plain
// cast of such a double is undefined).
int whole_int(double value) {
    if (!(value == value)) return 0;
    if (value >= 2147483647.0) return std::numeric_limits<int>::max();
    if (value <= -2147483648.0) return std::numeric_limits<int>::min();
    return static_cast<int>(value);
}

// The same as 32 bits that wrap: a negative number counts back from 2^32.
std::uint32_t whole_u32(double value) {
    if (!(value == value)) return 0u;
    if (value >= 4294967295.0) return 0xFFFFFFFFu;
    if (value >= 0.0) return static_cast<std::uint32_t>(value);
    return static_cast<std::uint32_t>(whole_int(value));
}

bool read_rows(const Value& from, Rows& out) {
    if (const Value* story = from.get("story"); story != nullptr && story->object() != nullptr) {
        for (const auto& [key, value] : *story->object()) {
            if (const std::string* words = value.string()) {
                out.story[key] = *words;
            } else if (const double* number = value.number()) {
                out.story[key] = std::to_string(whole_int(*number));
            } else if (const bool* flag = std::get_if<bool>(&value.data)) {
                out.story[key] = *flag ? "on" : "off";
            }
        }
    }
    if (const Value* messages = from.get("text"); messages != nullptr && messages->object() != nullptr) {
        for (const auto& [id, words] : *messages->object()) {
            Text read = read_text(&words);
            // "like" names a game message, not a language.
            if (const auto like = read.languages.find("like"); like != read.languages.end()) {
                out.text_like[id] = like->second;
                read.languages.erase(like);
            }
            out.text[id] = std::move(read);
        }
    }
    if (const Value* galaxies = from.get("galaxies"); galaxies != nullptr && galaxies->array() != nullptr) {
        for (const Value& entry : *galaxies->array()) {
            Galaxy galaxy;
            galaxy.name = text_field(entry, "name");
            galaxy.map_pane = text_field(entry, "map_pane");
            if (galaxy.name.empty()) return false;
            if (const Value* conditions = entry.get("open_conditions"); conditions != nullptr && conditions->array() != nullptr) {
                for (const Value& condition : *conditions->array()) {
                    if (condition.string() != nullptr && galaxy.open_conditions.size() < 3u) galaxy.open_conditions.push_back(*condition.string());
                }
            }
            if (const Value* stars = entry.get("stars_to_open"); stars != nullptr && stars->number() != nullptr) galaxy.stars_to_open = whole_int(*stars->number());
            if (const Value* dome = entry.get("dome"); dome != nullptr && dome->number() != nullptr) galaxy.dome = whole_int(*dome->number());
            if (galaxy.stars_to_open < 0 || galaxy.stars_to_open > 255 || galaxy.dome < 0 || galaxy.dome > 255) return false;
            if (const Value* hungry = entry.get("hungry_luma"); hungry != nullptr && hungry->object() != nullptr) {
                const Value* slot = hungry->get("slot");
                const Value* bits = hungry->get("star_bits");
                if (slot == nullptr || slot->number() == nullptr || bits == nullptr || bits->number() == nullptr) return false;
                galaxy.hungry_slot = whole_int(*slot->number());
                galaxy.hungry_star_bits = whole_int(*bits->number());
            }
            if (const Value* place = entry.get("map_position"); place != nullptr && place->array() != nullptr && place->array()->size() == 2u) {
                const Value& x = (*place->array())[0];
                const Value& y = (*place->array())[1];
                if (x.number() == nullptr || y.number() == nullptr) return false;
                galaxy.has_map_position = true;
                galaxy.map_x = static_cast<float>(*x.number());
                galaxy.map_y = static_cast<float>(*y.number());
            }
            out.galaxies.push_back(std::move(galaxy));
        }
    }
    if (const Value* music = from.get("music"); music != nullptr && music->array() != nullptr) {
        for (const Value& entry : *music->array()) {
            Music track;
            track.name = text_field(entry, "name");
            track.file = text_field(entry, "file");
            if (track.name.empty() || track.file.empty()) return false;
            if (const Value* stages = entry.get("stages"); stages != nullptr && stages->array() != nullptr) {
                for (const Value& row : *stages->array()) {
                    MusicPlace place;
                    place.stage = text_field(row, "stage");
                    if (place.stage.empty()) return false;
                    if (const Value* scenario = row.get("scenario"); scenario != nullptr && scenario->number() != nullptr) place.scenario = whole_int(*scenario->number());
                    if (const Value* area = row.get("change_area"); area != nullptr && area->number() != nullptr) place.change_area = whole_int(*area->number());
                    track.places.push_back(std::move(place));
                }
            }
            out.music.push_back(std::move(track));
        }
    }
    if (const Value* flags = from.get("event_flags"); flags != nullptr && flags->array() != nullptr) {
        for (const Value& entry : *flags->array()) {
            EventFlag flag;
            flag.name = text_field(entry, "name");
            if (flag.name.empty()) return false;
            const auto whole = [&entry](const char* key) {
                const Value* value = entry.get(key);
                return value != nullptr && value->number() != nullptr ? whole_int(*value->number()) : 0;
            };
            flag.kind = whole("kind");
            flag.a = whole("a");
            flag.b = whole("b");
            flag.first = text_field(entry, "first");
            flag.second = text_field(entry, "second");
            out.event_flags.push_back(std::move(flag));
        }
    }
    if (const Value* schedule = from.get("comet_schedule"); schedule != nullptr && schedule->array() != nullptr) {
        for (const Value& entry : *schedule->array()) {
            CometSchedule dome;
            if (const Value* number = entry.get("dome"); number != nullptr && number->number() != nullptr) dome.dome = whole_int(*number->number());
            if (dome.dome < 1) return false;
            if (const Value* comets = entry.get("comets"); comets != nullptr && comets->array() != nullptr) {
                for (const Value& comet : *comets->array()) {
                    std::string galaxy = text_field(comet, "galaxy");
                    if (galaxy.empty()) return false;
                    const Value* coins = comet.get("coins");
                    dome.comets.emplace_back(std::move(galaxy), coins != nullptr && coins->number() != nullptr ? whole_int(*coins->number()) : 0);
                }
            }
            if (dome.comets.empty()) return false;
            out.comet_schedule.push_back(std::move(dome));
        }
    }
    return true;
}

}  // namespace

const std::string& Text::in(std::string_view language) const {
    if (const auto it = languages.find(std::string(language)); it != languages.end()) return it->second;
    const std::size_t dash = language.find('-');
    if (dash != std::string_view::npos) {
        if (const auto it = languages.find(std::string(language.substr(0, dash))); it != languages.end()) return it->second;
    }
    return fallback;
}

std::optional<Manifest> parse_manifest(std::string_view text) {
    const std::optional<Value> root = Parser(text).whole();
    if (!root || root->object() == nullptr) return std::nullopt;
    Manifest out;
    const Value* format = root->get("format");
    if (format == nullptr || format->number() == nullptr || *format->number() < 1.0) return std::nullopt;
    out.format = whole_u32(*format->number());
    // A newer format's other fields may mean anything: the number is all a reader can say of it.
    if (out.format > kFormat) return out;
    out.id = text_field(*root, "id");
    if (!valid_id(out.id)) return std::nullopt;
    out.name = read_text(root->get("name"));
    out.version = text_field(*root, "version");
    out.game = text_field(*root, "game");
    if (out.id.empty() || out.name.fallback.empty() || out.game.empty()) return std::nullopt;
    out.versions = string_list(root->get("versions"));
    // "regions": "any", or the discs it is made for. A name Nebula doesn't know refuses the file (LumaLink's check
    // names it first).
    if (const Value* regions = root->get("regions"); regions != nullptr) {
        const std::string* any = std::get_if<std::string>(&regions->data);
        if (any != nullptr) {
            if (*any != "any") return std::nullopt;
        } else {
            out.regions = string_list(regions);
            if (out.regions.empty()) return std::nullopt;
            for (const std::string& region : out.regions) {
                if (region != "US" && region != "PAL" && region != "JP" && region != "KR") return std::nullopt;
            }
        }
    }
    out.nebula = text_field(*root, "nebula");
    out.requires_features = string_list(root->get("requires"));
    out.authors = string_list(root->get("authors"));
    out.description = read_text(root->get("description"));
    out.license = text_field(*root, "license");
    out.homepage = text_field(*root, "homepage");
    out.icon = text_field(*root, "icon");
    if (!out.icon.empty() && !path_stays_inside(out.icon)) out.icon.clear();
    for (std::string& picture : string_list(root->get("pictures"))) {
        if (out.pictures.size() < kMaxPictures && !picture.empty() && path_stays_inside(picture)) out.pictures.push_back(std::move(picture));
    }
    if (const Value* list = root->get("achievements"); list != nullptr && list->array() != nullptr) {
        for (const Value& entry : *list->array()) {
            Achievement achievement;
            achievement.id = text_field(entry, "id");
            achievement.name = read_text(entry.get("name"));
            achievement.description = read_text(entry.get("description"));
            if (const Value* hidden = entry.get("hidden"); hidden != nullptr) {
                if (const bool* flag = std::get_if<bool>(&hidden->data)) achievement.hidden = *flag;
            }
            if (const Value* when = entry.get("when"); when != nullptr && when->object() != nullptr) {
                achievement.event = text_field(*when, "event");
                achievement.event_name = text_field(*when, "name");
                if (const Value* value = when->get("value"); value != nullptr && value->number() != nullptr) {
                    achievement.has_value = true;
                    achievement.event_value = whole_int(*value->number());
                }
            }
            const bool taken = std::any_of(out.achievements.begin(), out.achievements.end(),
                                           [&](const Achievement& other) { return other.id == achievement.id; });
            if (valid_id(achievement.id) && !achievement.name.fallback.empty() && !taken && out.achievements.size() < kMaxAchievements) {
                out.achievements.push_back(std::move(achievement));
            }
        }
    }
    out.needs = dependency_list(root->get("needs"), "version");
    out.conflicts = dependency_list(root->get("conflicts"), "below");
    out.load_after = string_list(root->get("load_after"));
    out.load_before = string_list(root->get("load_before"));
    if (const Value* options = root->get("options"); options != nullptr && options->array() != nullptr) {
        for (const Value& entry : *options->array()) {
            Option option;
            option.id = text_field(entry, "id");
            option.name = read_text(entry.get("name"));
            option.description = read_text(entry.get("description"));
            option.fallback = text_field(entry, "default");
            if (const Value* choices = entry.get("choices"); choices != nullptr && choices->array() != nullptr) {
                for (const Value& choice : *choices->array()) {
                    Choice read{text_field(choice, "id"), read_text(choice.get("name"))};
                    if (!read.id.empty()) option.choices.push_back(std::move(read));
                }
            }
            // A choice for each number of a range: { "from": 1, "to": 20, "step": 1 }. Its id is the number; a range from
            // a larger number to a smaller one counts down.
            if (const Value* range = entry.get("range"); range != nullptr && range->object() != nullptr) {
                const auto number = [range](const char* key, double missing) {
                    const Value* value = range->get(key);
                    return value != nullptr && value->number() != nullptr ? *value->number() : missing;
                };
                const double from = number("from", 0.0);
                const double to = number("to", from);
                const double step = number("step", 1.0);
                const double span = to >= from ? to - from : from - to;
                const double toward = to >= from ? step : -step;
                if (step > 0.0 && span / step <= 10000.0) {
                    // Counted, so that a step too small for the numbers' size (1e300 + 1) cannot loop for ever.
                    const std::size_t count = static_cast<std::size_t>(span / step + 0.001) + 1u;
                    for (std::size_t i = 0; i < count; ++i) {
                        const double at = from + static_cast<double>(i) * toward;
                        char id[32];
                        std::snprintf(id, sizeof(id), "%g", at);
                        Choice choice;
                        choice.id = id;
                        choice.name.fallback = id;
                        option.choices.push_back(std::move(choice));
                    }
                }
                if (const Value* start = entry.get("default"); start != nullptr && start->number() != nullptr) {
                    char id[32];
                    std::snprintf(id, sizeof(id), "%g", *start->number());
                    option.fallback = id;
                }
            }
            if (option.id.empty() || option.choices.empty()) continue;
            const bool known = std::any_of(option.choices.begin(), option.choices.end(), [&](const Choice& c) { return c.id == option.fallback; });
            if (!known) option.fallback = option.choices.front().id;
            out.options.push_back(std::move(option));
        }
    }
    const Value* layers = root->get("layers");
    if (layers == nullptr || layers->array() == nullptr) return std::nullopt;
    for (const Value& entry : *layers->array()) {
        Layer layer;
        layer.path = text_field(entry, "path");
        if (!layer.path.empty() && !path_stays_inside(layer.path)) return std::nullopt;
        if (const Value* changes = entry.get("object_changes"); changes != nullptr && changes->array() != nullptr) {
            for (const Value& row : *changes->array()) {
                ObjectChange change;
                change.object = text_field(row, "object");
                if (change.object.empty()) return std::nullopt;
                change.becomes = text_field(row, "becomes");
                const std::string where = text_field(row, "in");
                if (where == "display_models") change.where = 1;
                else if (where == "treasure_boxes") change.where = 2;
                else if (!where.empty() && where != "placed") return std::nullopt;
                layer.object_changes.push_back(std::move(change));
            }
        }
        if (const Value* limits = entry.get("comet_time_limits"); limits != nullptr && limits->array() != nullptr) {
            for (const Value& row : *limits->array()) {
                CometLimit limit;
                limit.galaxy = text_field(row, "galaxy");
                if (limit.galaxy.empty()) return std::nullopt;
                const Value* mission = row.get("mission");
                const Value* seconds = row.get("seconds");
                if (mission == nullptr || mission->number() == nullptr || seconds == nullptr || seconds->number() == nullptr) return std::nullopt;
                limit.mission = whole_int(*mission->number());
                limit.seconds = whole_int(*seconds->number());
                layer.comet_time_limits.push_back(std::move(limit));
            }
        }
        if (const Value* stages = entry.get("stages"); stages != nullptr && stages->array() != nullptr) {
            for (const Value& row : *stages->array()) {
                StageRule rule;
                rule.stage = text_field(row, "stage");
                if (rule.stage.empty()) return std::nullopt;
                if (const Value* scenario = row.get("scenario"); scenario != nullptr && scenario->number() != nullptr) rule.scenario = whole_int(*scenario->number());
                rule.acts_as = string_list(row.get("acts_as"));
                if (const Value* own = row.get("own_rules"); own != nullptr) {
                    if (const bool* flag = std::get_if<bool>(&own->data)) rule.own_rules = *flag;
                }
                layer.stages.push_back(std::move(rule));
            }
        }
        if (const Value* constants = entry.get("player_constants"); constants != nullptr && constants->object() != nullptr) {
            // { "mario": {...}, "luigi": {...}, "both": {...} }, each a name with a number or a list of numbers.
            for (const auto& [who, fields] : *constants->object()) {
                if (fields.object() == nullptr || (who != "mario" && who != "luigi" && who != "both")) return std::nullopt;
                for (const auto& [name, value] : *fields.object()) {
                    const auto add = [&, name = name](int index, double number) {
                        // Past a float's range is infinite (a cast would be undefined): the start names it and sets nothing.
                        const float held = std::fabs(number) <= static_cast<double>(std::numeric_limits<float>::max())
                                               ? static_cast<float>(number)
                                               : (number > 0.0 ? 1.0f : -1.0f) * std::numeric_limits<float>::infinity();
                        if (who != "luigi") layer.player_constants.push_back({0, name, index, held, {}});
                        if (who != "mario") layer.player_constants.push_back({1, name, index, held, {}});
                    };
                    if (const double* number = value.number()) {
                        add(0, *number);
                    } else if (const Value* option = value.get("option"); option != nullptr && option->string() != nullptr) {
                        if (who != "luigi") layer.player_constants.push_back({0, name, 0, 0.0f, *option->string()});
                        if (who != "mario") layer.player_constants.push_back({1, name, 0, 0.0f, *option->string()});
                    } else if (const Array* list = value.array()) {
                        for (std::size_t i = 0; i < list->size(); ++i) {
                            // A null keeps the game's number at that place.
                            if (const double* item = (*list)[i].number()) add(static_cast<int>(i), *item);
                        }
                    }
                }
            }
        }
        if (const Value* tunables = entry.get("tunables"); tunables != nullptr) {
            // { "GameRules.cPlayerHealth": 6, "GameRules.cDodoryuShellOnly": true, "GameRules.cMaxCoins": {"option": "coins"}, "JetTurtle.sGravityLevel": [0.02, null, 0.01] }
            if (tunables->object() == nullptr) return std::nullopt;
            for (const auto& [key, value] : *tunables->object()) {
                if (key.find('.') == std::string::npos) return std::nullopt;
                if (const double* number = value.number()) {
                    layer.tunables.push_back({key, *number, {}});
                } else if (const bool* flag = std::get_if<bool>(&value.data)) {
                    layer.tunables.push_back({key, *flag ? 1.0 : 0.0, {}});
                } else if (const Value* option = value.get("option"); option != nullptr && option->string() != nullptr) {
                    layer.tunables.push_back({key, 0.0, *option->string()});
                } else if (const Array* list = value.array()) {
                    // A list's places; a null keeps the game's number there.
                    for (std::size_t i = 0; i < list->size(); ++i) {
                        if (const double* item = (*list)[i].number()) {
                            layer.tunables.push_back({key + "[" + std::to_string(i) + "]", *item, {}});
                        } else if (const bool* item_flag = std::get_if<bool>(&(*list)[i].data)) {
                            layer.tunables.push_back({key + "[" + std::to_string(i) + "]", *item_flag ? 1.0 : 0.0, {}});
                        } else if (!std::holds_alternative<std::monostate>((*list)[i].data)) {
                            return std::nullopt;
                        }
                    }
                } else {
                    return std::nullopt;
                }
            }
        }
        if (const Value* sounds = entry.get("sound_effects"); sounds != nullptr && sounds->array() != nullptr) {
            for (const Value& row : *sounds->array()) {
                SoundEffect sound{text_field(row, "name"), text_field(row, "file")};
                if (sound.name.empty() || sound.file.empty()) return std::nullopt;
                layer.sound_effects.push_back(std::move(sound));
            }
        }
        if (const Value* animations = entry.get("player_animations"); animations != nullptr) {
            if (animations->array() == nullptr) return std::nullopt;
            for (const Value& row : *animations->array()) {
                PlayerAnimation animation{text_field(row, "name"), text_field(row, "file")};
                if (animation.name.empty() || animation.file.empty()) return std::nullopt;
                if (const Value* rate = row.get("rate"); rate != nullptr) {
                    const double* number = rate->number();
                    if (number == nullptr || !(*number > 0.0 && *number <= 100.0)) return std::nullopt;
                    animation.rate = *number;
                }
                if (const Value* blend = row.get("blend_frames"); blend != nullptr) {
                    const double* number = blend->number();
                    if (number == nullptr || !(*number >= 0.0 && *number <= 600.0) || *number != static_cast<double>(static_cast<int>(*number))) {
                        return std::nullopt;
                    }
                    animation.blend_frames = static_cast<int>(*number);
                }
                layer.player_animations.push_back(std::move(animation));
            }
        }
        if (const Value* classes = entry.get("object_classes"); classes != nullptr && classes->array() != nullptr) {
            for (const Value& row : *classes->array()) {
                ObjectClass object{text_field(row, "name"), text_field(row, "like"), text_field(row, "script")};
                // A new name made like a game class, or a script on a class (the game's own name, no "like").
                if (object.name.empty() || (object.like.empty() && object.script.empty())) return std::nullopt;
                if (!object.script.empty() && !script_name_ok(object.script)) return std::nullopt;
                layer.object_classes.push_back(std::move(object));
            }
        }
        if (const Value* forms = entry.get("forms"); forms != nullptr) {
            if (forms->array() == nullptr) return std::nullopt;
            for (const Value& row : *forms->array()) {
                Form form;
                form.name = text_field(row, "name");
                form.model = text_field(row, "model");
                form.music = text_field(row, "music");
                form.script = text_field(row, "script");
                if (form.name.empty() || (!form.script.empty() && !script_name_ok(form.script))) return std::nullopt;
                // The game's forms by name (NEBULA_POWER_UP_*).
                const auto game_form = [&row](std::string_view key, int& out) {
                    const std::string name = text_field(row, key);
                    if (name.empty()) return true;
                    static constexpr std::pair<std::string_view, int> names[] = {
                        {"normal", 0}, {"invincible", 1}, {"fire", 2}, {"ice", 3}, {"bee", 4},
                        {"spring", 5}, {"boo", 6},        {"flying", 7}, {"tornado", 9}};
                    for (const auto& [known, number] : names) {
                        if (name == known) {
                            out = number;
                            return true;
                        }
                    }
                    return false;
                };
                const auto whole = [&row](std::string_view key, int& out, int most) {
                    const Value* value = row.get(key);
                    if (value == nullptr) return true;
                    const double* number = value->number();
                    if (number == nullptr || !(*number >= 0.0 && *number <= most) || *number != static_cast<double>(static_cast<int>(*number))) return false;
                    out = static_cast<int>(*number);
                    return true;
                };
                if (!game_form("acts_like", form.acts_like) || !game_form("looks_like", form.looks_like) ||
                    !whole("color_frame", form.color_frame, 255) || !whole("frames", form.frames, 65535)) {
                    return std::nullopt;
                }
                layer.forms.push_back(std::move(form));
            }
        }
        if (const Value* objects = entry.get("objects"); objects != nullptr) {
            if (objects->array() == nullptr) return std::nullopt;
            for (const Value& row : *objects->array()) {
                ScriptObject object;
                object.name = text_field(row, "name");
                object.script = text_field(row, "script");
                object.archive = text_field(row, "archive");
                if (object.name.empty() || !script_name_ok(object.script)) return std::nullopt;
                const std::string kind = text_field(row, "kind");
                static constexpr std::string_view kinds[] = {"enemy", "map", "npc", "ride", "decoration"};
                object.kind = -1;
                for (int k = 0; k < 5; ++k) {
                    if (kind.empty() ? k == 0 : kind == kinds[k]) object.kind = k;
                }
                if (object.kind < 0) return std::nullopt;
                const auto distance = [&row](std::string_view key, double& out) {
                    const Value* value = row.get(key);
                    if (value == nullptr) return true;
                    const double* number = value->number();
                    if (number == nullptr || !(*number >= 0.0 && *number <= 100000.0)) return false;
                    out = *number;
                    return true;
                };
                if (!distance("radius", object.radius) || !distance("shadow_radius", object.shadow_radius) ||
                    !distance("clip_radius", object.clip_radius)) {
                    return std::nullopt;
                }
                if (const Value* collision = row.get("collision"); collision != nullptr) {
                    const bool* solid = std::get_if<bool>(&collision->data);
                    if (solid == nullptr) return std::nullopt;
                    object.collision = *solid;
                }
                layer.objects.push_back(std::move(object));
            }
        }
        if (const Value* scripts = entry.get("scripts"); scripts != nullptr) {
            if (scripts->array() == nullptr) return std::nullopt;
            for (const Value& row : *scripts->array()) {
                const std::string* name = std::get_if<std::string>(&row.data);
                if (name == nullptr || !script_name_ok(*name)) return std::nullopt;
                layer.scripts.push_back(*name);
            }
        }
        // "switches" gave way to "tunables": the mod is refused, naming the tunable for its first switch.
        if (const Value* switches = entry.get("switches"); switches != nullptr && out.retired.empty()) {
            out.retired = "\"SWITCHES\" ARE NOW \"TUNABLES\"";
            if (switches->object() != nullptr && !switches->object()->empty()) {
                const std::string& name = switches->object()->begin()->first;
                if (const SwitchTunable tunable = switch_tunable(name); !tunable.key.empty()) {
                    out.retired += ": " + name + " IS " + std::string(tunable.key) + (tunable.opposite ? " (THE OPPOSITE SENSE)" : "");
                }
            }
        }
        if (const Value* materials = entry.get("materials"); materials != nullptr && materials->object() != nullptr) {
            for (const auto& [name, file] : *materials->object()) {
                if (file.string() == nullptr || !path_stays_inside(*file.string())) return std::nullopt;
                layer.materials[name] = *file.string();
            }
        }
        if (!read_rows(entry, layer.rows)) return std::nullopt;
        layer.when = read_when(entry.get("when"));
        out.layers.push_back(std::move(layer));
    }
    if (const Value* save = root->get("save"); save != nullptr && save->object() != nullptr) {
        out.save_path = text_field(*save, "path");
        if (!path_stays_inside(out.save_path)) out.save_path.clear();
        const std::string mode = text_field(*save, "mode");
        out.save_mode = mode == "own" ? SaveMode::Own : mode == "copy" ? SaveMode::Copy : SaveMode::Shared;
        out.save_tag = text_field(*save, "tag");
        const Value* change = save->get("player_can_change");
        const bool* can = change != nullptr ? std::get_if<bool>(&change->data) : nullptr;
        out.save_player_can_change = can != nullptr && *can;
    }
    if (out.save_tag.empty()) out.save_tag = out.id;
    // The tag names a folder of saves: an id's letters keep it in mods/.saves.
    if (!valid_id(out.save_tag)) return std::nullopt;
    if (!read_rows(*root, out)) return std::nullopt;
    if (const Value* plugins = root->get("plugins"); plugins != nullptr && plugins->array() != nullptr) {
        for (const Value& entry : *plugins->array()) {
            Plugin plugin;
            if (const Value* binaries = entry.get("binaries"); binaries != nullptr && binaries->object() != nullptr) {
                for (const auto& [target, file] : *binaries->object()) {
                    if (file.string() == nullptr || !path_stays_inside(*file.string())) return std::nullopt;
                    plugin.binaries[target] = *file.string();
                }
            }
            const Value* api = entry.get("api");
            plugin.api = api != nullptr && api->number() != nullptr ? whole_u32(*api->number()) : 0u;
            plugin.when = read_when(entry.get("when"));
            out.plugins.push_back(std::move(plugin));
        }
    }
    return out;
}

// A luma.json past this is not read: real ones are kilobytes.
constexpr std::uintmax_t kManifestLimit = 16u << 20u;

std::optional<Manifest> read_manifest(const std::filesystem::path& folder) {
    std::error_code size_error;
    const std::uintmax_t size = std::filesystem::file_size(folder / "luma.json", size_error);
    if (size_error || size > kManifestLimit) return std::nullopt;
    std::ifstream in(folder / "luma.json", std::ios::binary);
    if (!in) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse_manifest(text);
}

int compare_versions(std::string_view a, std::string_view b) {
    while (!a.empty() || !b.empty()) {
        const auto next = [](std::string_view& text) {
            unsigned long value = 0;
            std::size_t i = 0;
            for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) value = value * 10u + static_cast<unsigned long>(text[i] - '0');
            while (i < text.size() && text[i] != '.') ++i;
            text.remove_prefix(i < text.size() ? i + 1u : i);
            return value;
        };
        const unsigned long left = next(a);
        const unsigned long right = next(b);
        if (left != right) return left < right ? -1 : 1;
    }
    return 0;
}

std::string refusal(const Manifest& manifest, const std::string& game_id, std::string_view nebula_version, std::string_view) {
    if (manifest.format > kFormat) return "THIS MOD NEEDS A NEWER NEBULA";
    if (!manifest.retired.empty()) return manifest.retired;
    // The features this Nebula has; any other is one it lacks. "tunables": the game's tuning numbers by "<file>.<name>";
    // "player-animations": a layer's player_animations.
    for (const std::string& feature : manifest.requires_features) {
        if (feature != "custom-shaders" && feature != "tunables" && feature != "player-animations") return "THIS MOD NEEDS A NEWER NEBULA";
    }
    if (!manifest.nebula.empty() && compare_versions(nebula_version, manifest.nebula) < 0) return "THIS MOD NEEDS A NEWER NEBULA";
    if (manifest.game.starts_with("SB4") || manifest.game.starts_with("SB5")) return "THIS MOD IS FOR SUPER MARIO GALAXY 2";
    if (!manifest.game.starts_with("RMG")) return "THIS MOD IS FOR ANOTHER GAME";
    if (!manifest.versions.empty() && game_id.size() >= 4u && !contains(manifest.versions, game_id.substr(3, 1))) {
        return "THIS MOD DOESN'T WORK ON THIS VERSION OF THE GAME";
    }
    // Made for other discs: the reason names the ones it is for (the game's menu translates the words; the region
    // names stay as written).
    if (!manifest.regions.empty() && game_id.size() >= 4u) {
        const char letter = game_id[3];
        const std::string_view here = letter == 'E' ? "US" : letter == 'P' ? "PAL" : letter == 'J' ? "JP" : letter == 'K' ? "KR" : "";
        if (!contains(manifest.regions, std::string(here))) {
            std::string reason = "THIS MOD IS ONLY FOR: ";
            for (std::size_t i = 0; i < manifest.regions.size(); ++i) reason += (i != 0u ? ", " : "") + manifest.regions[i];
            return reason;
        }
    }
    for (const Plugin& plugin : manifest.plugins) {
        if (plugin.api > kPluginApi) return "THIS MOD NEEDS A NEWER NEBULA";
        if (plugin.api != 0u && plugin.api < kOldestPluginApi) return "THIS MOD'S CODE IS FOR AN OLDER NEBULA";
        if (plugin.binaries.find(std::string(plugin_target())) == plugin.binaries.end()) return "THIS MOD HAS NO VERSION FOR THIS DEVICE";
    }
    return {};
}

std::string refusal_detail(const Manifest& manifest) {
    for (std::size_t i = 0; i < manifest.plugins.size(); ++i) {
        const std::uint32_t api = manifest.plugins[i].api;
        if (api > kPluginApi || (api != 0u && api < kOldestPluginApi)) {
            return "luma.json: plugins[" + std::to_string(i) + "].api is " + std::to_string(api) + "; this game runs plugins built with nebula_mod_api.h " +
                   std::to_string(kOldestPluginApi) + " to " + std::to_string(kPluginApi) + ", and a LumaCore library (luma.h) gives no api";
        }
    }
    return {};
}

bool when_holds(const When& when, const Situation& situation) {
    if (when.unknown) return false;
    if (!when.versions.empty() && !contains(when.versions, situation.version)) return false;
    if (!when.languages.empty() && !contains(when.languages, situation.language)) return false;
    if (!when.platforms.empty() && !contains(when.platforms, situation.platform)) return false;
    for (const auto& [option, choices] : when.options) {
        const auto it = situation.options.find(option);
        if (it == situation.options.end() || !contains(choices, it->second)) return false;
    }
    for (const std::string& mod : when.mods) {
        if (!contains(situation.mods, mod)) return false;
    }
    for (const When& one : when.not_any) {
        if (when_holds(one, situation)) return false;
    }
    if (!when.any_of.empty() && std::none_of(when.any_of.begin(), when.any_of.end(), [&](const When& one) { return when_holds(one, situation); })) return false;
    return true;
}

bool layer_used(const Layer& layer, const Situation& situation) {
    return when_holds(layer.when, situation);
}

std::string_view plugin_target() noexcept {
#if defined(_M_ARM64) || defined(__aarch64__)
#define NEBULA_PLUGIN_ARCH "arm64"
#else
#define NEBULA_PLUGIN_ARCH "x64"
#endif
#if defined(_WIN32)
    return "windows-" NEBULA_PLUGIN_ARCH;
#elif defined(__ANDROID__)
    return "android-" NEBULA_PLUGIN_ARCH;
#elif defined(__APPLE__)
    return "macos-" NEBULA_PLUGIN_ARCH;
#else
    return "linux-" NEBULA_PLUGIN_ARCH;
#endif
#undef NEBULA_PLUGIN_ARCH
}

std::string language_code(std::string_view folder) {
    static constexpr std::pair<std::string_view, std::string_view> kCodes[] = {
        {"UsEnglish", "en"},       {"EuEnglish", "en-GB"},    {"UsFrench", "fr-CA"},      {"EuFrench", "fr"},
        {"UsSpanish", "es-MX"},    {"EuSpanish", "es"},       {"EuGerman", "de"},         {"EuItalian", "it"},
        {"EuDutch", "nl"},         {"UsPortuguese", "pt"},    {"JpJapanese", "ja"},       {"KrKorean", "ko"},
        {"CnSimpChinese", "zh-Hans"}, {"TwTradChinese", "zh-Hant"},
    };
    for (const auto& [name, code] : kCodes) {
        if (name == folder) return std::string(code);
    }
    return "en";
}

std::string_view this_platform() noexcept {
#if defined(_WIN32)
    return "windows";
#elif defined(__ANDROID__)
    return "android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return "ios";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

SwitchTunable switch_tunable(std::string_view name) noexcept {
    static constexpr std::pair<std::string_view, SwitchTunable> kSwitches[] = {
        {"flip_panel_group", {"FlipPanel.cPanelGroupSize"}},
        {"flip_panel_observer_group", {"FlipPanel.cObserverGroupSize"}},
        {"water_bazooka_shot_flag", {"WaterBazooka.cShotInvalidSpinKill"}},
        {"kamek_barrier_offset_y", {"BossKameckBarrier.cOffsetY"}},
        {"skeletal_fish_baby_no_bubble", {"SkeletalFishBaby.cBreakAirBubble", true}},
        {"dodoryu_shell_only", {"GameRules.cDodoryuShellOnly"}},
        {"miss_star_pieces", {"SequenceUtil.cMissStarPieceNum"}},
        {"no_hidden_star_table", {"SceneUtil.cHiddenStarTable", true}},
        {"no_sudden_death_dodoryu", {"SceneUtil.cSuddenDeathDodoryu", true}},
        {"begoman_fire_proof", {"GameRules.cBegomanFireProof"}},
        {"messenger_always_ex", {"KinopioAstro.cMessangerAlwaysEx"}},
        {"messenger_ex_one_ups", {"KinopioAstro.sMessangerOneUpExNum"}},
        {"player_health", {"GameRules.cPlayerHealth"}},
        {"player_health_fixed", {"MarioActor.cDarkCometOneHealth", true}},
        {"hit_takes_all", {"MarioActor.cHitTakesAll"}},
        {"life_up_health", {"GameRules.cLifeUpHealth"}},
        {"no_low_health_warning", {"MarioEffect.cLowHealthWarning", true}},
        {"no_dark_comet_rules", {"EventUtil.cDarkCometRules", true}},
        {"max_lives", {"GameDataPlayerStatus.cMaxPlayerLeft"}},
        {"max_star_bits", {"GameRules.cMaxStarBits"}},
        {"max_carried_star_bits", {"GameDataTemporaryInGalaxy.cMaxCarriedStarPieceNum"}},
        {"max_coins", {"GameRules.cMaxCoins"}},
        {"coins_per_one_up", {"ScenePlayingResult.cCoinStepNum"}},
        {"star_bits_per_one_up", {"ScenePlayingResult.cStarPieceStepNum"}},
        {"starting_lives", {"GameDataPlayerStatus.cStartingPlayerLeft"}},
        {"purple_coins_for_star", {"ScenePlayingResult.cPurpleCoinPowerStarAppearNum"}},
        {"water_air", {"MarioActor.cWaterAir"}},
    };
    for (const auto& [known, tunable] : kSwitches) {
        if (known == name) return tunable;
    }
    return {};
}

namespace {

// Every language folder a game of the series has messages in (nebula-import's cook/overlay.rs MESSAGE_LANGUAGES).
constexpr std::string_view kMessageLanguages[] = {
    "UsEnglish", "UsFrench", "UsSpanish", "EuEnglish", "EuFrench", "EuGerman", "EuItalian", "EuSpanish", "EuDutch",
    "JpJapanese", "KrKorean", "CnSimpChinese", "TwTradChinese", "UsPortuguese",
};

std::string key_of(std::initializer_list<std::string_view> fields) {
    std::string key;
    for (const std::string_view field : fields) {
        if (!key.empty()) key += '\t';
        key += field;
    }
    return key;
}

std::string lower_text(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// A JSON value as one piece of a key: a number as the shortest text that reads back as it.
std::string value_text(const Value& value) {
    if (const std::string* text = value.string()) return *text;
    if (const double* number = value.number()) {
        char out[32];
        std::snprintf(out, sizeof(out), "%.17g", *number);
        return out;
    }
    if (const bool* flag = std::get_if<bool>(&value.data)) return *flag ? "true" : "false";
    return "null";
}

// A table row named by its "where" columns, sorted, so that two mods naming them in another order name one row.
std::string row_text(const Value& where) {
    const Object* fields = where.object();
    if (fields == nullptr) return value_text(where);
    std::vector<std::string> parts;
    for (const auto& [column, value] : *fields) parts.push_back(column + "=" + value_text(value));
    std::sort(parts.begin(), parts.end());
    std::string out;
    for (const std::string& part : parts) out += (out.empty() ? "" : ",") + part;
    return out;
}

void add_lines(std::vector<std::string>& keys, std::string_view kind, std::string_view id, std::string_view language) {
    if (!language.empty()) {
        keys.push_back(key_of({kind, id, language}));
        return;
    }
    for (const std::string_view folder : kMessageLanguages) keys.push_back(key_of({kind, id, folder}));
}

}  // namespace

bool rows_set_game(const Rows& rows) noexcept {
    return !rows.galaxies.empty() || !rows.music.empty() || !rows.event_flags.empty() || !rows.comet_schedule.empty() ||
           !rows.text.empty() || !rows.text_like.empty() || !rows.story.empty();
}

bool layer_sets_game(const Layer& layer) noexcept {
    return rows_set_game(layer.rows) || !layer.materials.empty() || !layer.object_changes.empty() || !layer.comet_time_limits.empty() ||
           !layer.stages.empty() || !layer.player_constants.empty() || !layer.tunables.empty() || !layer.object_classes.empty() || !layer.forms.empty() ||
           !layer.objects.empty() || !layer.scripts.empty() || !layer.sound_effects.empty() || !layer.player_animations.empty();
}

bool manifest_sets_game(const Manifest& manifest) noexcept {
    if (rows_set_game(manifest) || !manifest.achievements.empty() || manifest.save_mode != SaveMode::Shared) return true;
    const std::string target(plugin_target());
    return std::any_of(manifest.plugins.begin(), manifest.plugins.end(),
                       [&target](const Plugin& plugin) { return plugin.binaries.find(target) != plugin.binaries.end(); });
}

std::vector<std::string> setting_keys(const Rows& rows) {
    std::vector<std::string> keys;
    for (const Galaxy& galaxy : rows.galaxies) keys.push_back(key_of({"galaxy", galaxy.name}));
    for (const Music& music : rows.music) keys.push_back(key_of({"music", music.name}));
    for (const EventFlag& flag : rows.event_flags) keys.push_back(key_of({"event flag", flag.name}));
    for (const CometSchedule& schedule : rows.comet_schedule) keys.push_back(key_of({"comet schedule", std::to_string(schedule.dome)}));
    for (const auto& [id, words] : rows.text) add_lines(keys, "line", id, {});
    for (const auto& [part, value] : rows.story) keys.push_back(key_of({"story", part}));
    return keys;
}

std::vector<std::string> setting_keys(const Layer& layer) {
    std::vector<std::string> keys = setting_keys(layer.rows);
    for (const auto& [material, file] : layer.materials) keys.push_back(key_of({"material", lower_text(material)}));
    for (const ObjectChange& change : layer.object_changes) {
        keys.push_back(key_of({"object", std::to_string(change.where), change.object}));
    }
    for (const CometLimit& limit : layer.comet_time_limits) {
        keys.push_back(key_of({"comet limit", limit.galaxy, std::to_string(limit.mission)}));
    }
    for (const StageRule& rule : layer.stages) keys.push_back(key_of({"stage", rule.stage, std::to_string(rule.scenario)}));
    for (const PlayerConstant& constant : layer.player_constants) {
        keys.push_back(key_of({"constant", std::to_string(constant.character), constant.name, std::to_string(constant.index)}));
    }
    for (const Tunable& tunable : layer.tunables) keys.push_back(key_of({"tunable", tunable.key}));
    for (const ObjectClass& object : layer.object_classes) keys.push_back(key_of({"class", object.name}));
    for (const Form& form : layer.forms) keys.push_back(key_of({"form", form.name}));
    for (const ScriptObject& object : layer.objects) keys.push_back(key_of({"class", object.name}));
    for (const std::string& script : layer.scripts) keys.push_back(key_of({"script", script}));
    for (const SoundEffect& sound : layer.sound_effects) keys.push_back(key_of({"sound", sound.name}));
    for (const PlayerAnimation& animation : layer.player_animations) keys.push_back(key_of({"animation", animation.name}));
    return keys;
}

std::vector<std::string> merge_keys(std::string_view path, std::string_view json) {
    std::vector<std::string> keys;
    const std::string lower = lower_text(path.substr(path.starts_with('/') ? 1u : 0u));
    if (!lower.ends_with(".json") || lower.ends_with(".jpc.json") || lower.ends_with("particlenames.bcsv.json")) return keys;
    const std::optional<Value> root = Parser(json).whole();
    if (!root || root->object() == nullptr) return keys;
    // Messages: every language the game has, or the one its folder names.
    std::string language;
    bool messages = lower == "messagedata/messages.json";
    if (!messages && lower.ends_with("/messagedata/message.arc/message.bmg.json") && std::count(lower.begin(), lower.end(), '/') == 3) {
        messages = true;
        language = std::string(path.substr(path.starts_with('/') ? 1u : 0u, lower.find('/')));
    }
    if (messages) {
        for (const char* part : {"lines", "flows"}) {
            if (const Value* entries = root->get(part); entries != nullptr && entries->object() != nullptr) {
                for (const auto& [id, value] : *entries->object()) add_lines(keys, part[0] == 'l' ? "line" : "flow", id, language);
            }
        }
        return keys;
    }
    const std::string table = lower.substr(0, lower.size() - 5u);
    if (const Value* removes = root->get("remove"); removes != nullptr && removes->array() != nullptr) {
        for (const Value& where : *removes->array()) keys.push_back(key_of({"row", table, row_text(where)}));
    }
    if (const Value* changes = root->get("change"); changes != nullptr && changes->array() != nullptr) {
        for (const Value& change : *changes->array()) {
            const Value* where = change.get("where");
            const Value* set = change.get("set");
            if (where == nullptr || set == nullptr || set->object() == nullptr) continue;
            const std::string row = row_text(*where);
            for (const auto& [column, value] : *set->object()) keys.push_back(key_of({"cell", table, row, column}));
        }
    }
    return keys;
}

}  // namespace galaxy::luma
