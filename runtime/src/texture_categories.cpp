// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/texture_categories.h"

#include "galaxy/atomic_file.h"
#include "galaxy/disc_files.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace galaxy::textures {

namespace {

struct ObjectKind {
    const char* name;
    Category category;
};

// The objects by the game's own sorting of them: the folder of the class each one is (generated from the object table).
constexpr ObjectKind kObjectKinds[] = {
#include "texture_object_kinds.inc"
};

constexpr const char* kLanguageFolders[] = {"UsEnglish", "UsSpanish", "UsFrench",  "EuEnglish",     "EuSpanish",
                                            "EuFrench",  "EuGerman",  "EuItalian", "EuDutch",       "UsPortuguese",
                                            "JpJapanese", "KrKorean", "CnSimpChinese", "TwTradChinese"};

bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(std::string_view s, std::string_view part) noexcept {
    return s.find(part) != std::string_view::npos;
}

std::string_view without_extension(std::string_view name) noexcept {
    const std::size_t dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

Category category_of_layout(std::string_view layout) noexcept {
    if (layout == "Font") return Category::Fonts;
    if (starts_with(layout, "Icon") || ends_with(layout, "Button") || starts_with(layout, "DPDPointer") ||
        contains(layout, "Guidance") || contains(layout, "Icon") || layout == "P2Manual") {
        return Category::Icons;
    }
    if (starts_with(layout, "PictureBook") || starts_with(layout, "Prologue") || starts_with(layout, "StaffRoll") ||
        (contains(layout, "Letter") && !starts_with(layout, "TalkBalloon"))) {
        return Category::Story;
    }
    if (starts_with(layout, "Map") || starts_with(layout, "Galaxy") || layout == "ScenarioSelect" ||
        layout == "StageTitle" || layout == "AllStarList") {
        return Category::GalaxyMap;
    }
    if (ends_with(layout, "Counter") || ends_with(layout, "Meter") || starts_with(layout, "TalkBalloon") ||
        starts_with(layout, "Cinema") || starts_with(layout, "Wipe") || starts_with(layout, "Time") ||
        layout == "PlayerLeft" || layout == "OneUp" || layout == "BombTimer" || layout == "Race" ||
        layout == "Sight" || layout == "Telescope" || layout == "GetStar" || layout == "GameOver" ||
        layout == "TooBad" || layout == "ResultWindow" || layout == "SignboardBalloon" ||
        layout == "InformationWindow" || layout == "CometScreenFilter" || layout == "Welcome" ||
        layout == "CommandStream") {
        return Category::Hud;
    }
    return Category::Menus;
}

// What the words of a name say, lower case. Whole words where short ones would match inside others.
bool has_word(const std::string& text, std::string_view word) noexcept {
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1u)) {
        const bool start = at == 0u || std::isalnum(static_cast<unsigned char>(text[at - 1u])) == 0;
        const std::size_t end = at + word.size();
        const bool finish = end == text.size() || std::isalnum(static_cast<unsigned char>(text[end])) == 0 ||
                            (text[end] == 's' && (end + 1u == text.size() ||
                                                  std::isalnum(static_cast<unsigned char>(text[end + 1u])) == 0));
        if (start && finish) return true;
    }
    return false;
}

// The record's words (nebula_cooked_texture.h).
constexpr char kRecordMagic[8] = {'N', 'b', 'T', 'e', 'x', 't', 'u', 'r'};
constexpr std::size_t kRecordHeader = 64u;
constexpr std::size_t kRecordSizeAt = 12u;
constexpr std::size_t kRecordFormatAt = 22u;
constexpr std::size_t kRecordHashAt = 24u;
constexpr std::uint8_t kRecordMade = 0xFFu;

constexpr char kCacheMagic[8] = {'N', 'b', 'T', 'e', 'x', 'I', 'd', 'x'};
constexpr std::uint32_t kCacheVersion = 1u;

struct Index {
    std::mutex mutex;
    std::condition_variable done;
    bool started = false;
    std::atomic<bool> ready{false};
    bool ok = false;
    std::vector<std::pair<std::uint64_t, std::uint8_t>> entries;  // sorted by hash
};

Index& index() {
    static Index* const value = new Index();  // never destroyed: its thread may run while the process ends
    return *value;
}

void finish(std::vector<std::pair<std::uint64_t, std::uint8_t>> entries, bool ok) {
    Index& i = index();
    {
        const std::lock_guard<std::mutex> lock(i.mutex);
        i.entries = std::move(entries);
        i.ok = ok;
        i.ready.store(true, std::memory_order_release);
    }
    i.done.notify_all();
}

bool read_cache(const std::filesystem::path& file, std::uint64_t store_size,
                std::vector<std::pair<std::uint64_t, std::uint8_t>>& out) {
    std::ifstream in(file, std::ios::binary);
    char magic[8];
    std::uint32_t version = 0;
    std::uint64_t size = 0;
    std::uint64_t count = 0;
    if (!in.read(magic, 8) || std::memcmp(magic, kCacheMagic, 8) != 0 ||
        !in.read(reinterpret_cast<char*>(&version), sizeof(version)) || version != kCacheVersion ||
        !in.read(reinterpret_cast<char*>(&size), sizeof(size)) || size != store_size ||
        !in.read(reinterpret_cast<char*>(&count), sizeof(count)) || count > (1u << 24)) {
        return false;
    }
    out.resize(static_cast<std::size_t>(count));
    for (auto& [hash, category] : out) {
        if (!in.read(reinterpret_cast<char*>(&hash), sizeof(hash)) ||
            !in.read(reinterpret_cast<char*>(&category), sizeof(category)) ||
            category >= kCategoryCount) {
            out.clear();
            return false;
        }
    }
    return true;
}

void write_cache(const std::filesystem::path& file, std::uint64_t store_size,
                 const std::vector<std::pair<std::uint64_t, std::uint8_t>>& entries) {
    std::string bytes(kCacheMagic, 8);
    const auto put = [&bytes](const auto& value) {
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    put(kCacheVersion);
    put(store_size);
    put(static_cast<std::uint64_t>(entries.size()));
    for (const auto& [hash, category] : entries) {
        put(hash);
        put(category);
    }
    if (!galaxy::io::write_file_atomically(file, bytes)) {
        std::cerr << "[textures] the index could not be kept\n";
    }
}

// Every record in a file's bytes.
void note_records(const std::vector<std::uint8_t>& bytes, Category category,
                  std::vector<std::pair<std::uint64_t, std::uint8_t>>& out) {
    if (bytes.size() < kRecordHeader) return;
    const std::uint8_t* const data = bytes.data();
    const std::size_t last = bytes.size() - kRecordHeader;
    for (std::size_t at = 0; at <= last;) {
        const auto* found = static_cast<const std::uint8_t*>(std::memchr(data + at, kRecordMagic[0], last + 1u - at));
        if (found == nullptr) return;
        at = static_cast<std::size_t>(found - data);
        if (std::memcmp(found, kRecordMagic, sizeof(kRecordMagic)) != 0) {
            ++at;
            continue;
        }
        std::uint32_t size = 0;
        std::memcpy(&size, found + kRecordSizeAt, sizeof(size));
        if (found[kRecordFormatAt] != kRecordMade) {
            std::uint64_t hash = 0;
            std::memcpy(&hash, found + kRecordHashAt, sizeof(hash));
            out.emplace_back(hash, static_cast<std::uint8_t>(category));
        }
        at += size >= kRecordHeader && size <= bytes.size() - at ? size : kRecordHeader;
    }
}

void scan_folder(const galaxy::disc::AssetStore& store, std::int32_t folder, const std::string& path,
                 std::vector<std::uint8_t>& buffer, std::vector<std::pair<std::uint64_t, std::uint8_t>>& out) {
    const std::optional<galaxy::disc::Entry> self = store.entry(folder);
    if (!self) return;
    for (std::uint32_t c = 0; c < self->children; ++c) {
        const std::optional<galaxy::disc::Entry> child = store.child(folder, c);
        if (!child) continue;
        const std::string name = path.empty() ? std::string(child->name) : path + "/" + child->name;
        if (path.empty() && (name == "AudioRes" || name == "MovieData")) continue;
        if (child->folder) {
            scan_folder(store, child->index, name, buffer, out);
            continue;
        }
        constexpr std::uint64_t kLargest = 256u * 1024u * 1024u;
        if (child->length < kRecordHeader || child->length > kLargest) continue;
        buffer.resize(static_cast<std::size_t>(child->length));
        if (store.read(child->position, buffer.size(), buffer.data())) {
            note_records(buffer, category_of_path(name), out);
        }
    }
}

void build(const std::filesystem::path& store_file, const std::filesystem::path& cache_file) {
    std::error_code error;
    const std::uint64_t store_size = std::filesystem::file_size(store_file, error);
    std::vector<std::pair<std::uint64_t, std::uint8_t>> entries;
    if (!error && read_cache(cache_file, store_size, entries)) {
        finish(std::move(entries), true);
        return;
    }
    // A store of its own: the game's may be closed while this runs.
    auto* store = new galaxy::disc::AssetStore();
    if (error || !store->open(store_file)) {
        std::cerr << "[textures] the game's files could not be read to sort textures\n";
        finish({}, false);
        return;
    }
    std::vector<std::uint8_t> buffer;
    scan_folder(*store, 0, {}, buffer, entries);
    // One texture in several files keeps the first: stable by hash.
    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    entries.erase(std::unique(entries.begin(), entries.end(),
                              [](const auto& a, const auto& b) { return a.first == b.first; }),
                  entries.end());
    std::cerr << "[textures] " << entries.size() << " game textures sorted\n";
    write_cache(cache_file, store_size, entries);
    finish(std::move(entries), true);
}

}  // namespace

const char* category_name(Category category) noexcept {
    switch (category) {
    case Category::MarioLuigi: return "MARIO AND LUIGI";
    case Category::Characters: return "CHARACTERS";
    case Category::Enemies: return "ENEMIES";
    case Category::Bosses: return "BOSSES";
    case Category::Items: return "ITEMS";
    case Category::Planets: return "PLANETS AND LANDS";
    case Category::Objects: return "OBJECTS";
    case Category::Skies: return "SKIES";
    case Category::Particles: return "PARTICLES AND EFFECTS";
    case Category::Hud: return "HUD";
    case Category::Menus: return "MENUS";
    case Category::GalaxyMap: return "GALAXY MAP";
    case Category::Icons: return "BUTTONS AND ICONS";
    case Category::Fonts: return "FONTS";
    case Category::Story: return "STORY ART";
    default: return "OTHER";
    }
}

Category category_of_object(std::string_view archive) noexcept {
    std::string_view name = archive;
    for (const std::string_view suffix : {"Low", "Middle", "Bloom", "Volume"}) {
        if (name.size() > suffix.size() && ends_with(name, suffix)) name.remove_suffix(suffix.size());
    }
    if (contains(name, "Planet")) return Category::Planets;
    if (ends_with(name, "Sky") || ends_with(name, "Air") || starts_with(name, "VR")) return Category::Skies;
    for (const std::string_view item :
         {"Coin", "PurpleCoin", "RedCoin", "RailCoin", "StarPiece", "PowerStar", "GrandStar", "BlueChip", "YellowChip",
          "KinokoOneUp", "KinokoLifeUp", "LifeUp", "MorphItem", "BenefitItem", "PowerUp", "GreenStar"}) {
        if (starts_with(name, item)) return Category::Items;
    }
    if (name != "LuigiNPC" && !starts_with(name, "LuigiTalk") && (contains(name, "Mario") || contains(name, "Luigi"))) {
        return Category::MarioLuigi;
    }
    if (starts_with(name, "Boss") || starts_with(name, "TriPodBoss") || starts_with(name, "TripodBoss") ||
        (starts_with(name, "Koopa") && !starts_with(name, "KoopaJr") && !starts_with(name, "KoopaBattleMap") &&
         !starts_with(name, "KoopaPlate"))) {
        return Category::Bosses;
    }
    // The longest name in the table that starts this one (BasaBasaHang is a BasaBasa).
    const ObjectKind* best = nullptr;
    std::size_t best_length = 0;
    for (const ObjectKind& kind : kObjectKinds) {
        const std::size_t length = std::char_traits<char>::length(kind.name);
        if (length > best_length && starts_with(name, std::string_view(kind.name, length))) {
            best = &kind;
            best_length = length;
        }
    }
    return best != nullptr ? best->category : Category::Objects;
}

Category category_of_path(std::string_view path) noexcept {
    // A language's own folder holds what the game's do, in its words.
    for (const char* language : kLanguageFolders) {
        const std::string_view folder(language);
        if (starts_with(path, folder) && path.size() > folder.size() && path[folder.size()] == '/') {
            path.remove_prefix(folder.size() + 1u);
            break;
        }
    }
    const std::size_t slash = path.find('/');
    if (slash == std::string_view::npos) return Category::Other;
    const std::string_view top = path.substr(0, slash);
    std::string_view rest = path.substr(slash + 1u);
    const std::size_t next = rest.find('/');
    const std::string_view file = without_extension(next == std::string_view::npos ? rest : rest.substr(0, next));
    if (top == "ParticleData") return Category::Particles;
    if (top == "ObjectData") return category_of_object(file);
    if (top == "LayoutData") return category_of_layout(file);
    if (top == "StageData") return Category::Planets;
    if (top == "HomeButton2" || top == "NebulaData") return Category::Menus;
    return Category::Other;
}

std::optional<Category> category_of_pack_folder(std::string_view folder) noexcept {
    std::string text;
    text.reserve(folder.size());
    for (const char c : folder) text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    static constexpr std::pair<const char*, Category> kWords[] = {
        {"mario", Category::MarioLuigi},   {"luigi", Category::MarioLuigi},   {"player", Category::MarioLuigi},
        {"particle", Category::Particles}, {"effect", Category::Particles},   {"vfx", Category::Particles},
        {"font", Category::Fonts},         {"text", Category::Fonts},         {"icon", Category::Icons},
        {"button", Category::Icons},       {"prompt", Category::Icons},       {"hud", Category::Hud},          {"fade", Category::Hud},
        {"menu", Category::Menus},         {"ui", Category::Menus},           {"gui", Category::Menus},
        {"layout", Category::Menus},       {"map", Category::GalaxyMap},      {"sky", Category::Skies},
        {"skybox", Category::Skies},       {"background", Category::Skies},   {"boss", Category::Bosses},
        {"bosse", Category::Bosses},       {"enemy", Category::Enemies},      {"enemies", Category::Enemies},
        {"npc", Category::Characters},     {"character", Category::Characters}, {"rosalina", Category::Characters},
        {"luma", Category::Characters},    {"toad", Category::Characters},    {"item", Category::Items},
        {"coin", Category::Items},         {"star", Category::Items},         {"planet", Category::Planets},
        {"environment", Category::Planets}, {"terrain", Category::Planets},    {"ground", Category::Planets},     {"stage", Category::Planets},
        {"level", Category::Planets},      {"galaxy", Category::Planets},     {"object", Category::Objects},
        {"prop", Category::Objects},       {"story", Category::Story},        {"cutscene", Category::Story},
    };
    for (const auto& [word, category] : kWords) {
        if (has_word(text, word)) return category;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> hash_of_pack_name(std::string_view stem) noexcept {
    if (!starts_with(stem, "tex1_")) return std::nullopt;
    // tex1_<w>x<h>[_m]_<hash>...: the first part of sixteen hexadecimal digits.
    std::size_t at = 5u;
    while (at < stem.size()) {
        std::size_t end = stem.find('_', at);
        if (end == std::string_view::npos) end = stem.size();
        const std::string_view part = stem.substr(at, end - at);
        if (part.size() == 16u && std::all_of(part.begin(), part.end(), [](char c) {
                return std::isxdigit(static_cast<unsigned char>(c)) != 0;
            })) {
            std::uint64_t value = 0;
            for (const char c : part) {
                const int digit = std::isdigit(static_cast<unsigned char>(c)) != 0
                                      ? c - '0'
                                      : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
                value = (value << 4u) | static_cast<std::uint64_t>(digit);
            }
            return value;
        }
        at = end + 1u;
    }
    return std::nullopt;
}

void start_index(const std::filesystem::path& store_file, const std::filesystem::path& cache_file) {
    Index& i = index();
    {
        const std::lock_guard<std::mutex> lock(i.mutex);
        if (i.started) return;
        i.started = true;
    }
    std::thread([store_file, cache_file] { build(store_file, cache_file); }).detach();
}

bool index_ready() noexcept {
    return index().ready.load(std::memory_order_acquire);
}

bool wait_for_index() {
    Index& i = index();
    std::unique_lock<std::mutex> lock(i.mutex);
    if (!i.started) return false;
    i.done.wait(lock, [&i] { return i.ready.load(std::memory_order_acquire); });
    return i.ok;
}

std::optional<Category> category_of_hash(std::uint64_t hash) noexcept {
    Index& i = index();
    if (!i.ready.load(std::memory_order_acquire)) return std::nullopt;
    const auto it = std::lower_bound(i.entries.begin(), i.entries.end(), hash,
                                     [](const auto& entry, std::uint64_t value) { return entry.first < value; });
    if (it == i.entries.end() || it->first != hash) return std::nullopt;
    return static_cast<Category>(it->second);
}

Category category_of_pack_texture(std::uint64_t hash, std::string_view folder) noexcept {
    if (const std::optional<Category> known = category_of_hash(hash)) return *known;
    if (const std::optional<Category> named = category_of_pack_folder(folder)) return *named;
    return Category::Other;
}

void set_index_for_tests(std::initializer_list<std::pair<std::uint64_t, Category>> entries) {
    std::vector<std::pair<std::uint64_t, std::uint8_t>> sorted;
    for (const auto& [hash, category] : entries) sorted.emplace_back(hash, static_cast<std::uint8_t>(category));
    std::sort(sorted.begin(), sorted.end());
    {
        const std::lock_guard<std::mutex> lock(index().mutex);
        index().started = true;
    }
    finish(std::move(sorted), true);
}

}  // namespace galaxy::textures
