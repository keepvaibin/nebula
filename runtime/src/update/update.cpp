// SPDX-License-Identifier: GPL-3.0-only
// The update check's rules and its service (update.h). The platform's transport and install step are in
// update_transport_*.cpp and update_install_*.cpp.
#include "update_internal.h"

#include "galaxy/atomic_file.h"
#include "galaxy/platform_paths.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <variant>

namespace galaxy::update {

namespace {

constexpr int kCheckTimeoutMs = 15000;
constexpr std::int64_t kCheckIntervalSeconds = 3600;
constexpr std::size_t kMaxListingBytes = 8u << 20;   // a listing is a few hundred kilobytes
constexpr std::size_t kMaxManifestBytes = 4096u;
constexpr std::size_t kMaxSignatureBytes = 65536u;
constexpr std::uint64_t kMaxPackageBytes = 2ull << 30;  // a release without a size in the listing
constexpr std::size_t kNotesLimit = 1500u;
// As the Windows launcher reads it (Updater.ReleasesUrl, BuildInfo.Repository).
constexpr const char* kReleasesUrl = "https://api.github.com/repos/keepvaibin/nebula/releases?per_page=30";

// ---- JSON: the subset a release listing and update-check.json use ----

struct Json;
using JsonObject = std::map<std::string, Json, std::less<>>;
using JsonArray = std::vector<Json>;
struct Json {
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> value;
    [[nodiscard]] const Json* get(std::string_view key) const {
        const auto* object = std::get_if<JsonObject>(&value);
        if (object == nullptr) return nullptr;
        const auto found = object->find(key);
        return found == object->end() ? nullptr : &found->second;
    }
    [[nodiscard]] std::string text(std::string_view key) const {
        const Json* item = get(key);
        const auto* s = item != nullptr ? std::get_if<std::string>(&item->value) : nullptr;
        return s != nullptr ? *s : std::string();
    }
    [[nodiscard]] double number(std::string_view key) const {
        const Json* item = get(key);
        const auto* n = item != nullptr ? std::get_if<double>(&item->value) : nullptr;
        return n != nullptr ? *n : 0.0;
    }
    [[nodiscard]] bool flag(std::string_view key, bool fallback = false) const {
        const Json* item = get(key);
        const auto* b = item != nullptr ? std::get_if<bool>(&item->value) : nullptr;
        return b != nullptr ? *b : fallback;
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}
    bool read(Json& out) {
        if (!value(out, 0)) return false;
        space();
        return at_ == text_.size();
    }

private:
    void space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\r' || text_[at_] == '\n')) ++at_;
    }
    bool literal(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) return false;
        at_ += word.size();
        return true;
    }
    static void append_utf8(std::string& out, std::uint32_t code) {
        if (code < 0x80u) out += static_cast<char>(code);
        else if (code < 0x800u) {
            out += static_cast<char>(0xC0u | (code >> 6));
            out += static_cast<char>(0x80u | (code & 0x3Fu));
        } else if (code < 0x10000u) {
            out += static_cast<char>(0xE0u | (code >> 12));
            out += static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (code & 0x3Fu));
        } else {
            out += static_cast<char>(0xF0u | (code >> 18));
            out += static_cast<char>(0x80u | ((code >> 12) & 0x3Fu));
            out += static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (code & 0x3Fu));
        }
    }
    bool hex4(std::uint32_t& code) {
        if (at_ + 4u > text_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[at_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (at_ >= text_.size() || text_[at_] != '"') return false;
        ++at_;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20u) return false;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) return false;
            const char e = text_[at_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t code = 0;
                if (!hex4(code)) return false;
                if (code >= 0xD800u && code < 0xDC00u) {
                    std::uint32_t low = 0;
                    if (!literal("\\u") || !hex4(low) || low < 0xDC00u || low >= 0xE000u) return false;
                    code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
                } else if (code >= 0xDC00u && code < 0xE000u) {
                    return false;
                }
                append_utf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool value(Json& out, int depth) {
        if (depth > 64) return false;
        space();
        if (at_ >= text_.size()) return false;
        const char c = text_[at_];
        if (c == '{') {
            ++at_;
            JsonObject object;
            space();
            if (at_ < text_.size() && text_[at_] == '}') {
                ++at_;
                out.value = std::move(object);
                return true;
            }
            for (;;) {
                space();
                std::string key;
                if (!string(key)) return false;
                space();
                if (at_ >= text_.size() || text_[at_++] != ':') return false;
                Json item;
                if (!value(item, depth + 1)) return false;
                object[std::move(key)] = std::move(item);
                space();
                if (at_ >= text_.size()) return false;
                if (text_[at_] == ',') { ++at_; continue; }
                if (text_[at_] == '}') { ++at_; break; }
                return false;
            }
            out.value = std::move(object);
            return true;
        }
        if (c == '[') {
            ++at_;
            JsonArray array;
            space();
            if (at_ < text_.size() && text_[at_] == ']') {
                ++at_;
                out.value = std::move(array);
                return true;
            }
            for (;;) {
                Json item;
                if (!value(item, depth + 1)) return false;
                array.push_back(std::move(item));
                space();
                if (at_ >= text_.size()) return false;
                if (text_[at_] == ',') { ++at_; continue; }
                if (text_[at_] == ']') { ++at_; break; }
                return false;
            }
            out.value = std::move(array);
            return true;
        }
        if (c == '"') {
            std::string s;
            if (!string(s)) return false;
            out.value = std::move(s);
            return true;
        }
        if (literal("true")) { out.value = true; return true; }
        if (literal("false")) { out.value = false; return true; }
        if (literal("null")) { out.value = nullptr; return true; }
        const std::size_t start = at_;
        while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) || text_[at_] == '-' || text_[at_] == '+' ||
                                      text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E')) ++at_;
        if (at_ == start) return false;
        const std::string number(text_.substr(start, at_ - start));
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end != number.c_str() + number.size()) return false;
        out.value = parsed;
        return true;
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

void write_json_string(std::string& out, std::string_view text) {
    out += '"';
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
    out += '"';
}

// ---- URLs ----

bool served_safely(std::string_view url) {
    if (url.starts_with("https://")) return url.size() > 8u;
    return url.starts_with("http://127.0.0.1:") || url.starts_with("http://127.0.0.1/");  // the tests' server
}

}  // namespace

// ---- Rules ----

int compare_versions(std::string_view a, std::string_view b) noexcept {
    // MAJOR.MINOR.PATCH[-label.N]: a release sorts after its previews (Updater.CompareVersions).
    const auto key = [](std::string_view version) {
        std::array<std::int64_t, 5> k{};
        if (version.empty()) return k;  // before every version, as Updater.CompareVersions has it
        const std::size_t dash = version.find('-');
        const std::string_view numbers = version.substr(0, dash);
        std::size_t at = 0;
        for (int i = 0; i < 3 && at <= numbers.size(); ++i) {
            const std::size_t dot = numbers.find('.', at);
            const std::string_view part = numbers.substr(at, dot == std::string_view::npos ? std::string_view::npos : dot - at);
            std::int64_t value = 0;
            bool digits = !part.empty();
            for (const char c : part) {
                if (c < '0' || c > '9' || value > 1'000'000'000) { digits = false; break; }
                value = value * 10 + (c - '0');
            }
            k[static_cast<std::size_t>(i)] = digits ? value : 0;
            if (dot == std::string_view::npos) break;
            at = dot + 1u;
        }
        k[3] = dash == std::string_view::npos ? 1 : 0;
        if (dash != std::string_view::npos) {
            const std::string_view label = version.substr(dash + 1u);
            const std::size_t dot = label.rfind('.');
            if (dot != std::string_view::npos) {
                std::int64_t value = 0;
                bool digits = dot + 1u < label.size();
                for (const char c : label.substr(dot + 1u)) {
                    if (c < '0' || c > '9' || value > 1'000'000'000) { digits = false; break; }
                    value = value * 10 + (c - '0');
                }
                k[4] = digits ? value : 0;
            }
        }
        return k;
    };
    const auto x = key(a), y = key(b);
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

bool is_beta(std::string_view version) noexcept {
    for (std::size_t i = 0; i + 6u <= version.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < 6u && match; ++j) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(version[i + j])));
            match = c == "-beta."[j];
        }
        if (match) return true;
    }
    return false;
}

std::optional<Release> select_newer(std::string_view listing_json, std::string_view installed, std::string_view asset, bool& is_list) {
    Json root;
    is_list = false;
    if (!JsonReader(listing_json).read(root)) return std::nullopt;
    const auto* list = std::get_if<JsonArray>(&root.value);
    if (list == nullptr) return std::nullopt;
    is_list = true;
    const bool beta = is_beta(installed);
    std::optional<Release> best;
    for (const Json& item : *list) {
        if (!std::holds_alternative<JsonObject>(item.value) || item.flag("draft")) continue;
        const std::string tag = item.text("tag_name");
        const std::string version = tag.starts_with('v') ? tag.substr(1) : tag;
        if (version.empty() || is_beta(version) != beta) continue;
        if (compare_versions(version, best ? std::string_view(best->version) : installed) <= 0) continue;
        Release release;
        release.version = version;
        release.tag = tag;
        const Json* assets = item.get("assets");
        const auto* array = assets != nullptr ? std::get_if<JsonArray>(&assets->value) : nullptr;
        if (array == nullptr) continue;
        for (const Json& entry : *array) {
            const std::string name = entry.text("name");
            const std::string url = entry.text("browser_download_url");
            if (name == asset) {
                release.package_url = url;
                const double size = entry.number("size");
                release.package_bytes = size > 0.0 && size < 1e15 ? static_cast<std::uint64_t>(size) : 0u;
            } else if (name.size() == asset.size() + 9u && name.starts_with(asset) && name.ends_with(".manifest")) {
                release.manifest_url = url;
            } else if (name.size() == asset.size() + 13u && name.starts_with(asset) && name.ends_with(".manifest.sig")) {
                release.manifest_signature_url = url;
            }
        }
        if (!served_safely(release.package_url) || !served_safely(release.manifest_url) || !served_safely(release.manifest_signature_url)) continue;
        release.notes = plain_notes(item.text("body"), kNotesLimit);
        best = std::move(release);
    }
    return best;
}

std::string plain_notes(std::string_view markdown, std::size_t limit) {
    // As the Windows prompt shows them (ReleaseNotes.Plain): quotes and headings as their text, list items with a
    // dash, links as their text (and address when it differs), no emphasis or code marks, rules and repeated blank
    // lines dropped.
    std::string out;
    bool blank = true;
    std::size_t at = 0;
    while (at <= markdown.size()) {
        std::size_t end = markdown.find('\n', at);
        if (end == std::string_view::npos) end = markdown.size();
        std::string line(markdown.substr(at, end - at));
        at = end + 1u;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        while (line.starts_with('>')) {
            line.erase(0, 1);
            line.erase(0, line.find_first_not_of(" \t") == std::string::npos ? line.size() : line.find_first_not_of(" \t"));
        }
        {
            std::size_t hashes = 0;
            while (hashes < line.size() && line[hashes] == '#') ++hashes;
            if (hashes >= 1u && hashes <= 6u && hashes < line.size() && (line[hashes] == ' ' || line[hashes] == '\t')) {
                line.erase(0, line.find_first_not_of(" \t", hashes));
            }
        }
        {
            const std::size_t indent = line.find_first_not_of(" \t");
            if (indent != std::string::npos && indent + 1u < line.size() && (line[indent] == '*' || line[indent] == '+') &&
                (line[indent + 1u] == ' ' || line[indent + 1u] == '\t')) {
                line[indent] = '-';
            }
        }
        // A rule: three or more of the same mark and nothing else.
        {
            std::size_t marks = 0;
            bool only = !line.empty();
            for (const char c : line) {
                if (c == '-' || c == '*' || c == '_') ++marks;
                else if (c != ' ' && c != '\t') { only = false; break; }
            }
            if (only && marks >= 3u) line.clear();
        }
        std::string plain;
        for (std::size_t i = 0; i < line.size();) {
            // [text](address)
            if (line[i] == '[') {
                const std::size_t close = line.find("](", i + 1u);
                const std::size_t paren = close == std::string::npos ? std::string::npos : line.find(')', close + 2u);
                if (paren != std::string::npos) {
                    const std::string text = line.substr(i + 1u, close - i - 1u);
                    const std::string address = line.substr(close + 2u, paren - close - 2u);
                    plain += text == address ? address : text + " (" + address + ")";
                    i = paren + 1u;
                    continue;
                }
            }
            // Emphasis and code marks: * _ ` around words (an underscore inside a word stays).
            if (line[i] == '*' || line[i] == '`') {
                ++i;
                continue;
            }
            if (line[i] == '_') {
                const bool inside = i > 0u && i + 1u < line.size() && std::isalnum(static_cast<unsigned char>(line[i - 1u])) &&
                                    std::isalnum(static_cast<unsigned char>(line[i + 1u]));
                if (!inside) {
                    ++i;
                    continue;
                }
            }
            plain += line[i++];
        }
        if (plain.empty()) {
            if (!blank) out += '\n';
            blank = true;
        } else {
            out += plain;
            out += '\n';
            blank = false;
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    if (out.size() <= limit) return out;
    std::size_t cut = out.rfind('\n', limit);
    if (cut == std::string::npos || cut <= limit / 2u) {
        cut = limit;
        while (cut > 0u && (static_cast<unsigned char>(out[cut]) & 0xC0u) == 0x80u) --cut;  // not inside a character
    }
    out.resize(cut);
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    out += "\n\xE2\x80\xA6";  // …
    return out;
}

// ---- The service ----

namespace {

struct Kept {
    std::optional<Release> release;
    std::int64_t checked = 0;  // seconds since 1970, UTC
    std::string skipped;
    bool enabled = true;
    bool readable = false;
};

struct Check {
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    bool failed = false;
    bool unreachable = false;  // the probe's verdict, when it came first
    std::optional<Release> release;
};

struct Service {
    std::mutex mutex;
    std::string installed;
    std::string asset;
    std::shared_ptr<Check> check;
    std::thread worker, probe;
    detail::Transport* transport = nullptr;
    std::filesystem::path data;
    // update-check.json as begin read it, and as this process has written it since (offer reads no file).
    std::optional<Kept> kept;
    // The install.
    std::thread installer;
    std::atomic_bool cancel{false};
    InstallProgress progress;
    bool installing = false;
};

Service& service() {
    static Service* s = new Service();  // never destroyed: threads may still hold it at exit
    return *s;
}

detail::Transport& transport_of(Service& s) { return s.transport != nullptr ? *s.transport : detail::platform_transport(); }

std::filesystem::path cache_path(Service& s) {
    const std::filesystem::path data = !s.data.empty() ? s.data : nebula_data_directory();
    return data.empty() ? std::filesystem::path() : data / "update-check.json";
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ISO 8601 UTC ("2026-11-13T18:02:11Z", with or without fractions, as Windows writes "o"), to seconds since 1970.
bool parse_time(std::string_view text, std::int64_t& seconds) {
    // YYYY-MM-DDTHH:MM:SS, digits where the pattern has 9.
    constexpr std::string_view kPattern = "9999-99-99T99:99:99";
    if (text.size() < kPattern.size()) return false;
    for (std::size_t i = 0; i < kPattern.size(); ++i) {
        const bool digit = text[i] >= '0' && text[i] <= '9';
        if (kPattern[i] == '9' ? !digit : text[i] != kPattern[i]) return false;
    }
    const auto field = [&](std::size_t at, std::size_t count) {
        int value = 0;
        for (std::size_t i = at; i < at + count; ++i) value = value * 10 + (text[i] - '0');
        return value;
    };
    const int year = field(0, 4), month = field(5, 2), day = field(8, 2), hour = field(11, 2), minute = field(14, 2), second = field(17, 2);
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    // Days from civil (Howard Hinnant's algorithm).
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
    seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

std::string format_time(std::int64_t seconds) {
    const std::int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    const std::int64_t rest = seconds - days * 86400;
    // Civil from days (Howard Hinnant's algorithm).
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
    char text[40];
    std::snprintf(text, sizeof(text), "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ", static_cast<long long>(y), m, d,
                  static_cast<long long>(rest / 3600), static_cast<long long>(rest / 60 % 60), static_cast<long long>(rest % 60));
    return text;
}

Kept read_kept(Service& s) {
    Kept kept;
    const std::filesystem::path path = cache_path(s);
    if (path.empty()) return kept;
    std::ifstream in(path, std::ios::binary);
    if (!in) return kept;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() > kMaxListingBytes) return kept;
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);  // a byte order mark
    Json root;
    if (!JsonReader(text).read(root) || !std::holds_alternative<JsonObject>(root.value)) return kept;
    kept.enabled = root.flag("update_check", true);
    kept.skipped = root.text("skipped");
    if (!parse_time(root.text("checkedUtc"), kept.checked)) return kept;
    kept.readable = true;
    const std::string version = root.text("version");
    if (version.empty()) return kept;
    Release release;
    release.version = version;
    release.tag = root.text("tag");
    release.notes = root.text("notes");
    release.package_url = root.text("setupUrl");
    release.manifest_signature_url = root.text("signatureUrl");
    release.manifest_url = root.text("manifestUrl");
    const double size = root.number("setupSize");
    release.package_bytes = size > 0.0 && size < 1e15 ? static_cast<std::uint64_t>(size) : 0u;
    if (!release.package_url.empty() && !release.manifest_url.empty() && !release.manifest_signature_url.empty()) {
        kept.release = std::move(release);
    }
    return kept;
}

void write_kept(Service& s, const Kept& kept) {
    const std::filesystem::path path = cache_path(s);
    if (path.empty()) return;
    std::string out = "{\n  \"checkedUtc\": ";
    write_json_string(out, format_time(kept.checked));
    if (!kept.enabled) out += ",\n  \"update_check\": false";
    if (!kept.skipped.empty()) {
        out += ",\n  \"skipped\": ";
        write_json_string(out, kept.skipped);
    }
    if (kept.release) {
        const Release& r = *kept.release;
        const std::pair<const char*, const std::string*> fields[] = {
            {"version", &r.version}, {"tag", &r.tag}, {"notes", &r.notes}, {"setupUrl", &r.package_url},
            {"signatureUrl", &r.manifest_signature_url}, {"manifestUrl", &r.manifest_url}};
        for (const auto& [name, value] : fields) {
            out += ",\n  \"";
            out += name;
            out += "\": ";
            write_json_string(out, *value);
        }
        out += ",\n  \"setupSize\": " + std::to_string(r.package_bytes);
        out += ",\n  \"prerelease\": ";
        out += is_beta(r.version) ? "true" : "false";
    }
    out += "\n}\n";
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    (void)io::write_file_atomically(path, out);
}

bool offered(const std::optional<Release>& release, std::string_view installed, std::string_view skipped) {
    if (!release || compare_versions(release->version, installed) <= 0 || is_beta(release->version) != is_beta(installed)) return false;
    return skipped.empty() || compare_versions(release->version, skipped) > 0;
}

bool turned_off() {
#if defined(_WIN32)
    char* off = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&off, &length, "NEBULA_NO_UPDATE_CHECK") != 0 || off == nullptr) return false;
    const bool on = off[0] != '\0' && off[0] != '0';
    std::free(off);
    return on;
#else
    const char* off = std::getenv("NEBULA_NO_UPDATE_CHECK");
    return off != nullptr && off[0] != '\0' && off[0] != '0';
#endif
}

void join_quietly(std::thread& thread) {
    if (thread.joinable()) thread.join();
}

}  // namespace

void begin(std::string_view installed_version) {
    Service& s = service();
    std::lock_guard lock(s.mutex);
    if (s.check || turned_off()) return;
    s.installed = std::string(installed_version);
    s.asset = detail::platform_asset(is_beta(installed_version));
    if (s.asset.empty()) return;
    s.kept = read_kept(s);
    const Kept& kept = *s.kept;
    const std::int64_t now = now_seconds();
    if (!kept.enabled) return;
    if (kept.readable && kept.checked <= now && now - kept.checked < kCheckIntervalSeconds) return;
    auto check = std::make_shared<Check>();
    s.check = check;
    detail::Transport* transport = &transport_of(s);
    const std::string installed = s.installed, asset = s.asset;
    s.worker = std::thread([check, transport, installed, asset] {
        std::string body;
        const int status = transport->get(kReleasesUrl, body, kMaxListingBytes, kCheckTimeoutMs);
        bool is_list = false;
        std::optional<Release> release;
        if (status == 200) release = select_newer(body, installed, asset, is_list);
        Service& owner = service();
        if (is_list) {
            // Kept as the next start's answer; a failed check (offline, portal, rate limit) keeps the last one.
            std::lock_guard service_lock(owner.mutex);
            Kept next = owner.kept ? *owner.kept : read_kept(owner);
            next.release = release;
            next.checked = now_seconds();
            next.readable = true;
            write_kept(owner, next);
            owner.kept = std::move(next);
        }
        std::lock_guard check_lock(check->mutex);
        check->done = true;
        check->failed = !is_list;
        check->release = std::move(release);
        check->changed.notify_all();
    });
    s.probe = std::thread([check, transport] {
        const bool unreachable = transport->unreachable(kReleasesUrl);
        std::lock_guard check_lock(check->mutex);
        if (!unreachable) return;
        check->unreachable = true;
        check->changed.notify_all();
    });
}

std::optional<Release> offer(int budget_ms) {
    Service& s = service();
    std::shared_ptr<Check> check;
    std::string installed;
    {
        std::lock_guard lock(s.mutex);
        check = s.check;
        installed = s.installed;
    }
    if (installed.empty()) return std::nullopt;  // begin was not called, or the check is off
    if (check) {
        std::unique_lock lock(check->mutex);
        check->changed.wait_for(lock, std::chrono::milliseconds(std::max(0, budget_ms)),
                                [&] { return check->done || check->unreachable; });
        if (check->done && !check->failed) {
            std::lock_guard service_lock(s.mutex);
            const std::string skipped = s.kept ? s.kept->skipped : std::string();
            return offered(check->release, installed, skipped) ? check->release : std::nullopt;
        }
    }
    std::lock_guard service_lock(s.mutex);
    if (!s.kept) return std::nullopt;
    return offered(s.kept->release, installed, s.kept->skipped) ? s.kept->release : std::nullopt;
}

std::optional<Release> poll() { return offer(0); }

void skip(std::string_view version) {
    Service& s = service();
    std::lock_guard lock(s.mutex);
    Kept kept = s.kept ? *s.kept : read_kept(s);
    if (!kept.readable) kept.checked = 0;  // no answer yet: the next start checks
    if (!kept.skipped.empty() && compare_versions(kept.skipped, version) >= 0) return;
    kept.skipped = std::string(version);
    write_kept(s, kept);
    s.kept = std::move(kept);
}

void finish() {
    Service& s = service();
    std::shared_ptr<Check> check;
    {
        std::lock_guard lock(s.mutex);
        check = s.check;
    }
    if (!check) return;
    std::unique_lock lock(check->mutex);
    check->changed.wait_for(lock, std::chrono::milliseconds(kCheckTimeoutMs + 1000), [&] { return check->done; });
}

// ---- The install ----

bool can_install() noexcept { return detail::platform_can_install(); }

InstallProgress install_progress() {
    Service& s = service();
    std::lock_guard lock(s.mutex);
    return s.progress;
}

void cancel_install() { service().cancel.store(true, std::memory_order_relaxed); }

namespace {

void set_progress(InstallState state, std::uint64_t bytes, std::uint64_t total, std::string error = {}) {
    Service& s = service();
    std::lock_guard lock(s.mutex);
    s.progress = {state, bytes, total, std::move(error)};
    if (state == InstallState::Restart || state == InstallState::Failed || state == InstallState::Canceled) s.installing = false;
}

bool file_sha256(const std::filesystem::path& path, std::string& hex, std::uint64_t& size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    detail::Sha256 sha;
    std::vector<char> buffer(1u << 16);
    size = 0;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        sha.update(buffer.data(), static_cast<std::size_t>(got));
        size += static_cast<std::uint64_t>(got);
    }
    const auto digest = sha.finish();
    static constexpr char kHex[] = "0123456789abcdef";
    hex.clear();
    for (const std::uint8_t byte : digest) {
        hex += kHex[byte >> 4];
        hex += kHex[byte & 15u];
    }
    return true;
}

void install(Release release, std::string installed, detail::Transport* transport, std::filesystem::path staging) {
    Service& s = service();
    const auto canceled = [&] { return s.cancel.load(std::memory_order_relaxed); };
    // The signed manifest first: what it names is the only package this install takes.
    std::string manifest, signature;
    if (transport->get(release.manifest_url, manifest, kMaxManifestBytes, kCheckTimeoutMs) != 200 ||
        transport->get(release.manifest_signature_url, signature, kMaxSignatureBytes, kCheckTimeoutMs) != 200) {
        set_progress(canceled() ? InstallState::Canceled : InstallState::Failed, 0, release.package_bytes, "manifest download failed");
        return;
    }
    while (!signature.empty() && (signature.back() == '\n' || signature.back() == '\r' || signature.back() == ' ')) signature.pop_back();
    if (!verify_signature(manifest, signature)) {
        set_progress(InstallState::Failed, 0, release.package_bytes, "the manifest is not signed by the Nebula release key");
        return;
    }
    std::map<std::string, std::string, std::less<>> fields;
    {
        std::istringstream lines(manifest);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t equals = line.find('=');
            if (equals != std::string::npos) fields[line.substr(0, equals)] = line.substr(equals + 1u);
        }
    }
    const std::string version = fields["version"], sha = fields["sha256"];
    std::uint64_t size = 0;
    try { size = std::stoull(fields["size"]); } catch (...) { size = 0; }
    // Monotonic: the signed version is this release's and newer than the running one (no replay, no downgrade).
    if (version != release.version || compare_versions(version, installed) <= 0 || sha.size() != 64u || size == 0u || size > kMaxPackageBytes) {
        set_progress(InstallState::Failed, 0, size, "the signed manifest names another version");
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(staging, error);
    const std::filesystem::path package = staging / ("update-" + version + ".partial");
    set_progress(InstallState::Downloading, 0, size);
    for (int attempt = 0; attempt < 2; ++attempt) {
        const int status = transport->download(release.package_url, package, size, s.cancel, [size](std::uint64_t bytes) {
            set_progress(InstallState::Downloading, bytes, size);
        });
        if (canceled()) {
            set_progress(InstallState::Canceled, 0, size);
            return;
        }
        set_progress(InstallState::Verifying, size, size);
        std::string hex;
        std::uint64_t got = 0;
        if ((status == 200 || status == 206 || status == 416) && file_sha256(package, hex, got) && got == size && hex == sha) {
            set_progress(InstallState::Installing, size, size);
            std::string why;
            if (!detail::platform_install(package, why)) {
                std::filesystem::remove(package, error);
                set_progress(InstallState::Failed, size, size, why);
                return;
            }
            set_progress(InstallState::Restart, size, size);
            return;
        }
        // Not the signed bytes (a corrupt or replaced partial file): once more from the start.
        std::filesystem::remove(package, error);
    }
    set_progress(InstallState::Failed, 0, size, "the download does not match the signed manifest");
}

}  // namespace

bool start_install(const Release& release) {
    if (!detail::platform_can_install()) return false;
    Service& s = service();
    std::lock_guard lock(s.mutex);
    const std::filesystem::path data = !s.data.empty() ? s.data : nebula_data_directory();
    if (s.installing || s.installed.empty() || data.empty()) return false;
    join_quietly(s.installer);
    s.installing = true;
    s.cancel.store(false, std::memory_order_relaxed);
    s.progress = {InstallState::Downloading, 0, release.package_bytes, {}};
    s.installer = std::thread(install, release, s.installed, &transport_of(s), data / "update-staging");
    return true;
}

namespace detail {

void set_transport_for_tests(Transport* transport) { service().transport = transport; }
void set_data_directory_for_tests(const std::filesystem::path& directory) { service().data = directory; }

void reset_for_tests() {
    Service& s = service();
    std::thread worker, probe, installer;
    {
        std::lock_guard lock(s.mutex);
        worker = std::move(s.worker);
        probe = std::move(s.probe);
        installer = std::move(s.installer);
    }
    join_quietly(worker);
    join_quietly(probe);
    join_quietly(installer);
    std::lock_guard lock(s.mutex);
    s.check.reset();
    s.kept.reset();
    s.installed.clear();
    s.asset.clear();
    s.progress = {};
    s.installing = false;
}

}  // namespace detail

}  // namespace galaxy::update
