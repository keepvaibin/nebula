// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/luma.h"

#include <cstdio>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <cstdlib>
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

// A path that stays in the mod's folder.
bool path_stays_inside(const std::string& path) {
    if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) return false;
    std::size_t at = 0;
    while (at <= path.size()) {
        std::size_t end = path.find_first_of("/\\", at);
        if (end == std::string::npos) end = path.size();
        if (path.compare(at, end - at, "..") == 0) return false;
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
            out.text[id] = read_text(&words);
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
    out.nebula = text_field(*root, "nebula");
    out.requires_features = string_list(root->get("requires"));
    out.authors = string_list(root->get("authors"));
    out.description = read_text(root->get("description"));
    out.license = text_field(*root, "license");
    out.homepage = text_field(*root, "homepage");
    out.icon = text_field(*root, "icon");
    if (!out.icon.empty() && !path_stays_inside(out.icon)) out.icon.clear();
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
                        if (who != "luigi") layer.player_constants.push_back({0, name, index, static_cast<float>(number), {}});
                        if (who != "mario") layer.player_constants.push_back({1, name, index, static_cast<float>(number), {}});
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
        if (const Value* sounds = entry.get("sound_effects"); sounds != nullptr && sounds->array() != nullptr) {
            for (const Value& row : *sounds->array()) {
                SoundEffect sound{text_field(row, "name"), text_field(row, "file")};
                if (sound.name.empty() || sound.file.empty()) return std::nullopt;
                layer.sound_effects.push_back(std::move(sound));
            }
        }
        if (const Value* classes = entry.get("object_classes"); classes != nullptr && classes->array() != nullptr) {
            for (const Value& row : *classes->array()) {
                ObjectClass object{text_field(row, "name"), text_field(row, "like")};
                if (object.name.empty() || object.like.empty()) return std::nullopt;
                layer.object_classes.push_back(std::move(object));
            }
        }
        if (const Value* switches = entry.get("switches"); switches != nullptr && switches->object() != nullptr) {
            for (const auto& [name, value] : *switches->object()) {
                if (const double* number = value.number()) layer.switches[name] = whole_u32(*number);
                else if (const Value* option = value.get("option"); option != nullptr && option->string() != nullptr) layer.switch_options[name] = *option->string();
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
    // No feature names exist yet beyond the format itself: any is one this game lacks.
    for (const std::string& feature : manifest.requires_features) {
        if (feature != "custom-shaders") return "THIS MOD NEEDS A NEWER NEBULA";
    }
    if (!manifest.nebula.empty() && compare_versions(nebula_version, manifest.nebula) < 0) return "THIS MOD NEEDS A NEWER NEBULA";
    if (manifest.game.starts_with("SB4") || manifest.game.starts_with("SB5")) return "THIS MOD IS FOR SUPER MARIO GALAXY 2";
    if (!manifest.game.starts_with("RMG")) return "THIS MOD IS FOR ANOTHER GAME";
    if (!manifest.versions.empty() && game_id.size() >= 4u && !contains(manifest.versions, game_id.substr(3, 1))) {
        return "THIS MOD DOESN'T WORK ON THIS VERSION OF THE GAME";
    }
    for (const Plugin& plugin : manifest.plugins) {
        if (plugin.api > kPluginApi) return "THIS MOD NEEDS A NEWER NEBULA";
        if (plugin.binaries.find(std::string(plugin_target())) == plugin.binaries.end()) return "THIS MOD HAS NO VERSION FOR THIS DEVICE";
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

std::uint32_t switch_number(std::string_view name) noexcept {
    // Numbered as NEBULA_PATCH_* is: the host checks them against each other.
    static constexpr std::pair<std::string_view, std::uint32_t> kSwitches[] = {
        {"flip_panel_group", 1u},
        {"flip_panel_observer_group", 2u},
        {"water_bazooka_shot_flag", 3u},
        {"kamek_barrier_offset_y", 4u},
        {"skeletal_fish_baby_no_bubble", 5u},
        {"dodoryu_shell_only", 6u},
        {"miss_star_pieces", 7u},
        {"no_hidden_star_table", 8u},
        {"no_sudden_death_dodoryu", 9u},
        {"begoman_fire_proof", 10u},
        {"messenger_always_ex", 11u},
        {"messenger_ex_one_ups", 12u},
        {"player_health", 13u},
        {"player_health_fixed", 14u},
        {"hit_takes_all", 15u},
        {"life_up_health", 16u},
        {"no_low_health_warning", 17u},
        {"no_dark_comet_rules", 18u},
        {"max_lives", 19u},
        {"max_star_bits", 20u},
        {"max_carried_star_bits", 21u},
        {"max_coins", 22u},
        {"coins_per_one_up", 23u},
        {"star_bits_per_one_up", 24u},
        {"starting_lives", 25u},
        {"purple_coins_for_star", 26u},
        {"water_air", 27u},
    };
    for (const auto& [known, number] : kSwitches) {
        if (known == name) return number;
    }
    return 0u;
}

}  // namespace galaxy::luma
