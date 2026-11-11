// SPDX-License-Identifier: GPL-3.0-only
// The proof harness's host half (harness.h; contract `harness`, revision 1).
//
// The plan (written by tools/lumacore/luma_test.py, one item a line, UTF-8):
//   harness 1
//   out <folder>                   where facts.jsonl and pictures/ go (the rest of the line)
//   bare 0|1                       1: no game half
//   clock title                    a test with no stage: frame 0 is not before the title screen ("boot: title screen")
//   setup <item ...>               given to the game half in order ("setup save.spin 1", ..., "setup done")
//   step <at|after> <action ...>   a step; `at` a frame of the test clock, or `after` with the cond lines below it
//   cond fact <key> <op> <within|-> <JSON>   the step above waits for this fact (its newest value) to hold
//   cond log <regular expression>            ... or for a log line that matches (the rest of the line)
//   timeout <frames>                         an `after` step's wait, from the frame the steps before it have all run
// Actions: hold <BUTTON+BUTTON> <frames> | stick <x> <y> <frames> | shake <frames> | pointer <x> <y> <frames> |
// key <name> | picture <name> | wait | end | game <line for the game half: call, read, stage>.
//
// Facts go to <out>/facts.jsonl: {"frame": <test clock, -1 before frame 0>, "key": ..., "value": <JSON>}. Besides the
// game half's and the mods' (a log line "[mod <id>] fact <key> <JSON>" is the fact "<id>.<key>"): mod.<id>.loaded from
// the host's mods lines; picture.<name> when a picture is kept; harness.clock {"game_frame"} when frame 0 is fixed;
// harness.end {"frame"} at the end step; harness.stop {"verdict", "reason"} when setup or a step failed. The runner
// decides every verdict from these files.
#include "harness.h"

#include <luma_harness.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace harness {
namespace {

// --- A small JSON reader: what facts and conditions hold ----------------------------------------------------------

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;           // a string's value; an object's source text
    std::vector<Json> items;    // an array's
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    std::optional<Json> read() {
        std::optional<Json> value = any();
        space();
        if (!value || at_ != text_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    void space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\r' || text_[at_] == '\n')) {
            ++at_;
        }
    }
    bool word(std::string_view w) {
        if (text_.substr(at_, w.size()) == w) {
            at_ += w.size();
            return true;
        }
        return false;
    }
    std::optional<std::string> string() {
        if (at_ >= text_.size() || text_[at_] != '"') {
            return std::nullopt;
        }
        ++at_;
        std::string out;
        while (at_ < text_.size() && text_[at_] != '"') {
            char c = text_[at_++];
            if (static_cast<unsigned char>(c) < 0x20u) {
                return std::nullopt;
            }
            if (c == '\\') {
                if (at_ >= text_.size()) {
                    return std::nullopt;
                }
                const char e = text_[at_++];
                switch (e) {
                case '"': case '\\': case '/': out += e; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (at_ + 4u > text_.size()) {
                        return std::nullopt;
                    }
                    for (int i = 0; i < 4; ++i) {
                        if (std::isxdigit(static_cast<unsigned char>(text_[at_ + i])) == 0) {
                            return std::nullopt;
                        }
                    }
                    out += "\\u" + std::string(text_.substr(at_, 4));  // kept as written: compared, never shown
                    at_ += 4u;
                    break;
                }
                default: return std::nullopt;
                }
                continue;
            }
            out += c;
        }
        if (at_ >= text_.size()) {
            return std::nullopt;
        }
        ++at_;
        return out;
    }
    std::optional<Json> any() {
        if (++depth_ > 64) {
            return std::nullopt;
        }
        space();
        Json v;
        if (at_ >= text_.size()) {
            return std::nullopt;
        }
        const char c = text_[at_];
        if (word("null")) {
            v.kind = Json::Null;
        } else if (word("true")) {
            v.kind = Json::Bool;
            v.boolean = true;
        } else if (word("false")) {
            v.kind = Json::Bool;
        } else if (c == '"') {
            std::optional<std::string> s = string();
            if (!s) {
                return std::nullopt;
            }
            v.kind = Json::String;
            v.text = std::move(*s);
        } else if (c == '[') {
            ++at_;
            v.kind = Json::Array;
            space();
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
            } else {
                for (;;) {
                    std::optional<Json> item = any();
                    if (!item) {
                        return std::nullopt;
                    }
                    v.items.push_back(std::move(*item));
                    space();
                    if (at_ < text_.size() && text_[at_] == ',') {
                        ++at_;
                        continue;
                    }
                    if (at_ < text_.size() && text_[at_] == ']') {
                        ++at_;
                        break;
                    }
                    return std::nullopt;
                }
            }
        } else if (c == '{') {
            const std::size_t from = at_++;
            v.kind = Json::Object;
            space();
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
            } else {
                for (;;) {
                    space();
                    if (!string()) {
                        return std::nullopt;
                    }
                    space();
                    if (at_ >= text_.size() || text_[at_++] != ':' || !any()) {
                        return std::nullopt;
                    }
                    space();
                    if (at_ < text_.size() && text_[at_] == ',') {
                        ++at_;
                        continue;
                    }
                    if (at_ < text_.size() && text_[at_] == '}') {
                        ++at_;
                        break;
                    }
                    return std::nullopt;
                }
            }
            v.text = std::string(text_.substr(from, at_ - from));
        } else {
            const std::size_t from = at_;
            if (at_ < text_.size() && text_[at_] == '-') {
                ++at_;
            }
            const std::size_t digits = at_;
            while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) != 0 || text_[at_] == '.' ||
                                          text_[at_] == 'e' || text_[at_] == 'E' || text_[at_] == '+' || text_[at_] == '-')) {
                ++at_;
            }
            if (at_ == digits) {
                return std::nullopt;
            }
            const std::string number(text_.substr(from, at_ - from));
            char* end = nullptr;
            v.number = std::strtod(number.c_str(), &end);
            if (end != number.c_str() + number.size() || !std::isfinite(v.number)) {
                return std::nullopt;
            }
            v.kind = Json::Number;
        }
        --depth_;
        return v;
    }

    std::string_view text_;
    std::size_t at_ = 0u;
    int depth_ = 0;
};

std::optional<Json> parse(std::string_view text) {
    return JsonReader(text).read();
}

bool equal(const Json& a, const Json& b) {
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
    case Json::Null: return true;
    case Json::Bool: return a.boolean == b.boolean;
    case Json::Number: return a.number == b.number;
    case Json::String: case Json::Object: return a.text == b.text;
    case Json::Array:
        if (a.items.size() != b.items.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.items.size(); ++i) {
            if (!equal(a.items[i], b.items[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

std::string quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20u) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += escaped;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

// --- The plan ------------------------------------------------------------------------------------------------------

enum class Action { Hold, Stick, Shake, Pointer, Key, Picture, Wait, End, Game };

struct Cond {
    bool log = false;
    std::string key, op;      // a fact's
    std::optional<double> within;
    Json value;
    std::regex pattern;       // a log line's
    std::string source;       // as written, for the reason
};

struct Step {
    std::size_t number = 0u;  // 1-based, as written
    std::optional<std::int64_t> at;  // none: after
    Action action = Action::Wait;
    std::string text;         // the rest of the line
    std::uint32_t buttons = 0u;
    float x = 0.0f, y = 0.0f;
    std::uint32_t key = 0u;
    std::int64_t frames = 0;
    std::vector<Cond> conds;
    std::int64_t timeout = 600;
    // While running.
    bool done = false;
    bool waiting = false;     // a game line given, its answer not yet back
    std::optional<std::int64_t> armed;  // an after step: the frame its wait began
};

struct Input {
    std::int64_t start, frames;
    std::uint32_t buttons;
    bool shake, stick, pointer;
    float x, y;
};

struct Picture {
    std::uint64_t game_frame;
    std::string name;
};

struct State {
    const NebulaHostForExtension* host = nullptr;
    void (*log)(const std::string&) = nullptr;
    void (*quit)() = nullptr;
    bool on = false;
    bool bare = false;
    bool clock_title = false;     // frame 0 waits for the title screen
    bool title_seen = false;
    bool setup_finished = false;  // "setup done" answered
    std::string out;
    std::ofstream facts;
    std::vector<std::string> setup;
    std::size_t next_setup = 0u;
    bool setup_given = false;     // a setup line given, its answer not yet back
    std::vector<Step> steps;
    std::optional<std::uint64_t> base;  // the game frame that is frame 0
    std::uint64_t game_frame = 0u;
    std::int64_t frame_sent = -1;       // the test frame the game half was last told
    std::deque<std::size_t> game_queue;  // steps whose line the game half is to get
    std::string given;                   // the text next() returned
    std::size_t given_step = 0u;         // its step, 0 for setup or a frame line
    bool given_frame_line = false;
    std::vector<Input> inputs;
    std::deque<Picture> pictures;
    std::string picture_file;
    std::map<std::string, std::string> latest;  // each fact's newest value, as JSON text
    std::vector<std::regex*> log_watch;
    bool finished = false;
} g;

std::recursive_mutex g_lock;
LumaHarness g_service{};

std::int64_t test_frame() {
    return g.base ? static_cast<std::int64_t>(g.game_frame) - static_cast<std::int64_t>(*g.base) : -1;
}

void write_fact(const std::string& key, const std::string& json, std::optional<std::int64_t> frame = std::nullopt) {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (!g.facts.is_open()) {
        return;
    }
    const std::string at = std::to_string(frame.value_or(test_frame()));
    if (!parse(json)) {
        const std::string bad = "{\"key\":" + quoted(key) + ",\"value\":" + quoted(json) + "}";
        g.facts << "{\"frame\":" << at << ",\"key\":\"harness.bad_fact\",\"value\":" << bad << "}\n" << std::flush;
        return;
    }
    g.facts << "{\"frame\":" << at << ",\"key\":" << quoted(key) << ",\"value\":" << json << "}\n" << std::flush;
    g.latest[key] = json;
}

// Frame 0 of the test clock is this game frame.
void set_base(std::uint64_t game_frame) {
    g.base = game_frame;
    write_fact("harness.clock", "{\"game_frame\":" + std::to_string(game_frame) + "}", 0);
}

// Ends the run: the end step, or a failure the runner names.
void finish(const std::string& verdict, const std::string& reason) {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (g.finished) {
        return;
    }
    g.finished = true;
    if (verdict.empty()) {
        write_fact("harness.end", "{\"frame\":" + std::to_string(test_frame()) + "}");
    } else {
        write_fact("harness.stop", "{\"verdict\":" + quoted(verdict) + ",\"reason\":" + quoted(reason) + "}");
    }
    if (g.facts.is_open()) {
        g.facts.close();
    }
    if (g.quit != nullptr) {
        g.quit();
    }
}

std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string w;
    while (in >> w) {
        out.push_back(w);
    }
    return out;
}

// The text after the first `count` words.
std::string rest_after(const std::string& line, std::size_t count) {
    std::size_t at = 0u;
    for (std::size_t i = 0; i < count; ++i) {
        while (at < line.size() && line[at] == ' ') {
            ++at;
        }
        while (at < line.size() && line[at] != ' ') {
            ++at;
        }
    }
    while (at < line.size() && line[at] == ' ') {
        ++at;
    }
    return line.substr(at);
}

bool number(const std::string& text, double& value) {
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return !text.empty() && end == text.c_str() + text.size() && std::isfinite(value);
}

std::uint32_t button(const std::string& name) {
    static constexpr std::pair<std::string_view, std::uint32_t> kButtons[] = {
        {"A", NEBULA_EXT_BUTTON_A},       {"B", NEBULA_EXT_BUTTON_B},       {"Z", NEBULA_EXT_BUTTON_Z},
        {"C", NEBULA_EXT_BUTTON_C},       {"PLUS", NEBULA_EXT_BUTTON_PLUS}, {"MINUS", NEBULA_EXT_BUTTON_MINUS},
        {"ONE", NEBULA_EXT_BUTTON_ONE},   {"TWO", NEBULA_EXT_BUTTON_TWO},   {"HOME", NEBULA_EXT_BUTTON_HOME},
        {"UP", NEBULA_EXT_BUTTON_UP},     {"DOWN", NEBULA_EXT_BUTTON_DOWN}, {"LEFT", NEBULA_EXT_BUTTON_LEFT},
        {"RIGHT", NEBULA_EXT_BUTTON_RIGHT}};
    for (const auto& [n, bit] : kButtons) {
        if (n == name) {
            return bit;
        }
    }
    return 0u;
}

std::uint32_t menu_key(const std::string& name) {
    static constexpr std::pair<std::string_view, std::uint32_t> kKeys[] = {
        {"up", NEBULA_EXT_KEY_UP},       {"down", NEBULA_EXT_KEY_DOWN},   {"left", NEBULA_EXT_KEY_LEFT},
        {"right", NEBULA_EXT_KEY_RIGHT}, {"enter", NEBULA_EXT_KEY_ENTER}, {"esc", NEBULA_EXT_KEY_ESCAPE},
        {"tab", NEBULA_EXT_KEY_TAB},     {"space", NEBULA_EXT_KEY_SPACE}, {"backspace", NEBULA_EXT_KEY_BACKSPACE}};
    for (const auto& [n, key] : kKeys) {
        if (n == name) {
            return key;
        }
    }
    return 0u;
}

// One line of the plan into the state; false and why when it is not a line this host half reads.
bool read_line(const std::string& line, std::string& why) {
    const std::vector<std::string> w = words(line);
    if (w.empty()) {
        return true;
    }
    double value = 0.0;
    if (w[0] == "harness") {
        if (w.size() != 2u || w[1] != "1") {
            why = "a plan for harness " + (w.size() > 1u ? w[1] : std::string("?")) + "; this host half reads 1";
            return false;
        }
        return true;
    }
    if (w[0] == "out") {
        g.out = rest_after(line, 1u);
        if (g.out.empty()) {
            why = "out names no folder";
            return false;
        }
        return true;
    }
    if (w[0] == "clock") {
        if (w.size() != 2u || w[1] != "title") {
            why = "a clock line is `clock title`";
            return false;
        }
        g.clock_title = true;
        return true;
    }
    if (w[0] == "bare") {
        g.bare = w.size() == 2u && w[1] == "1";
        return true;
    }
    if (w[0] == "setup") {
        g.setup.push_back(line);
        return true;
    }
    if (w[0] == "step" && w.size() >= 3u) {
        Step s;
        s.number = g.steps.size() + 1u;
        if (w[1] != "after") {
            if (!number(w[1], value) || value < 0.0 || value != std::floor(value)) {
                why = "step " + std::to_string(s.number) + ": at is a frame number";
                return false;
            }
            s.at = static_cast<std::int64_t>(value);
        }
        const std::string& a = w[2];
        s.text = rest_after(line, 3u);
        const std::vector<std::string> args = words(s.text);
        const auto frames_at = [&](std::size_t i) {
            return i < args.size() && number(args[i], value) && value >= 1.0 && value == std::floor(value)
                       ? static_cast<std::int64_t>(value) : std::int64_t{0};
        };
        if (a == "hold" && args.size() == 2u) {
            s.action = Action::Hold;
            std::stringstream names(args[0]);
            std::string n;
            while (std::getline(names, n, '+')) {
                const std::uint32_t bit = button(n);
                if (bit == 0u) {
                    why = "step " + std::to_string(s.number) + ": no button " + n;
                    return false;
                }
                s.buttons |= bit;
            }
            s.frames = frames_at(1u);
        } else if ((a == "stick" || a == "pointer") && args.size() == 3u) {
            s.action = a == "stick" ? Action::Stick : Action::Pointer;
            double x = 0.0, y = 0.0;
            if (!number(args[0], x) || !number(args[1], y)) {
                why = "step " + std::to_string(s.number) + ": " + a + " takes two numbers";
                return false;
            }
            s.x = static_cast<float>(x);
            s.y = static_cast<float>(y);
            s.frames = frames_at(2u);
        } else if (a == "shake" && args.size() == 1u) {
            s.action = Action::Shake;
            s.frames = frames_at(0u);
        } else if (a == "key" && args.size() == 1u) {
            s.action = Action::Key;
            s.key = menu_key(args[0]);
            if (s.key == 0u) {
                why = "step " + std::to_string(s.number) + ": no key " + args[0];
                return false;
            }
        } else if (a == "picture" && args.size() == 1u) {
            s.action = Action::Picture;
        } else if (a == "wait" && args.empty()) {
            s.action = Action::Wait;
        } else if (a == "end" && args.empty()) {
            s.action = Action::End;
        } else if (a == "game" && !args.empty()) {
            s.action = Action::Game;
        } else {
            why = "step " + std::to_string(s.number) + ": not an action this host half knows: " + a;
            return false;
        }
        if ((s.action == Action::Hold || s.action == Action::Stick || s.action == Action::Shake || s.action == Action::Pointer) &&
            s.frames == 0) {
            why = "step " + std::to_string(s.number) + ": frames is a whole number from 1";
            return false;
        }
        g.steps.push_back(std::move(s));
        return true;
    }
    if ((w[0] == "cond" || w[0] == "timeout") && !g.steps.empty() && !g.steps.back().at) {
        Step& s = g.steps.back();
        if (w[0] == "timeout") {
            if (w.size() != 2u || !number(w[1], value) || value < 1.0 || value != std::floor(value)) {
                why = "step " + std::to_string(s.number) + ": timeout is a whole number of frames";
                return false;
            }
            s.timeout = static_cast<std::int64_t>(value);
            return true;
        }
        Cond c;
        c.source = rest_after(line, 1u);
        if (w.size() >= 3u && w[1] == "log") {
            c.log = true;
            try {
                c.pattern = std::regex(rest_after(line, 2u), std::regex::ECMAScript);
            } catch (const std::regex_error&) {
                why = "step " + std::to_string(s.number) + ": the log pattern is not a regular expression";
                return false;
            }
        } else if (w.size() >= 5u && w[1] == "fact") {
            c.key = w[2];
            c.op = w[3];
            if (w[4] != "-") {
                double within = 0.0;
                if (!number(w[4], within)) {
                    why = "step " + std::to_string(s.number) + ": within is a number";
                    return false;
                }
                c.within = within;
            }
            const std::optional<Json> v = parse(rest_after(line, 5u));
            if (!v) {
                why = "step " + std::to_string(s.number) + ": the condition's value is not JSON";
                return false;
            }
            c.value = *v;
        } else {
            why = "step " + std::to_string(s.number) + ": a cond is `fact <key> <op> <within|-> <JSON>` or `log <pattern>`";
            return false;
        }
        s.conds.push_back(std::move(c));
        return true;
    }
    why = "not a plan line: " + line;
    return false;
}

// Whether a fact condition holds on the newest value.
bool holds(const Cond& c) {
    const auto found = g.latest.find(c.key);
    if (c.op == "absent") {
        return found == g.latest.end();
    }
    if (found == g.latest.end()) {
        return false;
    }
    if (c.op == "exists") {
        return true;
    }
    const std::optional<Json> v = parse(found->second);
    if (!v) {
        return false;
    }
    if (c.op == "is") {
        return equal(*v, c.value);
    }
    if (c.op == "is_not") {
        return !equal(*v, c.value);
    }
    if (c.op == "near") {
        const double within = c.within.value_or(0.0);
        if (v->kind == Json::Number && c.value.kind == Json::Number) {
            return std::fabs(v->number - c.value.number) <= within;
        }
        if (v->kind == Json::Array && c.value.kind == Json::Array && v->items.size() == c.value.items.size()) {
            double sum = 0.0;
            for (std::size_t i = 0; i < v->items.size(); ++i) {
                if (v->items[i].kind != Json::Number || c.value.items[i].kind != Json::Number) {
                    return false;
                }
                const double d = v->items[i].number - c.value.items[i].number;
                sum += d * d;
            }
            return std::sqrt(sum) <= within;
        }
        return false;
    }
    if (v->kind != Json::Number || c.value.kind != Json::Number) {
        return false;
    }
    if (c.op == "less") {
        return v->number < c.value.number;
    }
    if (c.op == "at_most") {
        return v->number <= c.value.number;
    }
    if (c.op == "more") {
        return v->number > c.value.number;
    }
    if (c.op == "at_least") {
        return v->number >= c.value.number;
    }
    return false;
}

void run_step(Step& s, std::int64_t now) {
    switch (s.action) {
    case Action::Hold: g.inputs.push_back({now, s.frames, s.buttons, false, false, false, 0.0f, 0.0f}); break;
    case Action::Shake: g.inputs.push_back({now, s.frames, 0u, true, false, false, 0.0f, 0.0f}); break;
    case Action::Stick: g.inputs.push_back({now, s.frames, 0u, false, true, false, s.x, s.y}); break;
    // The plan's pointer is in 0..1 of the screen; the host's in -1..1.
    case Action::Pointer: g.inputs.push_back({now, s.frames, 0u, false, false, true, s.x * 2.0f - 1.0f, s.y * 2.0f - 1.0f}); break;
    case Action::Key:
        if (g.host != nullptr && g.host->menu_key != nullptr) {
            g.host->menu_key(g.host->host, s.key);
        }
        break;
    case Action::Picture: g.pictures.push_back({g.game_frame, s.text}); break;
    case Action::Wait: break;
    case Action::End: finish({}, {}); return;
    case Action::Game:
        s.waiting = true;
        g.game_queue.push_back(s.number);
        return;  // done when the game half answers
    }
    s.done = true;
}

// --- The service the game half asks ------------------------------------------------------------------------------

const char* service_next() {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    g.given.clear();
    g.given_step = 0u;
    g.given_frame_line = false;
    if (g.finished) {
        return nullptr;
    }
    if (!g.base) {
        if (g.setup_given || g.next_setup >= g.setup.size()) {
            return nullptr;
        }
        g.setup_given = true;
        g.given = g.setup[g.next_setup];
        return g.given.c_str();
    }
    const std::int64_t now = test_frame();
    if (now != g.frame_sent) {
        g.frame_sent = now;
        g.given = "frame " + std::to_string(now);
        g.given_frame_line = true;
        return g.given.c_str();
    }
    if (g.game_queue.empty()) {
        return nullptr;
    }
    g.given_step = g.game_queue.front();
    g.given = g.steps[g.given_step - 1u].text;
    return g.given.c_str();
}

void service_done(std::int32_t ok, const char* why) {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (g.finished) {
        return;
    }
    const std::string reason = why != nullptr ? why : "";
    if (g.given_frame_line) {
        g.given_frame_line = false;
        if (ok == 0) {
            finish("harness-error", "the game half did not take the frame line: " + reason);
        }
        return;
    }
    if (!g.base) {
        if (!g.setup_given) {
            return;
        }
        const std::string item = g.setup[g.next_setup];
        g.setup_given = false;
        ++g.next_setup;
        if (ok == 0) {
            finish("setup-failed", item.substr(6) + ": " + reason);
            return;
        }
        if (item == "setup done") {
            g.setup_finished = true;
            if (!g.clock_title || g.title_seen) {
                set_base(g.game_frame + 1u);  // the game half answers after the game's update: frame 0 is the next one
            }
        }
        return;
    }
    if (g.given_step == 0u || g.game_queue.empty() || g.game_queue.front() != g.given_step) {
        return;
    }
    g.game_queue.pop_front();
    Step& s = g.steps[g.given_step - 1u];
    s.waiting = false;
    s.done = true;
    if (ok == 0) {
        finish("fail", "step " + std::to_string(s.number) + " (" + s.text + "): " + reason);
    }
}

void service_fact(const char* key, const char* json) {
    if (key == nullptr || json == nullptr) {
        return;
    }
    const std::string k = key;
    write_fact(k, json, k == "setup.done" ? std::optional<std::int64_t>(0) : std::nullopt);
}

}  // namespace

bool start(const NebulaHostForExtension* host, const std::string& plan, void (*log)(const std::string&), void (*quit)()) {
    if (plan.empty()) {
        return false;
    }
    g.host = host;
    g.log = log;
    g.quit = quit;
    g.on = true;
    std::ifstream in(plan, std::ios::binary);
    if (!in) {
        log("harness: the plan cannot be read: " + plan);
        finish("harness-error", "the plan cannot be read");
        return false;
    }
    std::string line, why;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!read_line(line, why)) {
            break;
        }
    }
    if (g.out.empty() && why.empty()) {
        why = "the plan has no out line";
    }
    if (!g.out.empty()) {
        g.facts.open(g.out + "/facts.jsonl", std::ios::binary | std::ios::trunc);
        if (!g.facts.is_open()) {
            why = "facts.jsonl cannot be written in " + g.out;
        }
    }
    if (g.bare && !g.setup.empty()) {
        why = "a bare plan has setup lines";
    }
    std::size_t ends = 0u;
    for (const Step& s : g.steps) {
        ends += s.action == Action::End ? 1u : 0u;
        if (g.bare && s.action == Action::Game) {
            why = "a bare plan has a game step";
        }
        if (!s.at && s.conds.empty()) {
            why = "step " + std::to_string(s.number) + " is an after step with no cond";
        }
    }
    if (why.empty() && ends != 1u) {
        why = "the plan has " + std::to_string(ends) + " end steps; it needs exactly one";
    }
    if (!why.empty()) {
        log("harness: " + why);
        finish("harness-error", why);
        return false;
    }
    g_service.size = sizeof(LumaHarness);
    g_service.next = service_next;
    g_service.done = service_done;
    g_service.fact = service_fact;
    log("harness: plan read: " + std::to_string(g.setup.size()) + " setup lines, " + std::to_string(g.steps.size()) + " steps");
    return true;
}

bool on() {
    return g.on;
}

void frame(std::uint64_t game_frames) {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (!g.on || g.finished) {
        return;
    }
    g.game_frame = game_frames;
    if (!g.base && (g.bare || g.setup_finished) && (!g.clock_title || g.title_seen)) {
        set_base(game_frames);
    }
    if (!g.base) {
        return;
    }
    const std::int64_t now = test_frame();
    bool before_done = true;  // every step written before this one has run
    for (Step& s : g.steps) {
        if (g.finished) {
            return;
        }
        if (!s.done && !s.waiting) {
            if (s.at) {
                if (*s.at <= now) {
                    run_step(s, now);
                }
            } else if (before_done) {
                if (!s.armed) {
                    s.armed = now;
                }
                bool all = true;
                for (const Cond& c : s.conds) {
                    all = all && (c.log ? false : holds(c));
                }
                // A log condition is met by log_line (it clears the cond); none left means met.
                for (const Cond& c : s.conds) {
                    if (c.log) {
                        all = false;
                    }
                }
                if (all) {
                    run_step(s, now);
                } else if (now - *s.armed > s.timeout) {
                    std::string conds;
                    for (const Cond& c : s.conds) {
                        conds += (conds.empty() ? "" : "; ") + c.source;
                    }
                    finish("fail", "step " + std::to_string(s.number) + ": its after (" + conds + ") did not hold within " +
                                       std::to_string(s.timeout) + " frames");
                    return;
                }
            }
        }
        before_done = before_done && s.done;
    }
}

void controls(std::uint64_t game_frames, NebulaExtensionControls* out) {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (!g.on || !g.base || out == nullptr) {
        return;
    }
    const std::int64_t now = static_cast<std::int64_t>(game_frames) - static_cast<std::int64_t>(*g.base);
    for (const Input& i : g.inputs) {
        if (now < i.start || now - i.start >= i.frames) {
            continue;
        }
        out->buttons |= i.buttons;
        if (i.shake) {
            out->shake = 1u;
        }
        if (i.stick) {
            out->has_stick = 1u;
            out->stick_x = i.x;
            out->stick_y = i.y;
        }
        if (i.pointer) {
            out->has_pointer = 1u;
            out->pointer_x = i.x;
            out->pointer_y = i.y;
        }
    }
}

const char* picture_path(std::uint64_t game_frame, std::uint32_t kind) {
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (!g.on || g.pictures.empty() || kind == NEBULA_EXT_PICTURE_GENERATED || game_frame < g.pictures.front().game_frame) {
        return nullptr;
    }
    const Picture p = g.pictures.front();
    g.pictures.pop_front();
    g.picture_file = g.out + "/pictures/" + p.name + ".ppm";
    write_fact("picture." + p.name, "{\"game_frame\":" + std::to_string(game_frame) + ",\"asked_at\":" +
                                        std::to_string(static_cast<long long>(p.game_frame) - static_cast<long long>(g.base.value_or(0u))) + "}");
    return g.picture_file.c_str();
}

void log_line(const char* line) {
    if (line == nullptr) {
        return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    if (!g.on || g.finished) {
        return;
    }
    const std::string text = line;
    if (text.compare(0, 18u, "boot: title screen") == 0) {
        g.title_seen = true;
    }
    // A mod's fact: "game: [mod <id>] fact <key> <JSON>".
    static const std::string kMod = "game: [mod ";
    if (text.compare(0, kMod.size(), kMod) == 0) {
        const std::size_t close = text.find("] ", kMod.size());
        if (close != std::string::npos && text.compare(close + 2u, 5u, "fact ") == 0) {
            const std::string id = text.substr(kMod.size(), close - kMod.size());
            const std::string rest = text.substr(close + 7u);
            const std::size_t space = rest.find(' ');
            const std::string key = space == std::string::npos ? rest : rest.substr(0, space);
            const std::string json = space == std::string::npos ? std::string() : rest.substr(space + 1u);
            if (!key.empty() && parse(json)) {
                write_fact(id + "." + key, json);
            } else {
                write_fact("harness.bad_fact", quoted(text));
            }
        }
    }
    // A mod's start or refusal: "mods: <id> (<file>) started" or "... not started: <why>".
    static const std::string kMods = "mods: ";
    if (text.compare(0, kMods.size(), kMods) == 0) {
        const std::size_t open = text.find(" (", kMods.size());
        const std::size_t close = open == std::string::npos ? open : text.find(") ", open);
        if (close != std::string::npos) {
            const std::string id = text.substr(kMods.size(), open - kMods.size());
            const std::string tail = text.substr(close + 2u);
            if (tail == "started") {
                write_fact("mod." + id + ".loaded", "true");
            } else if (tail.compare(0, 13u, "not started: ") == 0) {
                write_fact("mod." + id + ".loaded", quoted(tail.substr(13u)));
            }
        } else {
            // "mods: <id>: <why>": a mod refused before its code was started (the Mods page's reason, in capitals) or
            // whose code the player has not allowed.
            const std::size_t colon = text.find(": ", kMods.size());
            const std::string id = colon == std::string::npos ? std::string() : text.substr(kMods.size(), colon - kMods.size());
            const std::string why = colon == std::string::npos ? std::string() : text.substr(colon + 2u);
            bool capitals = !why.empty();
            for (const char c : why) {
                capitals = capitals && !(c >= 'a' && c <= 'z');
            }
            if (!id.empty() && id.find(' ') == std::string::npos && (capitals || why.compare(0, 18u, "its code isn't run") == 0)) {
                write_fact("mod." + id + ".loaded", quoted(why));
            }
        }
    }
    // An after step waiting on a log line: met when the line is written, run at the next frame.
    for (Step& s : g.steps) {
        if (s.done || s.at || !s.armed) {
            continue;
        }
        for (auto it = s.conds.begin(); it != s.conds.end();) {
            if (it->log && std::regex_search(text, it->pattern)) {
                it = s.conds.erase(it);
            } else {
                ++it;
            }
        }
    }
}

const void* service(const char* name, std::uint32_t major, std::uint32_t /*minor*/) {
    if (!g.on || name == nullptr || std::strcmp(name, LUMA_HARNESS_NAME) != 0 || major != LUMA_HARNESS_MAJOR) {
        return nullptr;
    }
    return &g_service;
}

}  // namespace harness
