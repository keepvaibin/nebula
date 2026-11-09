// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/texture_categories.h"

#include "galaxy/asset_store.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

// Where the store lists its textures' names (nebula-import texture::NAMES_PATH).
constexpr std::string_view kNamesPath = "NebulaData/TextureNames.tsv";

struct Index {
    std::mutex mutex;
    std::condition_variable done;
    bool started = false;
    std::atomic<bool> ready{false};
    bool ok = false;
    std::vector<TextureName> rows;  // sorted by hash
};

Index& index() {
    static Index* const value = new Index();  // never destroyed: its thread may run while the process ends
    return *value;
}

void finish(std::vector<TextureName> rows, bool ok) {
    Index& i = index();
    {
        const std::lock_guard<std::mutex> lock(i.mutex);
        i.rows = std::move(rows);
        i.ok = ok;
        i.ready.store(true, std::memory_order_release);
    }
    i.done.notify_all();
}

bool parse_hex(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty() || text.size() > 16u) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0) return false;
        value = (value << 4u) | static_cast<std::uint64_t>(digit);
    }
    out = value;
    return true;
}

bool parse_number(std::string_view text, std::uint32_t& out) noexcept {
    if (text.empty() || text.size() > 9u) return false;
    std::uint32_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10u + static_cast<std::uint32_t>(c - '0');
    }
    out = value;
    return true;
}

void build(const std::filesystem::path& store_file) {
    // A store of its own: the game's may be closed while this runs.
    galaxy::assets::AssetStore store;
    std::optional<galaxy::assets::Entry> entry;
    if (store.open(store_file)) entry = store.find(kNamesPath);
    std::string text;
    if (entry && !entry->folder) {
        text.resize(static_cast<std::size_t>(entry->length));
        if (!store.read(entry->position, text.size(), text.data())) text.clear();
    }
    if (text.empty()) {
        std::cerr << "[textures] the game's texture names could not be read: import the game again\n";
        finish({}, false);
        return;
    }
    std::vector<TextureName> rows = parse_names(text);
    std::cerr << "[textures] " << rows.size() << " game textures by name\n";
    finish(std::move(rows), true);
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

const char* category_folder(Category category) noexcept {
    switch (category) {
    case Category::MarioLuigi: return "Mario and Luigi";
    case Category::Characters: return "Characters";
    case Category::Enemies: return "Enemies";
    case Category::Bosses: return "Bosses";
    case Category::Items: return "Items";
    case Category::Planets: return "Planets";
    case Category::Objects: return "Objects";
    case Category::Skies: return "Skies";
    case Category::Particles: return "Particles";
    case Category::Hud: return "HUD";
    case Category::Menus: return "Menus";
    case Category::GalaxyMap: return "Galaxy Map";
    case Category::Icons: return "Icons";
    case Category::Fonts: return "Fonts";
    case Category::Story: return "Story";
    default: return "Other";
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
        {"layout", Category::Menus},       {"map", Category::GalaxyMap},      {"sky", Category::Skies},       {"skies", Category::Skies},
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

std::uint64_t name_hash(std::string_view name) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        h ^= static_cast<std::uint64_t>(byte >= 'A' && byte <= 'Z' ? byte - 'A' + 'a' : byte);
        h *= 0x100000001B3ull;
    }
    return h;
}

std::vector<TextureName> parse_names(std::string_view text) {
    std::vector<TextureName> rows;
    bool header = true;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1u);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1u);
        if (header) {
            header = false;
            continue;
        }
        std::array<std::string_view, 7> parts{};
        std::size_t count = 0;
        while (count < parts.size()) {
            const std::size_t cut = line.find('\t');
            parts[count++] = line.substr(0, cut);
            if (cut == std::string_view::npos) break;
            line = line.substr(cut + 1u);
        }
        TextureName row;
        std::uint32_t format = 0;
        if (count != parts.size() || parts[0].empty() || !parse_hex(parts[1], row.image) || !parse_hex(parts[2], row.palette) ||
            !parse_number(parts[3], row.width) || !parse_number(parts[4], row.height) || !parse_number(parts[5], format) ||
            format > 255u) {
            continue;
        }
        row.name = std::string(parts[0]);
        row.hash = name_hash(row.name);
        row.format = static_cast<std::uint8_t>(format);
        row.category = category_of_path(parts[6]);
        rows.push_back(std::move(row));
    }
    // One name in several files (a texture of one name in two models of one stem) keeps the first.
    std::stable_sort(rows.begin(), rows.end(), [](const TextureName& a, const TextureName& b) { return a.hash < b.hash; });
    return rows;
}

void start_index(const std::filesystem::path& store_file) {
    Index& i = index();
    {
        const std::lock_guard<std::mutex> lock(i.mutex);
        if (i.started) return;
        i.started = true;
    }
    std::thread([store_file] { build(store_file); }).detach();
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

std::span<const TextureName> names() noexcept {
    Index& i = index();
    if (!i.ready.load(std::memory_order_acquire)) return {};
    return i.rows;
}

const TextureName* find_name(std::uint64_t hash) noexcept {
    const std::span<const TextureName> rows = names();
    const auto it = std::lower_bound(rows.begin(), rows.end(), hash,
                                     [](const TextureName& row, std::uint64_t value) { return row.hash < value; });
    return it != rows.end() && it->hash == hash ? &*it : nullptr;
}

bool is_dolphin_pack_name(std::string_view stem) noexcept {
    return stem.size() > 5u && (stem.substr(0, 5u) == "tex1_" || stem.substr(0, 5u) == "TEX1_");
}

std::optional<PackFileName> parse_pack_file(std::string_view folder, std::string_view stem) noexcept {
    if (stem.empty() || is_dolphin_pack_name(stem)) return std::nullopt;
    PackFileName out;
    // _mip<n> last, _arb before it.
    if (const std::size_t at = stem.rfind("_mip"); at != std::string_view::npos && at + 4u < stem.size()) {
        std::uint32_t level = 0;
        if (parse_number(stem.substr(at + 4u), level) && level != 0u && level < 32u) {
            out.level = static_cast<std::uint8_t>(level);
            stem = stem.substr(0, at);
        }
    }
    if (stem.size() > 4u && stem.substr(stem.size() - 4u) == "_arb") {
        out.arbitrary = true;
        stem.remove_suffix(4u);
    }
    if (stem.empty()) return std::nullopt;
    const std::size_t slash = folder.find_last_of("/\\");
    const std::string_view last = slash == std::string_view::npos ? folder : folder.substr(slash + 1u);
    std::string with_folder;
    if (!last.empty() && last != ".") {
        with_folder.reserve(last.size() + 1u + stem.size());
        with_folder.append(last).append(1u, '/').append(stem);
    }
    out.keys[0] = with_folder.empty() ? name_hash(stem) : name_hash(with_folder);
    out.keys[1] = name_hash(stem);
    return out;
}

std::uint64_t pack_file_key(const PackFileName& name) noexcept {
    for (const std::uint64_t key : name.keys) {
        if (find_name(key) != nullptr) return key;
    }
    return name.keys[0];
}

Category category_of_pack_texture(std::uint64_t hash, std::string_view folder) noexcept {
    if (const TextureName* const known = find_name(hash)) return known->category;
    if (const std::optional<Category> named = category_of_pack_folder(folder)) return *named;
    return Category::Other;
}

void set_index_for_tests(std::vector<TextureName> rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const TextureName& a, const TextureName& b) { return a.hash < b.hash; });
    {
        const std::lock_guard<std::mutex> lock(index().mutex);
        index().started = true;
    }
    finish(std::move(rows), true);
}

}  // namespace galaxy::textures
