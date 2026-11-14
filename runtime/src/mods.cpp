// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h). Riivolution's patch format follows Dolphin's implementation (DiscIO RiivolutionParser.cpp and
// RiivolutionPatcher.cpp, GPL-2.0-or-later): external paths, filename-only disc paths, option parameters, config
// defaults and patch order are Riivolution's as Dolphin reproduces them.

#include "galaxy/mods.h"
#include "galaxy/mod_consent.h"

#include <atomic>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include "galaxy/path_text.h"

#include "galaxy/atomic_file.h"
#include "galaxy/asset_store.h"
#include "galaxy/mod_xml.h"
#include "galaxy/dolphin_pack.h"
#include "galaxy/texture_categories.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace galaxy::mods {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool same_name(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::string utf8_name(const fs::path& path) {
    const std::u8string u = path.filename().u8string();
    return std::string(u.begin(), u.end());
}

fs::path from_utf8(std::string_view s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

// A patch or settings file past this is not read: real ones are kilobytes.
constexpr std::uintmax_t kTextFileLimit = 64u << 20u;

std::string read_text(const fs::path& path) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error || size > kTextFileLimit) return {};
    std::ifstream in(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    in.read(text.data(), static_cast<std::streamsize>(size));
    text.resize(static_cast<std::size_t>(in.gcount()));
    return text;
}

// The entries of `dir`, empty when it cannot be read. A folder that changes or goes while it is read (the player
// moves a mod in Explorer) ends the list; the range-for form of directory_iterator throws there. `unreadable`, when
// given, is set if the list ended on an error, for the caller to report.
std::vector<fs::directory_entry> entries_of(const fs::path& dir, bool* unreadable = nullptr) {
    std::vector<fs::directory_entry> entries;
    std::error_code error;
    for (auto it = fs::directory_iterator(dir, error); !error && it != fs::directory_iterator(); it.increment(error)) {
        entries.push_back(*it);
    }
    if (error && unreadable != nullptr) *unreadable = true;
    return entries;
}

constexpr const char* kUnreadableMod = "A FOLDER OF THIS MOD COULD NOT BE READ IN FULL";

// Whether what a mod brings is used: it is on, and it is no .luma with a problem (such a mod is used for nothing; another
// kind's problem says what of it can't work, and the rest does).
bool in_use(const ModInfo& mod) noexcept {
    return mod.enabled && (!mod.package || mod.problem.empty());
}

// A child of `dir` named `name` whatever its case (Riivolution assumes an SD card's case-insensitive names).
std::optional<fs::path> child_any_case(const fs::path& dir, std::string_view name, bool want_directory) {
    std::error_code error;
    const fs::path exact = dir / from_utf8(name);
    if (fs::exists(exact, error) && fs::is_directory(exact, error) == want_directory) return exact;
    for (const auto& entry : entries_of(dir)) {
        if (same_name(utf8_name(entry.path()), name) && entry.is_directory(error) == want_directory) return entry.path();
    }
    return std::nullopt;
}

struct FilePatch {
    std::string disc, external;
    bool resize = true, create = false;
    std::uint64_t offset = 0, file_offset = 0, length = 0;
};
struct FolderPatch {
    std::string disc, external;
    bool resize = true, create = false, recursive = true;
    std::uint64_t length = 0;
};
struct SavePatch {
    std::string external;
    bool clone = true;
};
struct MemoryPatch {
    std::uint32_t offset = 0;
    std::vector<std::uint8_t> value;
    bool ocarina = false, search = false;
};
struct Patch {
    std::string id, root;
    std::vector<FilePatch> files;
    std::vector<FolderPatch> folders;
    std::vector<SavePatch> saves;
    std::vector<MemoryPatch> memory;  // patches written for the console's code
};
struct PatchRef {
    std::string id;
    std::map<std::string, std::string> params;
};
struct Choice {
    std::string name;
    std::vector<PatchRef> patches;
};
struct Option {
    std::string id, name;
    std::vector<Choice> choices;
    unsigned selected = 0;
};
struct Section {
    std::string name;
    std::vector<Option> options;
};
struct RiivDisc {
    fs::path xml;
    std::optional<std::string> game, developer;
    std::optional<long long> disc, version;
    std::optional<std::vector<std::string>> regions;
    std::string root;
    std::vector<Section> sections;
    std::vector<Patch> patches;
};

std::map<std::string, std::string> read_params(const XmlNode& node, std::map<std::string, std::string> params = {}) {
    for (const XmlNode& p : node.children) {
        if (p.name == "param") params[p.attribute("name")] = p.attribute("value");
    }
    return params;
}

std::vector<std::uint8_t> hex_bytes(std::string_view s) {
    if (s.starts_with("0x") || s.starts_with("0X")) s.remove_prefix(2);
    if (s.size() % 2u != 0u) return {};
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = digit(s[i]);
        const int lo = digit(s[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<std::uint8_t>(hi << 4 | lo));
    }
    return out;
}

std::optional<RiivDisc> parse_riivolution(const fs::path& xml, std::string* error) {
    std::optional<XmlNode> doc = parse_xml(read_text(xml), error);
    if (!doc) return std::nullopt;
    if (doc->name != "wiidisc") {
        if (error != nullptr) *error = "not a Riivolution patch (no <wiidisc>)";
        return std::nullopt;
    }
    if (doc->attribute_int("version", -1) != 1) {
        if (error != nullptr) *error = "a Riivolution version other than 1";
        return std::nullopt;
    }
    RiivDisc disc;
    disc.xml = xml;
    disc.root = doc->attribute("root");
    if (const XmlNode* id = doc->child("id")) {
        if (id->has_attribute("game")) disc.game = id->attribute("game");
        if (id->has_attribute("developer")) disc.developer = id->attribute("developer");
        if (id->has_attribute("disc")) disc.disc = id->attribute_int("disc", -1);
        if (id->has_attribute("version")) disc.version = id->attribute_int("version", -1);
        std::vector<std::string> regions;
        for (const XmlNode& r : id->children) {
            if (r.name == "region") regions.push_back(r.attribute("type"));
        }
        if (!regions.empty()) disc.regions = std::move(regions);
    }
    if (const XmlNode* options = doc->child("options")) {
        for (const XmlNode& s : options->children) {
            if (s.name != "section") continue;
            Section& section = disc.sections.emplace_back();
            section.name = s.attribute("name");
            for (const XmlNode& o : s.children) {
                if (o.name != "option") continue;
                Option& option = section.options.emplace_back();
                option.id = o.attribute("id");
                option.name = o.attribute("name");
                option.selected = static_cast<unsigned>(o.attribute_uint("default", 0));
                const auto option_params = read_params(o);
                for (const XmlNode& c : o.children) {
                    if (c.name != "choice") continue;
                    Choice& choice = option.choices.emplace_back();
                    choice.name = c.attribute("name");
                    const auto choice_params = read_params(c, option_params);
                    for (const XmlNode& p : c.children) {
                        if (p.name != "patch") continue;
                        choice.patches.push_back({p.attribute("id"), read_params(p, choice_params)});
                    }
                }
            }
        }
        // Macros: an option cloned under another name with other parameters.
        for (const XmlNode& m : options->children) {
            if (m.name != "macros") continue;
            for (Section& section : disc.sections) {
                auto it = std::find_if(section.options.begin(), section.options.end(),
                                       [&](const Option& o) { return o.id == m.attribute("id"); });
                if (it == section.options.end()) continue;
                Option clone = *it;
                clone.name = m.attribute("name");
                for (Choice& c : clone.choices) {
                    for (PatchRef& p : c.patches) p.params = read_params(m, p.params);
                }
                section.options.push_back(std::move(clone));
            }
        }
    }
    for (const XmlNode& p : doc->children) {
        if (p.name != "patch") continue;
        Patch& patch = disc.patches.emplace_back();
        patch.id = p.attribute("id");
        patch.root = p.attribute("root");
        if (patch.root.empty()) patch.root = disc.root;
        for (const XmlNode& n : p.children) {
            if (n.name == "file") {
                FilePatch& f = patch.files.emplace_back();
                f.disc = n.attribute("disc");
                f.external = n.attribute("external");
                f.resize = n.attribute_bool("resize", true);
                f.create = n.attribute_bool("create", false);
                f.offset = n.attribute_uint("offset", 0);
                f.file_offset = n.attribute_uint("fileoffset", 0);
                f.length = n.attribute_uint("length", 0);
            } else if (n.name == "folder") {
                FolderPatch& f = patch.folders.emplace_back();
                f.disc = n.attribute("disc");
                f.external = n.attribute("external");
                f.resize = n.attribute_bool("resize", true);
                f.create = n.attribute_bool("create", false);
                f.recursive = n.attribute_bool("recursive", true);
                f.length = n.attribute_uint("length", 0);
            } else if (n.name == "savegame") {
                patch.saves.push_back({n.attribute("external"), n.attribute_bool("clone", true)});
            } else if (n.name == "memory") {
                MemoryPatch& m = patch.memory.emplace_back();
                m.offset = static_cast<std::uint32_t>(n.attribute_uint("offset", 0));
                m.value = hex_bytes(n.attribute("value"));
                m.ocarina = n.attribute_bool("ocarina", false);
                m.search = n.attribute_bool("search", false);
            }
        }
    }
    return disc;
}

bool valid_for_game(const RiivDisc& d, const std::string& game_id, int revision, int disc_number) {
    if (game_id.size() != 6u) return false;
    // A mod written with one version in mind works on the others: their files are the same.
    if (d.game && !game_id.starts_with(d.game->substr(0, 3))) return false;
    if (d.disc && *d.disc != disc_number) return false;
    if (d.version && *d.version != revision) return false;
    return true;
}

std::string option_key(const Section& s, const Option& o) { return o.id.empty() ? s.name + o.name : o.id; }

// Riivolution's own defaults (riivolution/config/<RMGE>.xml beside the patches).
void apply_config(RiivDisc& d, const fs::path& riivolution_dir, const std::string& game_id) {
    const std::optional<fs::path> config_dir = child_any_case(riivolution_dir, "config", true);
    if (!config_dir) return;
    const std::optional<fs::path> file = child_any_case(*config_dir, game_id.substr(0, 4) + ".xml", false);
    if (!file) return;
    std::optional<XmlNode> doc = parse_xml(read_text(*file), nullptr);
    if (!doc || doc->name != "riivolution" || doc->attribute_int("version", -1) != 2) return;
    for (const XmlNode& o : doc->children) {
        if (o.name != "option") continue;
        for (Section& s : d.sections) {
            for (Option& option : s.options) {
                if (option_key(s, option) == o.attribute("id")) {
                    option.selected = static_cast<unsigned>(o.attribute_uint("default", 0));
                }
            }
        }
    }
}

// The SD card's and the patch's roots of a Riivolution mod, and its external paths.
struct Loader {
    fs::path sd_root;
    fs::path patch_root;

    std::optional<fs::path> resolve(std::string_view relative) const {
        if (relative.find('\\') != std::string_view::npos) return std::nullopt;
        fs::path result = relative.starts_with('/') ? sd_root : patch_root;
        std::size_t depth = 0;
        while (!relative.empty()) {
            while (relative.starts_with('/')) relative.remove_prefix(1);
            if (relative.empty()) break;
            const std::size_t slash = relative.find('/');
            const std::string_view element = relative.substr(0, slash);
            relative = slash == std::string_view::npos ? std::string_view{} : relative.substr(slash + 1);
            if (element == ".") continue;
            if (element == "..") {
                if (depth == 0) return std::nullopt;
                --depth;
                result = result.parent_path();
                continue;
            }
            if (!safe_path_part(element)) return std::nullopt;
            ++depth;
            std::error_code error;
            const fs::path exact = result / from_utf8(element);
            if (fs::exists(exact, error)) {
                result = exact;
                continue;
            }
            bool found = false;
            for (const auto& entry : entries_of(result)) {
                if (same_name(utf8_name(entry.path()), element)) {
                    result = entry.path();
                    found = true;
                    break;
                }
            }
            if (!found) result = exact;
        }
        return result;
    }
};

Loader make_loader(const fs::path& sd_root, const fs::path& xml, const std::string& patch_root) {
    Loader loader{sd_root, xml.parent_path()};
    if (!patch_root.empty()) {
        if (std::optional<fs::path> r = loader.resolve(patch_root)) loader.patch_root = *r;
    }
    return loader;
}

std::string replace_variables(std::string_view s, const std::vector<std::pair<std::string, std::string>>& with) {
    std::string out;
    while (!s.empty()) {
        bool replaced = false;
        for (const auto& [from, to] : with) {
            if (s.starts_with(from)) {
                out += to;
                s.remove_prefix(from.size());
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            out.push_back(s.front());
            s.remove_prefix(1);
        }
    }
    return out;
}

// The selected choices' patches, their placeholders filled.
std::vector<Patch> generate_patches(const RiivDisc& d, const std::string& game_id) {
    std::vector<Patch> active;
    for (const Section& s : d.sections) {
        for (const Option& o : s.options) {
            if (o.selected == 0u || o.selected > o.choices.size()) continue;
            for (const PatchRef& ref : o.choices[o.selected - 1u].patches) {
                const auto it = std::find_if(d.patches.begin(), d.patches.end(), [&](const Patch& p) { return p.id == ref.id; });
                if (it == d.patches.end()) continue;
                std::vector<std::pair<std::string, std::string>> with{{"{$__gameid}", game_id.substr(0, 3)},
                                                                      {"{$__region}", game_id.substr(3, 1)},
                                                                      {"{$__maker}", game_id.substr(4, 2)}};
                for (const auto& [k, v] : ref.params) with.emplace_back("{$" + k + "}", v);
                Patch p = *it;
                p.root = replace_variables(p.root, with);
                for (FilePatch& f : p.files) {
                    f.disc = replace_variables(f.disc, with);
                    f.external = replace_variables(f.external, with);
                }
                for (FolderPatch& f : p.folders) {
                    f.disc = replace_variables(f.disc, with);
                    f.external = replace_variables(f.external, with);
                }
                for (SavePatch& sp : p.saves) sp.external = replace_variables(sp.external, with);
                active.push_back(std::move(p));
            }
        }
    }
    return active;
}

// The disc's top-level folders of every release: a mod folder holding one of them is files in the disc's layout.
constexpr const char* kDiscFolders[] = {"AudioRes", "LayoutData", "MapPartsData", "MovieData", "ObjectData",
                                         "ParticleData", "StageData", "SystemData", "UsEnglish", "UsFrench",
                                         "UsSpanish", "EuEnglish", "EuFrench", "EuGerman", "EuItalian", "EuSpanish",
                                         "JpJapanese", "KrKorean", "CnSimpChinese", "TwTradChinese", "HomeButton2"};

struct ModLayout {
    fs::path package;  // luma.json of a converted mod
    fs::path sd_root;           // Riivolution: where riivolution/ is
    fs::path riivolution_dir;
    std::vector<fs::path> xmls;
    fs::path files_root;        // the disc's layout
    fs::path textures;
    fs::path movies;
    std::vector<fs::path> loose;  // files placed by their names
    bool unreadable = false;      // a folder of the mod ended its listing on an error
};

constexpr const char* kLanguageFolders[] = {"UsEnglish", "UsFrench", "UsSpanish", "EuEnglish", "EuFrench", "EuGerman",
                                            "EuItalian", "EuSpanish", "EuDutch", "JpJapanese", "KrKorean",
                                            "CnSimpChinese", "TwTradChinese"};

// The language folder a disc path is in ("/UsEnglish/MessageData/Message.arc" -> UsEnglish), else empty.
std::string language_of(std::string_view disc_path) {
    while (disc_path.starts_with('/')) disc_path.remove_prefix(1);
    const std::string_view first = disc_path.substr(0, disc_path.find('/'));
    for (const char* folder : kLanguageFolders) {
        if (same_name(first, folder)) return folder;
    }
    return {};
}

// Notes beside a mod's files: never placed on the disc.
bool mod_note(const fs::path& file) {
    const std::string extension = lower(galaxy::path_text(file.extension()));
    for (const char* known : {".txt", ".md", ".pdf", ".url", ".htm", ".html", ".rtf", ".png", ".jpg", ".jpeg",
                              ".webp", ".gif", ".nfo", ".xml", ".ini", ".db", ".zip", ".7z", ".rar"}) {
        if (extension == known) return true;
    }
    return false;
}

// A folder the game has at its root, or one language's.
bool is_disc_folder(std::string_view name) {
    const std::string wanted = lower(name);
    for (const char* folder : kDiscFolders) {
        if (lower(folder) == wanted) return true;
    }
    for (const char* folder : kLanguageFolders) {
        if (lower(folder) == wanted) return true;
    }
    return false;
}

bool has_disc_folder(const fs::path& dir) {
    for (const char* folder : kDiscFolders) {
        if (child_any_case(dir, folder, true)) return true;
    }
    return false;
}

// Every file below `dir`, as paths relative to it ("/StageData/A.arc", lower case).
void list_files(const fs::path& dir, const std::string& prefix, std::set<std::string>& out) {
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        std::string relative = lower(utf8_name(fs::relative(it->path(), dir, error)));
        std::replace(relative.begin(), relative.end(), '\\', '/');
        out.insert(prefix + "/" + relative);
    }
}

// A pack's texture file (texture_pack.h): its name's key and its folder in the pack; nullopt for anything else (a
// Dolphin pack's tex1_ file, a custom lower level, not an image).
struct PackFile {
    std::uint64_t key = 0;
    std::string folder;
};
std::optional<PackFile> pack_file(const fs::path& file, const fs::path& root) {
    const std::string extension = lower(utf8_name(file.extension()));
    if (extension != ".png" && extension != ".dds") return std::nullopt;
    PackFile out;
    out.folder = galaxy::path_text(file.parent_path().lexically_relative(root));
    const std::optional<galaxy::textures::PackFileName> name =
        galaxy::textures::parse_pack_file(out.folder, utf8_name(file.stem()));
    if (!name || name->level != 0u) return std::nullopt;
    out.key = galaxy::textures::pack_file_key(*name);
    return out;
}

// Each texture file of a pack folder, its converted Dolphin files (.dolphin/) left out.
template <typename Visit>
void each_pack_file(const fs::path& dir, Visit&& visit) {
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        std::error_code kind;
        if (it->is_directory(kind)) {
            if (utf8_name(it->path()) == ".dolphin") it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(kind)) continue;
        if (const std::optional<PackFile> file = pack_file(it->path(), dir)) visit(*file);
    }
}

// The texture names of a pack folder, by key (mip levels left out).
void list_textures(const fs::path& dir, std::set<std::string>& out) {
    each_pack_file(dir, [&out](const PackFile& file) {
        char key[17]{};
        std::snprintf(key, sizeof(key), "%016llx", static_cast<unsigned long long>(file.key));
        out.insert(key);
    });
}

// A Dolphin pack in `textures` converted to the game's names, once (dolphin_pack.h): needs the game's names.
void convert_dolphin_textures(const fs::path& textures, const std::string& id) {
    if (!galaxy::textures::has_dolphin_files(textures)) return;
    if (!galaxy::textures::wait_for_index()) {
        std::fprintf(stderr, "[mods] %s: a Dolphin texture pack; the game's texture names are not known yet, so it stays as it is\n",
                     id.c_str());
        return;
    }
    const galaxy::textures::DolphinConversion done = galaxy::textures::convert_dolphin_pack(textures, galaxy::textures::names());
    std::fprintf(stderr, "[mods] %s: Dolphin pack converted: %llu files, %llu textures named, %llu taken, %zu for none of "
                         "the game's textures, %zu failed\n",
                 id.c_str(), static_cast<unsigned long long>(done.files), static_cast<unsigned long long>(done.textures),
                 static_cast<unsigned long long>(done.taken), done.unmatched.size(), done.failed.size());
    for (const std::string& file : done.failed) std::fprintf(stderr, "[mods] %s: not converted: %s\n", id.c_str(), file.c_str());
}

// A texture pack without a textures/ folder, within a few folders of `dir`: a tex1_ file, a game folder (RMGE01, RMG)
// or a converted pack's .dolphin/ folder.
bool has_pack_textures(const fs::path& dir) {
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (it.depth() > 4) {
            it.disable_recursion_pending();
            continue;
        }
        const std::string name = lower(utf8_name(it->path()));
        std::error_code kind;
        if (it->is_directory(kind)) {
            if (name == ".dolphin" || name == "rmg" || (name.size() == 6u && name.starts_with("rmg"))) return true;
            continue;
        }
        if (name.starts_with("tex1_") && (name.ends_with(".png") || name.ends_with(".dds"))) return true;
    }
    return false;
}

// A string field of a resource pack's manifest.json ("name": "..."); a small reading, enough for its flat fields.
std::string json_string(const std::string& text, std::string_view field) {
    const std::string key = "\"" + std::string(field) + "\"";
    std::size_t at = text.find(key);
    if (at == std::string::npos) return {};
    at = text.find(':', at + key.size());
    if (at == std::string::npos) return {};
    at = text.find_first_not_of(" \t\r\n[", at + 1u);
    if (at == std::string::npos || text[at] != '"') return {};
    std::string value;
    for (++at; at < text.size() && text[at] != '"'; ++at) {
        if (text[at] == '\\' && at + 1u < text.size()) ++at;
        value.push_back(text[at]);
    }
    return value;
}

void read_pack(ModInfo& mod, const fs::path& textures) {
    auto list = std::make_shared<std::vector<PackTexture>>();
    std::error_code error;
    each_pack_file(textures, [&list](const PackFile& file) {
        PackTexture texture;
        texture.hash = file.key;
        if (const auto named = galaxy::textures::category_of_pack_folder(file.folder)) {
            texture.folder_category = static_cast<std::uint8_t>(*named);
        }
        list->push_back(texture);
    });
    mod.pack_textures = std::move(list);
    // A resource pack's manifest, in the mod or one folder down.
    std::vector<fs::path> places{mod.path, textures};
    for (const auto& entry : entries_of(mod.path)) {
        if (entry.is_directory(error)) places.push_back(entry.path());
    }
    for (const fs::path& place : places) {
        const std::optional<fs::path> manifest = child_any_case(place, "manifest.json", false);
        if (!manifest) continue;
        const std::string text = read_text(*manifest);
        mod.pack_title = json_string(text, "name");
        mod.pack_author = json_string(text, "authors");
        mod.pack_description = json_string(text, "description");
        break;
    }
}

// The language played, as language_folder takes it (the host says; English until it does).
std::atomic<unsigned> s_played_language{1u};

// The option a .luma's author may offer: its saves or the player's.
constexpr const char* kSavesOption = "nebula-saves";

// The family of saves a .luma plays on, as things stand; empty: the player's normal saves.
std::string own_saves_tag(const ModInfo& mod) {
    if (!mod.luma || mod.luma->save_mode == luma::SaveMode::Shared) return {};
    for (const ModOption& option : mod.options) {
        if (option.key == kSavesOption && option.selected == 2u) return {};
    }
    return mod.luma->save_tag;
}

// A mod's libraries for this device as they are, each with the files beside it (code_hash): changed files ask the
// player again.
std::string plugin_token(const fs::path& mod, const luma::Manifest& manifest) {
    std::uint64_t hash = 14695981039346656037ull;
    bool any = false;
    for (const luma::Plugin& plugin : manifest.plugins) {
        const auto binary = plugin.binaries.find(std::string(luma::plugin_target()));
        if (binary == plugin.binaries.end()) continue;
        const std::string code = code_hash(mod / fs::path(from_utf8(binary->second)));
        for (const char c : code.empty() ? std::string("unreadable") : code) hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
        any = true;
    }
    if (!any) return {};
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

// What a .luma's layers are used under, as things stand.
// The mods on, for a layer's `when`, as the plan goes through them.
std::vector<std::string> g_mods_on;

bool has_code_layer(const luma::Manifest& manifest) {
    return std::any_of(manifest.layers.begin(), manifest.layers.end(), [](const luma::Layer& layer) { return layer.when.code; });
}

luma::Situation situation_of(const ModInfo& mod, const std::string& game_id, const std::string& language) {
    luma::Situation situation;
    // Only a mod with a code layer reads the record: the others cost nothing.
    if (mod.luma && has_code_layer(*mod.luma)) {
        const CodeLayers record = code_layers_record();
        if (const auto chosen = record.find(mod.id); chosen != record.end()) situation.code_layers = chosen->second;
    }
    situation.mods = g_mods_on;
    situation.region = game_id;
    situation.version = game_id.size() >= 4u ? game_id.substr(3, 1) : std::string("E");
    situation.language = language;
    situation.platform = std::string(luma::plugin_target());
    situation.code = mod.code_allowed && mod.luma && std::any_of(mod.luma->plugins.begin(), mod.luma->plugins.end(), [](const luma::Plugin& plugin) {
                         return plugin.binaries.count(std::string(luma::plugin_target())) != 0u;
                     });
    for (const ModOption& option : mod.options) {
        if (option.kind == "number") {
            situation.options[option.key] = option.text;  // number_text's, as a layer's `when` writes a number
            continue;
        }
        situation.options[option.key] =
            option.selected != 0u && option.selected <= option.choice_ids.size() ? option.choice_ids[option.selected - 1u] : std::string("off");
    }
    return situation;
}

std::mutex g_resets_lock;
std::vector<OptionReset> g_resets;

// A number option's kept value that is not one of its values: one record a mod, option and kept value this session.
void number_reset_once(const std::string& mod_id, const luma::Option& entry, const std::string& kept) {
    const std::lock_guard<std::mutex> lock(g_resets_lock);
    for (const OptionReset& reset : g_resets) {
        if (reset.mod_id == mod_id && reset.key == entry.id && reset.kept == kept) return;
    }
    g_resets.push_back({mod_id, entry.id, kept, entry.fallback});
    std::fprintf(stderr, "[mods] %s: the value kept for its option %s (%s) is not one of its values; it reads %s\n", mod_id.c_str(),
                 entry.id.c_str(), kept.c_str(), entry.fallback.c_str());
}

ModLayout layout_of(const fs::path& mod) {
    ModLayout layout;
    std::error_code error;
    if (std::optional<fs::path> manifest = child_any_case(mod, "luma.json", false)) {
        // A .luma: its layers say where everything is.
        layout.package = *manifest;
        return layout;
    }
    // riivolution/ in the mod, or one folder down (an archive's own top folder).
    std::vector<fs::path> roots{mod};
    for (const auto& entry : entries_of(mod, &layout.unreadable)) {
        if (entry.is_directory(error)) roots.push_back(entry.path());
    }
    for (const fs::path& root : roots) {
        if (std::optional<fs::path> riiv = child_any_case(root, "riivolution", true)) {
            layout.sd_root = root;
            layout.riivolution_dir = *riiv;
            for (const auto& entry : entries_of(*riiv, &layout.unreadable)) {
                if (entry.is_regular_file(error) && lower(galaxy::path_text(entry.path().extension())) == ".xml") {
                    layout.xmls.push_back(entry.path());
                }
            }
            std::sort(layout.xmls.begin(), layout.xmls.end());
            break;
        }
    }
    if (std::optional<fs::path> files = child_any_case(mod, "files", true)) {
        layout.files_root = *files;
    } else if (has_disc_folder(mod)) {
        layout.files_root = mod;
    } else if (layout.xmls.empty()) {
        // The disc's folders one folder down (an archive's own top folder): that folder, when only one has them.
        std::vector<fs::path> with;
        for (const auto& entry : entries_of(mod, &layout.unreadable)) {
            if (entry.is_directory(error) && has_disc_folder(entry.path())) with.push_back(entry.path());
        }
        if (with.size() == 1u) layout.files_root = with.front();
    }
    if (std::optional<fs::path> textures = child_any_case(mod, "textures", true)) layout.textures = *textures;
    if (std::optional<fs::path> movies = child_any_case(mod, "movies", true)) layout.movies = *movies;
    if (layout.xmls.empty() && layout.files_root.empty()) {
        // Loose files beside nothing else: each goes where the disc has a file of its name.
        for (const auto& entry : entries_of(mod, &layout.unreadable)) {
            if (entry.is_regular_file(error) && !mod_note(entry.path()) &&
                !lower(utf8_name(entry.path())).starts_with("tex1_")) {
                layout.loose.push_back(entry.path());
            }
        }
        std::sort(layout.loose.begin(), layout.loose.end());
        // A texture pack without a textures/ folder (its RMGE01 or RMG folder, or tex1_ files themselves).
        if (layout.textures.empty() && layout.loose.empty() && has_pack_textures(mod)) layout.textures = mod;
    }
    return layout;
}

struct SavedMod {
    std::uint32_t categories_off = 0;
    bool enabled = true;
    unsigned order = 0;
    bool has_order = false;
    bool own_text = true;
    std::map<std::string, unsigned> options;
    std::map<std::string, std::string> choice_ids;  // a .luma's choices, by option
    std::map<std::string, std::string> texts;       // a .luma's text options, by option (text.<key>=<value>)
    std::string code_ok;  // the libraries the player allowed
    std::string code_no;  // the libraries the player said Not now to
    bool install_ask_never = false;  // install.ask=no
    std::map<std::string, PermissionAnswer> permissions;  // permission.<name>=yes|no <digest>
};

std::string escape_key(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (c == '%' || c == '=' || c == '\n' || c == '\r' || c == '[' || c == ']') {
            static constexpr char kHex[] = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 15]);
            out.push_back(kHex[static_cast<unsigned char>(c) & 15]);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string unescape_key(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const std::vector<std::uint8_t> b = hex_bytes(s.substr(i + 1, 2));
            if (b.size() == 1u) {
                out.push_back(static_cast<char>(b[0]));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

struct Settings {
    fs::path store;
    std::mutex mutex;
    fs::path folder;
    fs::path file;
    std::string game_id;
};

void start_texture_sorting();

Settings& settings() {
    static Settings s;
    return s;
}

// Where a family's saves are kept: beside the mods and not in one, so updating or removing a mod keeps them.
fs::path saves_of(const std::string& tag) {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.folder / ".saves" / from_utf8(tag);
}

struct Session {
    std::mutex mutex;
    SessionMods mods;
};

void start_texture_sorting() {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.store.empty() && !s.file.empty()) {
        galaxy::textures::start_index(s.store);
    }
}

Session& session() {
    static Session s;
    return s;
}

// What a restart would change: the enabled mods, their order, their choices and whether their code may run.
std::string mods_signature(std::span<const ModInfo> mods) {
    std::vector<const ModInfo*> sorted;
    for (const ModInfo& m : mods) sorted.push_back(&m);
    std::stable_sort(sorted.begin(), sorted.end(), [](const ModInfo* a, const ModInfo* b) { return a->order < b->order; });
    std::string out;
    for (const ModInfo* m : sorted) {
        if (!m->enabled || !changes_game_files(*m)) continue;
        out += m->id + (m->own_text ? "" : " text=game") + (m->code_allowed ? " code" : "") + '\n';
        for (const ModOption& o : m->options) out += o.key + '=' + (o.kind == "number" ? o.text : std::to_string(o.selected)) + '\n';
    }
    return out;
}

std::map<std::string, SavedMod> read_saved(const fs::path& file) {
    std::map<std::string, SavedMod> saved;
    std::istringstream in(read_text(file));
    std::string line;
    SavedMod* current = nullptr;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            current = &saved[unescape_key(std::string_view(line).substr(1, line.size() - 2))];
            continue;
        }
        const std::size_t eq = line.find('=');
        if (current == nullptr || eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "enabled") {
            current->enabled = value == "1";
        } else if (key == "order") {
            current->order = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
            current->has_order = true;
        } else if (key == "text") {
            current->own_text = value != "game";
        } else if (key == "textures_off") {
            current->categories_off = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (key == "code_ok") {
            current->code_ok = value;
        } else if (key == "code_no") {
            current->code_no = value;
        } else if (key == "install.ask") {
            current->install_ask_never = value == "no";
        } else if (key.starts_with("permission.")) {
            // "yes <digest>" or "no <digest>"; anything else is not an answer, so the player is asked again.
            const std::size_t space = value.find(' ');
            const std::string said = value.substr(0, space);
            if (space != std::string::npos && (said == "yes" || said == "no") && space + 1u < value.size()) {
                current->permissions[unescape_key(std::string_view(key).substr(11))] = {said == "yes", value.substr(space + 1u)};
            }
        } else if (key.starts_with("choice.")) {
            current->choice_ids[unescape_key(std::string_view(key).substr(7))] = value;
        } else if (key.starts_with("text.")) {
            current->texts[unescape_key(std::string_view(key).substr(5))] = unescape_key(value);
        } else if (key.starts_with("option.")) {
            current->options[unescape_key(std::string_view(key).substr(7))] =
                static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        }
    }
    return saved;
}

// The region a choice's name stands for ('E', 'P', 'J', 'K'), else 0: NTSC/USA, PAL/EUR, JP/NTSC-J, KOR.
char choice_region(std::string_view name) {
    std::vector<std::string> words;
    std::string word;
    for (const char c : lower(name)) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            word.push_back(c);
        } else if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& w = words[i];
        const std::string next = i + 1u < words.size() ? words[i + 1u] : std::string();
        if (w == "ntscj" || (w == "ntsc" && next == "j") || w == "jp" || w == "jpn" || w == "japan" || w == "jap") {
            return 'J';
        }
        if (w == "ntscu" || w == "ntsc" || w == "usa" || w == "us" || w == "na" || w == "america" || w == "americas") {
            return 'E';
        }
        if (w == "pal" || w == "eur" || w == "eu" || w == "europe" || w == "uk") return 'P';
        if (w == "kor" || w == "korea" || w == "kr" || w == "ntsck") return 'K';
    }
    return 0;
}

// An option whose every choice names a region: the choice for this disc (1-based), else 0.
unsigned region_choice(const Option& o, const std::string& game_id) {
    if (o.choices.size() < 2u || game_id.size() < 4u) return 0u;
    unsigned match = 0u;
    for (std::size_t i = 0; i < o.choices.size(); ++i) {
        const char region = choice_region(o.choices[i].name);
        if (region == 0) return 0u;
        if (region == game_id[3] && match == 0u) match = static_cast<unsigned>(i + 1u);
    }
    return match;
}

// A mod's Riivolution discs with its choices (saved, else its config's, else its XML's defaults).
std::vector<RiivDisc> mod_discs(const ModLayout& layout, const SavedMod* saved, const std::string& game_id,
                                std::string* problem) {
    std::vector<RiivDisc> discs;
    unsigned other_game = 0;
    std::string other_game_id;
    for (const fs::path& xml : layout.xmls) {
        std::string error;
        std::optional<RiivDisc> d = parse_riivolution(xml, &error);
        if (!d) {
            if (problem != nullptr && problem->empty()) *problem = utf8_name(xml) + ": " + error;
            continue;
        }
        if (!game_id.empty() && !valid_for_game(*d, game_id, -1, -1)) {
            ++other_game;
            if (d->game) other_game_id = *d->game;
            continue;
        }
        apply_config(*d, layout.riivolution_dir, game_id.empty() ? std::string("RMGE01") : game_id);
        std::size_t option_count = 0;
        for (const Section& s : d->sections) option_count += s.options.size();
        for (Section& s : d->sections) {
            for (Option& o : s.options) {
                const auto it = saved != nullptr ? saved->options.find(option_key(s, o)) : std::map<std::string, unsigned>::const_iterator{};
                if (saved != nullptr && it != saved->options.end()) {
                    o.selected = std::min<unsigned>(it->second, static_cast<unsigned>(o.choices.size()));
                } else if (const unsigned region = region_choice(o, game_id.empty() ? std::string("RMGE01") : game_id);
                           region != 0u) {
                    // Never chosen: a region option takes this disc's region (another region's code patches would
                    // not fit this disc).
                    o.selected = region;
                } else if (option_count == 1u && o.choices.size() == 1u && o.selected == 0u) {
                    // Never chosen: a mod's only option with one choice ("Enabled") is its own switch, which the
                    // mod's ON already is (several such options are extras, off as their mod made them).
                    o.selected = 1u;
                }
            }
        }
        discs.push_back(std::move(*d));
    }
    if (discs.empty() && other_game != 0u && problem != nullptr && problem->empty()) {
        *problem = other_game_id.starts_with("SB4")   ? "THIS MOD IS FOR SUPER MARIO GALAXY 2"
                   : other_game_id.starts_with("RMG") ? "THIS MOD IS FOR ANOTHER VERSION OF THE GAME"
                                                      : "THIS MOD IS FOR ANOTHER GAME";
    }
    return discs;
}

}  // namespace

std::string change_kinds(const ModInfo& mod) {
    const bool files = mod.files || mod.loose || (mod.changes != nullptr && !mod.changes->paths.empty());
    const bool settings = mod.changes != nullptr && !mod.changes->settings.empty();
    std::string kinds;
    for (const auto& [on, name] : {std::pair{mod.riivolution, "RIIVOLUTION"}, std::pair{files, "FILES"},
                                   std::pair{settings, "SETTINGS"}, std::pair{mod.textures, "TEXTURES"},
                                   std::pair{mod.movies, "MOVIES"}, std::pair{!mod.code_token.empty(), "CODE"}}) {
        if (on) kinds += kinds.empty() ? name : std::string(", ") + name;
    }
    return kinds;
}

std::string code_hash(const fs::path& library) {
    std::error_code error;
    std::vector<fs::path> files;
    for (auto it = fs::directory_iterator(library.parent_path(), error); !error && it != fs::directory_iterator(); it.increment(error)) {
        std::error_code status_error;
        if (it->is_regular_file(status_error)) files.push_back(it->path());
    }
    if (error || files.empty()) return {};
    std::sort(files.begin(), files.end());
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&hash](const char* bytes, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) hash = (hash ^ static_cast<unsigned char>(bytes[i])) * 1099511628211ull;
    };
    for (const fs::path& file : files) {
        const std::u8string name = file.filename().u8string();
        add(reinterpret_cast<const char*>(name.c_str()), name.size() + 1u);
        std::ifstream in(file, std::ios::binary);
        if (!in) return {};
        char buffer[65536];
        while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) add(buffer, static_cast<std::size_t>(in.gcount()));
        const char end = 0;
        add(&end, 1u);
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

bool safe_path_part(std::string_view part) {
    if (part.empty() || part.find_first_of(":/\\") != std::string_view::npos ||
        part.find_first_not_of(". ") == std::string_view::npos) {
        return false;
    }
    // A device whatever follows its name's first dot, on every platform: a mod is the same mod on all of them.
    std::string stem = lower(part.substr(0, part.find('.')));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" || stem == "conout$") return false;
    if (!stem.starts_with("com") && !stem.starts_with("lpt")) return true;
    const std::string_view number = std::string_view(stem).substr(3);
    const bool digit = number.size() == 1u && number[0] >= '1' && number[0] <= '9';
    return !digit && number != "\xC2\xB9" && number != "\xC2\xB2" && number != "\xC2\xB3";
}

void set_mods_folder(const std::filesystem::path& folder, const std::filesystem::path& settings_file,
                     const std::string& game_id) {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.folder = folder;
    s.file = settings_file;
    s.game_id = game_id;
}

std::string mods_game_id() {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.game_id;
}

namespace {
std::atomic<bool> s_code_off_after_crash{false};
std::atomic<SafeStart> s_safe_start{SafeStart::kNo};
std::atomic<bool> s_safe_start_taken{false};
}

void set_code_off_after_crash() noexcept {
    s_code_off_after_crash.store(true, std::memory_order_relaxed);
}

std::filesystem::path safe_start_file() {
    const std::filesystem::path folder = mods_folder();
    return folder.empty() ? folder : folder / ".safe_start";
}

SafeStart take_safe_start(bool asked) {
    // Once a process: the host takes it before it chooses the game module, and its mods start reads the same answer.
    if (s_safe_start_taken.exchange(true, std::memory_order_relaxed)) {
        return s_safe_start.load(std::memory_order_relaxed);
    }
    SafeStart safe = asked ? SafeStart::kAsked : SafeStart::kNo;
    const std::filesystem::path file = safe_start_file();
    std::error_code error;
    if (!file.empty() && std::filesystem::exists(file, error)) {
        if (safe == SafeStart::kNo) {
            safe = std::filesystem::file_size(file, error) == 0u && !error ? SafeStart::kAfterCrash : SafeStart::kAsked;
        }
        std::filesystem::remove(file, error);
    }
    s_safe_start.store(safe, std::memory_order_relaxed);
    return safe;
}

SafeStart safe_start() noexcept {
    return s_safe_start.load(std::memory_order_relaxed);
}

namespace {
// The blocklist as read from its file (once, then as written). Under s_blocked_mutex.
std::mutex s_blocked_mutex;
std::set<std::string> s_blocked;
bool s_blocked_read = false;

fs::path blocked_file() {
    const fs::path settings_file = mods_settings_file();
    return settings_file.empty() ? settings_file : settings_file.parent_path() / "mods-blocked.txt";
}

// With s_blocked_mutex held.
void read_blocked_locked() {
    if (s_blocked_read) return;
    const fs::path file = blocked_file();
    if (file.empty()) return;  // before set_mods_folder: read when there is a file to read
    s_blocked_read = true;
    std::istringstream in(read_text(file));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;
        s_blocked.insert(line);
    }
}
}  // namespace

bool mod_blocked(std::string_view id) {
    const std::lock_guard<std::mutex> lock(s_blocked_mutex);
    read_blocked_locked();
    return s_blocked.find(std::string(id)) != s_blocked.end();
}

bool set_mod_blocked(const std::string& id, bool blocked) {
    const std::lock_guard<std::mutex> lock(s_blocked_mutex);
    read_blocked_locked();
    const fs::path file = blocked_file();
    if (file.empty() || id.empty() || id.find_first_of("\r\n") != std::string::npos) return false;
    std::set<std::string> next = s_blocked;
    if (blocked) {
        next.insert(id);
    } else {
        next.erase(id);
    }
    std::string text = "; Nebula: mods the player blocked (one id a line): they never start, even when on or imported again.\n";
    for (const std::string& entry : next) text += entry + '\n';
    if (!io::write_file_atomically(file, text)) return false;
    s_blocked = std::move(next);
    return true;
}

std::filesystem::path mods_folder() {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.folder;
}

std::filesystem::path mods_settings_file() {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.file;
}

void set_played_language(unsigned language) noexcept {
    s_played_language.store(language, std::memory_order_relaxed);
}

unsigned played_language() noexcept {
    return s_played_language.load(std::memory_order_relaxed);
}

std::vector<ModInfo> scan_mods(const std::string& requested_game_id) {
    // (The packs' categories wait for start_texture_sorting's index and are counted again when it is ready.)
    fs::path folder;
    fs::path file;
    std::string game_id = requested_game_id;
    {
        Settings& s = settings();
        const std::lock_guard<std::mutex> lock(s.mutex);
        folder = s.folder;
        file = s.file;
        if (game_id.empty()) game_id = s.game_id;
    }
    std::vector<ModInfo> mods;
    std::error_code error;
    if (folder.empty() || !fs::is_directory(folder, error)) return mods;
    const std::map<std::string, SavedMod> saved = read_saved(file);
    std::vector<fs::path> dirs;
    bool folder_unreadable = false;
    for (const auto& entry : entries_of(folder, &folder_unreadable)) {
        // Hidden folders: imports in progress (mod_import.cpp) and the system's own.
        if (entry.is_directory(error) && !utf8_name(entry.path()).starts_with('.')) dirs.push_back(entry.path());
    }
    if (folder_unreadable) {
        std::fprintf(stderr, "[mods] the mods folder could not be read in full: %s\n", galaxy::path_text(folder).c_str());
    }
    std::sort(dirs.begin(), dirs.end());
    const std::string region_id = game_id.empty() ? std::string("RMGE01") : game_id;
    // What each mod changes, by its id: disc paths ("name:x.arc" for a file name alone) and texture names.
    std::map<std::string, std::set<std::string>> changed_paths;
    std::map<std::string, std::set<std::string>> changed_textures;
    // And what its rows and merges set (luma::setting_keys, luma::merge_keys).
    std::map<std::string, std::set<std::string>> changed_settings;
    std::set<std::string> new_mods;
    for (const fs::path& dir : dirs) {
        ModInfo mod;
        mod.id = utf8_name(dir);
        mod.path = dir;
        const ModLayout layout = layout_of(dir);
        mod.riivolution = !layout.xmls.empty();
        mod.files = !layout.files_root.empty();
        mod.textures = !layout.textures.empty();
        mod.movies = !layout.movies.empty();
        mod.loose = !layout.loose.empty();
        mod.package = !layout.package.empty();
        const auto it = saved.find(mod.id);
        const SavedMod* s = it != saved.end() ? &it->second : nullptr;
        mod.enabled = s == nullptr || s->enabled;
        mod.own_text = s == nullptr || s->own_text;
        mod.categories_off = s != nullptr ? s->categories_off : 0u;
        mod.install_ask_never = s != nullptr && s->install_ask_never;
        if (mod.textures) {
            // The game's names, read once from the store: a pack's files are named by them.
            start_texture_sorting();
            convert_dolphin_textures(layout.textures, mod.id);
            (void)galaxy::textures::wait_for_index();
            read_pack(mod, layout.textures);
            start_texture_sorting();
            update_pack_categories(mod);
        }
        const bool first_time = s == nullptr;
        std::set<std::string> languages;
        if (!layout.files_root.empty()) {
            for (const char* language : kLanguageFolders) {
                if (child_any_case(layout.files_root, language, true)) languages.insert(language);
            }
        }
        // A mod new to the list goes on top (above every saved place), new ones by name.
        mod.order = s != nullptr && s->has_order ? s->order + 1000000u : static_cast<unsigned>(mods.size());
        if (mod.riivolution) {
            for (const RiivDisc& d : mod_discs(layout, s, game_id, &mod.problem)) {
                for (const Section& sec : d.sections) {
                    for (const Option& o : sec.options) {
                        ModOption option;
                        option.key = option_key(sec, o);
                        option.section = sec.name;
                        option.name = o.name;
                        for (const Choice& c : o.choices) {
                            option.choices.push_back(c.name);
                            const char region = choice_region(c.name);
                            option.other_region.push_back(region != 0 && region_choice(o, region_id) != 0u &&
                                                          region != region_id[3]);
                        }
                        option.selected = o.selected;
                        mod.options.push_back(std::move(option));
                    }
                }
                // What the chosen patches change: files, language folders, and memory patches over the game's code.
                for (const Patch& p : generate_patches(d, region_id)) {
                    const Loader loader = make_loader(layout.sd_root, d.xml, p.root);
                    std::error_code ec;
                    std::set<std::string>& paths = changed_paths[mod.id];
                    for (const FilePatch& f : p.files) {
                        if (const std::optional<fs::path> host = loader.resolve(f.external); host && fs::is_regular_file(*host, ec)) {
                            const std::string disc = lower(f.disc);
                            paths.insert(disc.starts_with('/') ? disc : "name:" + disc);
                        }
                    }
                    for (const FolderPatch& f : p.folders) {
                        if (const std::optional<fs::path> host = loader.resolve(f.external); host && fs::is_directory(*host, ec)) {
                            std::string disc = lower(f.disc);
                            while (disc.ends_with('/')) disc.pop_back();
                            list_files(*host, disc, paths);
                        }
                    }
                    for (const FilePatch& f : p.files) {
                        if (const std::string file_language = language_of(f.disc); !file_language.empty()) {
                            languages.insert(file_language);
                        }
                    }
                    for (const FolderPatch& f : p.folders) {
                        const std::optional<fs::path> patch_dir = loader.resolve(f.external);
                        if (!patch_dir || !fs::is_directory(*patch_dir, ec)) continue;
                        if (const std::string folder_language = language_of(f.disc); !folder_language.empty()) {
                            languages.insert(folder_language);
                        } else if (f.disc == "/" || f.disc.empty()) {
                            for (const char* candidate : kLanguageFolders) {
                                if (child_any_case(*patch_dir, candidate, true)) languages.insert(candidate);
                            }
                        }
                    }
                    mod.code_patches += static_cast<unsigned>(p.memory.size());
                    for (const MemoryPatch& m : p.memory) {
                        char where[16];
                        std::snprintf(where, sizeof(where), "%08X", static_cast<unsigned>(m.offset));
                        mod.code_patch_notes.push_back("the code change at " + std::string(where) + " (" + std::to_string(m.value.size()) +
                                                       (m.ocarina || m.search ? " bytes, searched" : " bytes") +
                                                       ") is not applied: Nebula has no Wii program. LumaLink turns the known ones "
                                                       "into tunables; the rest need a plugin");
                    }
                }
            }
        }
        if (mod.package) {
            // The answers as read, whatever the manifest does this start: one not read keeps them (consent 1.2).
            if (s != nullptr) mod.permission_answers = s->permissions;
            std::optional<luma::Manifest> manifest = luma::read_manifest(dir);
            // What refuses a mod is the validator's (its report at the start check); this reads what the game uses.
            if (!manifest) {
                mod.problem = "THIS FILE IS DAMAGED";
            } else {
                const std::string playing = language_folder(region_id, s_played_language.load(std::memory_order_relaxed));
                const std::string words = luma::language_code(playing);
                mod.pack_title = manifest->name.in(words);
                mod.pack_description = manifest->description.in(words);
                for (const std::string& author : manifest->authors) {
                    mod.pack_author += (mod.pack_author.empty() ? "" : ", ") + author;
                }
                // Code for other devices only: not started here, and the Mods page says why. This is the device's own
                // answer, not a check of the mod: the validator checks a mod once for every device (one form removed
                // the old refusal() with this line in it; test 2282).
                if (!manifest->plugins.empty() &&
                    !manifest->plugins.front().binaries.contains(std::string(luma::plugin_target()))) {
                    mod.problem = "THIS MOD HAS NO VERSION FOR THIS DEVICE";
                    std::fprintf(stderr, "[mods] %s: %s\n", mod.id.c_str(), mod.problem.c_str());
                }
                // Scripts for the interpreter plan 1.10b removed: none of them runs, so the mod does not, and says why.
                // LumaLink builds a mod's scripts into its code now.
                if (mod.problem.empty() &&
                    std::any_of(manifest->layers.begin(), manifest->layers.end(), [](const luma::Layer& layer) { return layer.old_scripts; })) {
                    mod.problem = "ITS CODE DID NOT START. IT WAS BUILT FOR AN OLDER NEBULA";
                    std::fprintf(stderr, "[mods] %s: %s (its luma.json has scripts for the removed interpreter)\n", mod.id.c_str(),
                                 mod.problem.c_str());
                }
                mod.code_token = plugin_token(dir, *manifest);
                mod.code_allowed = !mod.code_token.empty() && s != nullptr && s->code_ok == mod.code_token;
                mod.code_declined = !mod.code_token.empty() && !mod.code_allowed && s != nullptr && s->code_no == mod.code_token;
                mod.code_changed = !mod.code_token.empty() && !mod.code_allowed && !mod.code_declined && s != nullptr &&
                                   (!s->code_ok.empty() || !s->code_no.empty());
                if (mod.problem.empty() && !mod.code_token.empty() && s_code_off_after_crash.load(std::memory_order_relaxed)) {
                    mod.problem = "THIS MOD'S CODE IS OFF AFTER A CRASH. RESTART TO TRY IT AGAIN";
                }
                for (const luma::KeyOption& key : manifest->keys) {
                    mod.actions.push_back({key.id, key.name.in(words), key.fallback});
                }
                for (const luma::Option& entry : manifest->options) {
                    ModOption option;
                    option.key = entry.id;
                    option.kind = entry.kind;
                    if (entry.kind == "text" || entry.kind == "key") {
                        // Typed or bound, not chosen: no choices, nothing selected (the page's side fills .text, J 6.3).
                        option.name = entry.name.in(words);
                        option.section = entry.description.in(words);
                        mod.options.push_back(std::move(option));
                        continue;
                    }
                    if (entry.kind == "number") {
                        // A range, stepped on the Mods page: its value in .text (mods.ini choice.<key>), no choices.
                        option.name = entry.name.in(words);
                        option.section = entry.description.in(words);
                        option.text = entry.fallback;
                        if (s != nullptr) {
                            if (const auto it_id = s->choice_ids.find(entry.id); it_id != s->choice_ids.end()) {
                                if (const std::optional<std::string> kept = luma::number_value(entry, it_id->second)) {
                                    option.text = *kept;
                                } else {
                                    number_reset_once(mod.id, entry, it_id->second);
                                }
                            }
                        }
                        mod.options.push_back(std::move(option));
                        continue;
                    }
                    option.name = entry.name.in(words);
                    option.section = entry.description.in(words);
                    unsigned start = 0u;
                    for (const luma::Choice& choice : entry.choices) {
                        if (choice.id == "off") continue;
                        option.choices.push_back(choice.name.in(words));
                        option.choice_ids.push_back(choice.id);
                        if (choice.id == entry.fallback) start = static_cast<unsigned>(option.choices.size());
                    }
                    option.other_region.assign(option.choices.size(), false);
                    option.must_choose = option.choices.size() == entry.choices.size();
                    if (option.must_choose && start == 0u) start = 1u;
                    option.selected = start;
                    // A choice is kept by its id, so an update that adds choices doesn't move anyone's.
                    if (s != nullptr) {
                        if (const auto it_id = s->choice_ids.find(entry.id); it_id != s->choice_ids.end()) {
                            if (it_id->second == "off" && !option.must_choose) option.selected = 0u;
                            for (std::size_t c = 0; c < option.choice_ids.size(); ++c) {
                                if (option.choice_ids[c] == it_id->second) option.selected = static_cast<unsigned>(c + 1u);
                            }
                        } else if (const auto it_old = s->options.find(entry.id); it_old != s->options.end()) {
                            option.selected = std::min(it_old->second, static_cast<unsigned>(option.choices.size()));
                            if (option.must_choose && option.selected == 0u) option.selected = 1u;
                        }
                    }
                    mod.options.push_back(std::move(option));
                }
                // Text options' typed values (mods.ini text.<key>, services-mods 8.4). One that no longer fits (an
                // update shortened max_length) is dropped: the manifest's default stands.
                if (s != nullptr && !s->texts.empty()) {
                    for (const luma::Option& entry : manifest->options) {
                        const auto typed = s->texts.find(entry.id);
                        if (entry.kind != "text" || typed == s->texts.end() || !text_option_fits(typed->second, entry.max_length)) continue;
                        for (ModOption& option : mod.options) {
                            if (option.key == entry.id && option.kind == "text") option.text = typed->second;
                        }
                    }
                }
                if (manifest->save_player_can_change && manifest->save_mode != luma::SaveMode::Shared) {
                    ModOption option;
                    option.key = kSavesOption;
                    option.name = "SAVES";
                    option.choices = {"THIS MOD'S SAVES", "YOUR NORMAL SAVES"};
                    option.choice_ids = {"own", "normal"};
                    option.other_region.assign(2u, false);
                    option.must_choose = true;
                    option.selected = 1u;
                    if (s != nullptr) {
                        if (const auto it_id = s->choice_ids.find(option.key); it_id != s->choice_ids.end() && it_id->second == "normal") option.selected = 2u;
                    }
                    mod.options.push_back(std::move(option));
                }
                const luma::Situation situation = situation_of(mod, region_id, playing);
                std::set<std::string>& settings_set = changed_settings[mod.id];
                for (std::string& key : luma::setting_keys(static_cast<const luma::Rows&>(*manifest))) settings_set.insert(std::move(key));
                for (const luma::Layer& layer : manifest->layers) {
                    if (!luma::layer_used(layer, situation)) continue;
                    for (std::string& key : luma::setting_keys(layer)) settings_set.insert(std::move(key));
                    mod.luma_changes_game = mod.luma_changes_game || luma::layer_sets_game(layer);
                    if (layer.path.empty()) continue;
                    const fs::path place = dir / fs::path(from_utf8(layer.path));
                    std::error_code place_error;
                    if (!fs::is_directory(place, place_error)) {
                        mod.problem = "THIS FILE IS DAMAGED";
                        continue;
                    }
                    if (const std::optional<fs::path> merge = child_any_case(place, "merge", true)) {
                        mod.luma_changes_game = true;
                        std::error_code walk_error;
                        std::error_code status_error;
                        for (auto walk = fs::recursive_directory_iterator(*merge, walk_error);
                             !walk_error && walk != fs::recursive_directory_iterator(); walk.increment(walk_error)) {
                            if (!walk->is_regular_file(status_error)) continue;
                            const std::string under = galaxy::path_text(walk->path().lexically_relative(*merge).generic_string());
                            for (std::string& key : luma::merge_keys(under, read_text(walk->path()))) settings_set.insert(std::move(key));
                        }
                    }
                    // assets/ is the mod's own store (Luma/<id>/...): it changes what the cook writes, never another
                    // mod's paths.
                    if (child_any_case(place, "assets", true)) mod.luma_changes_game = true;
                    // Patches by key change the game's tables; what two mods' patches share the applier names at the cook.
                    if (child_any_case(place, "patches", true)) mod.luma_changes_game = true;
                    if (std::optional<fs::path> files = child_any_case(place, "files", true)) {
                        mod.luma_changes_game = true;
                        list_files(*files, std::string(), changed_paths[mod.id]);
                        // A movie is a file at its game path (G's decision 3 of the one form, luma-format 5): the
                        // movie player looks in files/MovieData for <name>.<video kind> (movie_playback.cpp find).
                        if (std::optional<fs::path> movie_data = child_any_case(*files, "MovieData", true)) {
                            mod.luma_movies.push_back(*movie_data);
                        }
                        for (const char* language : kLanguageFolders) {
                            if (child_any_case(*files, language, true)) languages.insert(language);
                        }
                    }
                    if (std::optional<fs::path> textures = child_any_case(place, "textures", true)) {
                        start_texture_sorting();
                        convert_dolphin_textures(*textures, mod.id);
                        (void)galaxy::textures::wait_for_index();
                        mod.luma_textures.push_back(*textures);
                        list_textures(*textures, changed_textures[mod.id]);
                    }
                }
                mod.luma_changes_game = mod.luma_changes_game || luma::manifest_sets_game(*manifest);
                if (!mod.luma_textures.empty()) {
                    const std::string title = mod.pack_title;
                    const std::string author = mod.pack_author;
                    const std::string description = mod.pack_description;
                    mod.textures = true;
                    read_pack(mod, mod.luma_textures.front());
                    start_texture_sorting();
                    update_pack_categories(mod);
                    mod.pack_title = title;
                    mod.pack_author = author;
                    mod.pack_description = description;
                }
                mod.movies = !mod.luma_movies.empty();
                mod.luma = std::make_shared<const luma::Manifest>(std::move(*manifest));
            }
        }
        mod.languages.assign(languages.begin(), languages.end());
        if (!layout.files_root.empty()) list_files(layout.files_root, std::string(), changed_paths[mod.id]);
        for (const fs::path& loose_file : layout.loose) changed_paths[mod.id].insert("name:" + lower(utf8_name(loose_file)));
        if (!layout.textures.empty()) list_textures(layout.textures, changed_textures[mod.id]);
        mod.changed_files = static_cast<unsigned>(changed_paths[mod.id].size());
        mod.changed_textures = static_cast<unsigned>(changed_textures[mod.id].size());
        if (mod.code_patches != 0u) {
            // Its files apply; its code patches never do (code_patch_notes, one per patch, go to the log). LumaLink turns
            // the known ones into tunables when it converts the mod.
            mod.problem = "MOST OF THIS MOD WORKS. " + std::to_string(mod.code_patches) + " CODE CHANGES NEED A NEBULA UPDATE OR A PLUGIN";
        }
        mod.unreadable = layout.unreadable;
        if (!mod.package && !mod.riivolution && !mod.files && !mod.textures && !mod.movies && !mod.loose) {
            mod.problem = mod.unreadable ? kUnreadableMod : "THIS DOESN'T LOOK LIKE A MOD";
        } else if (mod.unreadable && mod.problem.empty()) {
            mod.problem = kUnreadableMod;
        }
#if defined(__ANDROID__) || defined(__APPLE__) && TARGET_OS_IPHONE
        if (!mod.package && (mod.riivolution || mod.files || mod.loose)) {
            mod.problem = "CONVERT THIS MOD WITH LUMALINK ON A COMPUTER, THEN OPEN THE FILE HERE";
        }
#endif
        // The player blocked it (task 6.6): off whatever mods.ini says, and why; its page unblocks it.
        if (mod_blocked(mod.id)) {
            mod.enabled = false;
            mod.problem = "YOU BLOCKED THIS MOD. UNBLOCK IT ON ITS PAGE TO USE IT";
        }
        if (first_time && mod.luma) new_mods.insert(mod.id);
        mods.push_back(std::move(mod));
    }
    // Disabled mods have no place: they follow the enabled ones by name.
    std::stable_sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) {
        if (a.enabled != b.enabled) return a.enabled;
        return a.enabled ? a.order < b.order : lower(a.id) < lower(b.id);
    });
    // Where a mod new to the list asks to go; the player's own order then wins. The top of the list wins, so a mod that loads
    // after another goes above it.
    const auto place_of = [&mods](const std::string& luma_id) {
        return std::find_if(mods.begin(), mods.end(), [&](const ModInfo& m) { return m.enabled && m.luma && m.luma->id == luma_id; });
    };
    for (const std::string& id : new_mods) {
        const auto find_self = [&mods, &id]() { return std::find_if(mods.begin(), mods.end(), [&](const ModInfo& m) { return m.id == id; }); };
        if (find_self() == mods.end() || !find_self()->enabled) continue;
        const std::vector<std::string> after = find_self()->luma->load_after;
        const std::vector<std::string> before = find_self()->luma->load_before;
        for (const std::string& other : after) {
            const auto them = place_of(other);
            const auto self = find_self();
            if (them != mods.end() && self > them) std::rotate(them, self, self + 1);
        }
        for (const std::string& other : before) {
            const auto them = place_of(other);
            const auto self = find_self();
            if (them != mods.end() && self < them) std::rotate(self, self + 1, them + 1);
        }
    }
    // What each mod changes, kept with it: the list settles again whenever the player changes it (settle_mods).
    for (ModInfo& mod : mods) {
        mod.own_problem = mod.problem;
        auto changes = std::make_shared<ModChanges>();
        changes->paths = std::move(changed_paths[mod.id]);
        changes->textures = std::move(changed_textures[mod.id]);
        changes->settings = std::move(changed_settings[mod.id]);
        mod.changes = std::move(changes);
    }
    settle_mods(mods);
    return mods;
}

void mark_needs(std::vector<ModInfo>& mods) {
    const auto title = [](const ModInfo& other) { return other.pack_title.empty() ? other.id : other.pack_title; };
    // Again until nothing changes: a mod off for its need takes the mods needing it along.
    const auto find_luma = [&mods](const std::string& luma_id) -> const ModInfo* {
        for (const ModInfo& other : mods) {
            if (other.luma && other.luma->id == luma_id) return &other;
        }
        return nullptr;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (ModInfo& mod : mods) {
            if (!mod.luma || !mod.problem.empty() || !mod.enabled) continue;
            for (const luma::Dependency& need : mod.luma->needs) {
                const ModInfo* const them = find_luma(need.id);
                if (them == nullptr || !them->enabled || !them->problem.empty()) {
                    mod.problem = "THIS MOD NEEDS: " + (them != nullptr ? title(*them) : need.id);
                } else if (const int where = luma::version_in_range(them->luma->version, need.version); where < 0) {
                    mod.problem = "THIS MOD NEEDS A NEWER VERSION OF: " + title(*them);
                } else if (where > 0) {
                    mod.problem = "THIS MOD NEEDS: " + title(*them) + " " + need.version;
                }
                if (!mod.problem.empty()) break;
            }
            if (mod.problem.empty()) {
                for (const luma::Dependency& conflict : mod.luma->conflicts) {
                    const ModInfo* const them = find_luma(conflict.id);
                    if (them != nullptr && them != &mod && them->enabled && them->problem.empty()) {
                        mod.problem = "THIS MOD CAN'T RUN WITH: " + title(*them);
                        break;
                    }
                }
            }
            changed = changed || !mod.problem.empty();
        }
    }
}

void settle_mods(std::vector<ModInfo>& mods) {
    static const ModChanges kNone;
    const auto changes_of = [](const ModInfo& mod) -> const ModChanges& { return mod.changes ? *mod.changes : kNone; };
    for (ModInfo& mod : mods) {
        mod.problem = mod.own_problem;
        mod.overlaps.clear();
    }
    const auto title = [](const ModInfo& other) { return other.pack_title.empty() ? other.id : other.pack_title; };
    // Another copy of a mod on, what a mod needs and what it can't run with are the validator's (L800 to L802, each
    // start: modlog::check_packages, which leaves out every mod refused, and the mods needing it, as the host's
    // own pass did). Here only what the validator does not know: one family of saves at a time, the higher on the list
    // has them.
    for (ModInfo& mod : mods) {
        if (!mod.luma || !mod.problem.empty() || !mod.enabled) continue;
        const std::string tag = own_saves_tag(mod);
        if (tag.empty()) continue;
        for (const ModInfo& above : mods) {
            if (&above == &mod) break;
            const std::string theirs = above.enabled && above.problem.empty() ? own_saves_tag(above) : std::string();
            if (!theirs.empty() && theirs != tag) {
                mod.problem = "THIS MOD CAN'T RUN WITH: " + title(above);
                break;
            }
        }
    }
    for (std::size_t i = 0; i < mods.size(); ++i) mods[i].order = static_cast<unsigned>(i + 1u);
    // Overlaps between enabled mods: the same disc file (a file name alone matches any path ending in it) or texture.
    const auto base = [](const std::string& path) {
        if (path.starts_with("name:")) return path.substr(5);
        const std::size_t slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1u);
    };
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (!in_use(mods[i])) continue;
        for (std::size_t j = 0; j < mods.size(); ++j) {
            if (i == j || !in_use(mods[j])) continue;
            ModOverlap overlap;
            overlap.other = mods[j].id;
            overlap.other_above = j < i;
            const std::set<std::string>& mine = changes_of(mods[i]).paths;
            const std::set<std::string>& theirs = changes_of(mods[j]).paths;
            for (const std::string& path : mine) {
                if (theirs.count(path) != 0u) {
                    ++overlap.files;
                    continue;
                }
                // A file name alone against a full path.
                const bool name_only = path.starts_with("name:");
                const std::string name = base(path);
                for (const std::string& other : theirs) {
                    if ((name_only || other.starts_with("name:")) && base(other) == name) {
                        ++overlap.files;
                        break;
                    }
                }
            }
            const std::set<std::string>& my_textures = changes_of(mods[i]).textures;
            const std::set<std::string>& their_textures = changes_of(mods[j]).textures;
            for (const std::string& name : my_textures) overlap.textures += their_textures.count(name) != 0u ? 1u : 0u;
            // Settings: the same key, or a row one of them removes and the other changes a cell of.
            const std::set<std::string>& my_settings = changes_of(mods[i]).settings;
            const std::set<std::string>& their_settings = changes_of(mods[j]).settings;
            const auto has_cell_of = [](const std::set<std::string>& keys, const std::string& row) {
                const std::string prefix = "cell" + row.substr(3) + "\t";
                const auto at = keys.lower_bound(prefix);
                return at != keys.end() && at->starts_with(prefix);
            };
            for (const std::string& key : my_settings) {
                bool shared = their_settings.count(key) != 0u;
                if (!shared && key.starts_with("cell\t")) shared = their_settings.count("row" + key.substr(4, key.rfind('\t') - 4)) != 0u;
                if (!shared && key.starts_with("row\t")) shared = has_cell_of(their_settings, key);
                overlap.settings += shared ? 1u : 0u;
            }
            if (overlap.files != 0u || overlap.textures != 0u || overlap.settings != 0u) mods[i].overlaps.push_back(std::move(overlap));
        }
    }
}

void set_mod_enabled(std::vector<ModInfo>& mods, std::size_t index, bool enabled) {
    if (index >= mods.size() || mods[index].enabled == enabled) return;
    ModInfo mod = std::move(mods[index]);
    mods.erase(mods.begin() + static_cast<std::ptrdiff_t>(index));
    mod.enabled = enabled;
    auto at = mods.begin();
    if (!enabled) {
        const std::string name = lower(mod.id);
        at = std::find_if(mods.begin(), mods.end(), [&](const ModInfo& m) { return !m.enabled && name < lower(m.id); });
    }
    mods.insert(at, std::move(mod));
    for (std::size_t i = 0; i < mods.size(); ++i) mods[i].order = static_cast<unsigned>(i + 1u);
}

std::vector<OptionReset> option_resets() {
    const std::lock_guard<std::mutex> lock(g_resets_lock);
    return g_resets;
}

bool text_option_fits(std::string_view text, std::uint32_t max_length) noexcept {
    std::uint32_t points = 0u;
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i]);
        std::size_t more = 0u;
        std::uint32_t point = 0u;
        if (b < 0x80u) {
            point = b;
        } else if ((b & 0xE0u) == 0xC0u) {
            more = 1u;
            point = b & 0x1Fu;
        } else if ((b & 0xF0u) == 0xE0u) {
            more = 2u;
            point = b & 0x0Fu;
        } else if ((b & 0xF8u) == 0xF0u) {
            more = 3u;
            point = b & 0x07u;
        } else {
            return false;
        }
        if (i + more >= text.size()) return false;  // cut short
        for (std::size_t k = 1; k <= more; ++k) {
            const auto c = static_cast<unsigned char>(text[i + k]);
            if ((c & 0xC0u) != 0x80u) return false;
            point = (point << 6u) | (c & 0x3Fu);
        }
        // Shortest form only, no surrogates, nothing past U+10FFFF; no control characters (C0, DEL, C1).
        static constexpr std::uint32_t kLeast[4] = {0u, 0x80u, 0x800u, 0x10000u};
        if (point < kLeast[more] || (point >= 0xD800u && point <= 0xDFFFu) || point > 0x10FFFFu) return false;
        if (point < 0x20u || (point >= 0x7Fu && point <= 0x9Fu)) return false;
        if (++points > max_length) return false;
        i += more + 1u;
    }
    return true;
}

bool save_mod_settings(std::span<const ModInfo> mods) {
    fs::path file;
    {
        Settings& s = settings();
        const std::lock_guard<std::mutex> lock(s.mutex);
        file = s.file;
    }
    if (file.empty()) return false;
    std::ostringstream out;
    out << "; Nebula mods: their place in the list (order 1 is the top and wins), on/off and choices.\n";
    for (const ModInfo& mod : mods) {
        out << '[' << escape_key(mod.id) << "]\n";
        out << "enabled=" << (mod.enabled ? 1 : 0) << '\n';
        out << "order=" << mod.order << '\n';
        if (!mod.own_text) out << "text=game\n";
        if (mod.categories_off != 0u) out << "textures_off=" << mod.categories_off << '\n';
        if (mod.code_allowed) out << "code_ok=" << mod.code_token << '\n';
        if (mod.code_declined) out << "code_no=" << mod.code_token << '\n';
        if (mod.install_ask_never) out << "install.ask=no\n";
        // Answers for the permissions it declares now, with the digest each was given for (a stale one is asked again);
        // an answer for a permission it no longer declares is dropped here. A mod whose manifest was not read this start
        // keeps every answer as read (consent 1.2).
        const auto write_answer = [&out](const std::string& name, const PermissionAnswer& answer) {
            out << "permission." << escape_key(name) << '=' << (answer.granted ? "yes " : "no ") << answer.digest << '\n';
        };
        if (mod.luma != nullptr) {
            for (const luma::Permission& permission : mod.luma->permissions) {
                const auto answer = mod.permission_answers.find(permission.name);
                if (answer == mod.permission_answers.end()) continue;
                write_answer(permission.name, answer->second);
            }
        } else {
            for (const auto& [name, answer] : mod.permission_answers) {
                write_answer(name, answer);
            }
        }
        for (const ModOption& o : mod.options) {
            if (o.kind == "text" || o.kind == "key") {
                // A key option is bound in input's store (plan 2.5), not here; an empty text is the default's.
                if (o.kind == "text" && !o.text.empty()) out << "text." << escape_key(o.key) << '=' << escape_key(o.text) << '\n';
                continue;
            }
            if (o.kind == "number") {
                // The value itself (a range has no choice to number).
                if (!o.text.empty()) out << "choice." << escape_key(o.key) << '=' << o.text << '\n';
                continue;
            }
            out << "option." << escape_key(o.key) << '=' << o.selected << '\n';
            if (!o.choice_ids.empty()) {
                out << "choice." << escape_key(o.key) << '=' << (o.selected != 0u && o.selected <= o.choice_ids.size() ? o.choice_ids[o.selected - 1u] : std::string("off")) << '\n';
            }
        }
    }
    return io::write_file_atomically(file, out.str());
}

bool changes_game_files(const ModInfo& mod) noexcept {
    return mod.riivolution || mod.files || mod.loose || mod.luma_changes_game;
}

std::vector<std::filesystem::path> texture_folders(std::span<const ModInfo> mods) {
    std::vector<std::filesystem::path> out;
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        if (!in_use(*it) || !it->textures) continue;
        out.insert(out.end(), it->luma_textures.begin(), it->luma_textures.end());
        const ModLayout layout = layout_of(it->path);
        if (!layout.textures.empty()) out.push_back(layout.textures);
    }
    return out;
}

std::vector<std::uint32_t> texture_folder_categories_off(std::span<const ModInfo> mods) {
    std::vector<std::uint32_t> out;
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        if (!in_use(*it) || !it->textures) continue;
        out.insert(out.end(), it->luma_textures.size(), it->categories_off);
        if (!layout_of(it->path).textures.empty()) out.push_back(it->categories_off);
    }
    return out;
}

bool update_pack_categories(ModInfo& mod) {
    if (mod.pack_textures == nullptr || mod.categories_final) return false;
    const bool sorted = galaxy::textures::index_ready();
    galaxy::textures::CategoryCounts counts{};
    unsigned game_textures = 0;
    for (const PackTexture& texture : *mod.pack_textures) {
        std::optional<galaxy::textures::Category> category;
        if (const galaxy::textures::TextureName* const known = galaxy::textures::find_name(texture.hash)) {
            category = known->category;
            ++game_textures;
        } else if (texture.folder_category < galaxy::textures::kCategoryCount) {
            category = static_cast<galaxy::textures::Category>(texture.folder_category);
        }
        ++counts[static_cast<std::size_t>(category.value_or(galaxy::textures::Category::Other))];
    }
    const bool changed = counts != mod.category_textures || game_textures != mod.game_textures;
    mod.category_textures = counts;
    mod.game_textures = game_textures;
    mod.categories_final = sorted;
    // A pack with none of this game's textures is for another game (or another release's other art).
    if (sorted && game_textures == 0u && !mod.pack_textures->empty() && mod.own_problem.empty()) {
        mod.own_problem = "THESE TEXTURES ARE FOR ANOTHER GAME";
        if (mod.problem.empty()) mod.problem = mod.own_problem;
    }
    return changed || sorted;
}

void set_mods_store_file(const std::filesystem::path& store) {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.store = store;
}

std::vector<std::filesystem::path> movie_folders(std::span<const ModInfo> mods) {
    std::vector<std::filesystem::path> out;
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        if (!in_use(*it) || !it->movies) continue;
        // A .luma's layers' files/MovieData in its order (the last wins), then a folder's own movies/.
        out.insert(out.end(), it->luma_movies.begin(), it->luma_movies.end());
        const ModLayout layout = layout_of(it->path);
        if (!layout.movies.empty()) out.push_back(layout.movies);
    }
    return out;
}

std::string language_folder(const std::string& game_id, unsigned language) {
    if (game_id.size() < 4u) return {};
    if (game_id[3] == 'K') return "KrKorean";
    if (game_id[3] != 'E' && game_id[3] != 'P' && game_id[3] != 'J') return {};  // no other release's folders are known
    // The game's own choice (Language.cpp): a language its release lacks comes from the release that has it, as a
    // language pack mod supplies it.
    switch (language) {
    case 0u: return "JpJapanese";
    case 2u: return "EuGerman";
    case 5u: return "EuItalian";
    case 6u: return "EuDutch";
    default: break;
    }
    switch (game_id[3]) {
    case 'E':
        return language == 3u ? "UsFrench" : language == 4u ? "UsSpanish" : "UsEnglish";
    case 'P':
        return language == 3u ? "EuFrench" : language == 4u ? "EuSpanish" : "EuEnglish";
    case 'J':
        return language == 1u ? "UsEnglish" : language == 3u ? "EuFrench" : language == 4u ? "EuSpanish" : "EuEnglish";
    case 'K': return "KrKorean";
    default: return {};  // no other release's folders are known
    }
}

namespace {

// Where a disc file name (`name`: "Mario.arc"; `stored`, its store name in lower case) is in a store, archives as files:
// each place's folder in the store with `name` after it, a disc path as plans take.
void find_names(const galaxy::assets::AssetStore& store, std::int32_t folder, const std::string& path,
                const std::string& name, const std::string& stored, std::vector<std::string>& out) {
    const std::optional<galaxy::assets::Entry> self = store.entry(folder);
    if (!self) return;
    for (std::uint32_t c = 0; c < self->children; ++c) {
        const std::optional<galaxy::assets::Entry> child = store.child(folder, c);
        if (!child || child->name == nullptr) continue;
        const std::string child_name(child->name);
        if (lower(child_name) == stored) out.push_back(path + "/" + name);
        if (child->folder && !child->archive) {
            find_names(store, child->index, path + "/" + child_name, name, stored, out);
        }
    }
}

// A layer's (or the manifest's own) rows into the plan.
void add_layer_rows(ModPlan& plan, const ModInfo& mod, const std::string& language, const luma::Rows& rows) {
    for (const auto& [key, value] : rows.story) {
        plan.rows.story[key] = value;
    }
    for (const auto& [id, words] : rows.text) {
        const auto like = rows.text_like.find(id);
        plan.rows.texts.push_back({id, words.in(luma::language_code(language)), like != rows.text_like.end() ? like->second : std::string()});
    }
    for (const luma::Galaxy& galaxy : rows.galaxies) {
        plan.rows.galaxies.push_back({mod.id, galaxy.name, galaxy.map_pane, galaxy.open_conditions, galaxy.stars_to_open, galaxy.dome, galaxy.hungry_slot, galaxy.hungry_star_bits, galaxy.has_map_position, galaxy.map_x, galaxy.map_y});
    }
    for (const luma::Music& track : rows.music) {
        MusicToAdd row{mod.id, track.name, track.file, {}};
        for (const luma::MusicPlace& place : track.places) row.places.push_back({place.stage, place.scenario, place.change_area});
        plan.rows.music.push_back(std::move(row));
    }
    for (const luma::EventFlag& flag : rows.event_flags) {
        plan.rows.event_flags.push_back({mod.id, flag.name, flag.kind, flag.a, flag.b, flag.first, flag.second});
    }
    for (const luma::CometSchedule& dome : rows.comet_schedule) {
        plan.rows.comets.push_back({mod.id, dome.dome, dome.comets});
    }
}

}  // namespace

ModPlan plan_mod_changes(const galaxy::assets::AssetStore& store, const std::string& game_id,
                           const std::string& language, std::vector<ModInfo> mods) {
    g_mods_on.clear();
    for (const ModInfo& mod : mods) {
        if (mod.enabled) g_mods_on.push_back(mod.id);
    }
    ModPlan plan;
    std::vector<std::string> active;
    // Saves a mod gets for adding a galaxy give way to a higher mod's, and to saves a mod asks for.
    bool saves_for_galaxy = false;
    std::map<std::string, ModPlan::File> chosen;  // by the lower-case store path, the last set winning
    struct Placed {
        std::string disc;
        fs::path host;
    };
    // From the bottom of the list up: the mod on top sets a file last and wins it.
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        const ModInfo& mod = *it;
        if (!mod.enabled) continue;
        active.push_back(mod.id);
        plan.code_patches += mod.code_patches;
        for (const std::string& patch_note : mod.code_patch_notes) plan.notes.push_back(mod.id + ": " + patch_note);
        if (mod.package && !mod.problem.empty()) {
            plan.notes.push_back(mod.id + ": " + mod.problem);
            continue;
        }
        plan.used.push_back(mod.id);
        bool own_saves = false;
        if (const std::string tag = own_saves_tag(mod); !tag.empty()) {
            own_saves = true;
            saves_for_galaxy = false;
            plan.save_folder = saves_of(tag);
            plan.save_clone = mod.luma->save_mode == luma::SaveMode::Copy;
            plan.save_start = mod.luma->save_path.empty() ? fs::path() : mod.path / fs::path(from_utf8(mod.luma->save_path));
        }
        if (mod.package) plan.luma_folders.push_back(mod.path);
        if (mod.package && mod.luma) {
            add_layer_rows(plan, mod, language, *mod.luma);
            const std::string words = luma::language_code(language);
            for (const luma::Achievement& achievement : mod.luma->achievements) {
                plan.rows.achievements.push_back({mod.id, "mod:" + mod.luma->id + "/" + achievement.id, achievement.name.in(words),
                                                  achievement.description.in(words), mod.luma->name.in(words), achievement.hidden,
                                                  achievement.event, achievement.event_name, achievement.has_value,
                                                  achievement.event_value});
            }
            const luma::Situation situation = situation_of(mod, game_id, language);
            // The mods' code binds from the top of the list down (loader contract 1): this loop goes bottom up, so
            // each mod's libraries go in front of the lower mods' already listed, in the mod's own order.
            const std::size_t lower_mods = plan.plugins.size();
            for (const luma::Plugin& plugin : mod.luma->plugins) {
                const auto binary = plugin.binaries.find(std::string(luma::plugin_target()));
                if (binary == plugin.binaries.end()) continue;
                if (!mod.code_allowed) {
                    plan.notes.push_back(mod.id + ": its code isn't run: the player hasn't allowed it");
                    break;
                }
                const fs::path library = mod.path / fs::path(from_utf8(binary->second));
                plan.plugins.push_back({library, mod.id, mod.luma->version, situation.options, code_hash(library), {}, granted_permissions(mod),
                                        declined_permissions(mod)});
                for (const luma::Dependency& need : mod.luma->needs) plan.plugins.back().needs.push_back(need.id);
                for (const luma::Layer& layer : mod.luma->layers) {
                    if (layer.when.code) plan.plugins.back().code_layers.push_back(luma::layer_name(layer));
                }
            }
            std::rotate(plan.plugins.begin(), plan.plugins.begin() + static_cast<std::ptrdiff_t>(lower_mods), plan.plugins.end());
        }
        if (!changes_game_files(mod)) continue;
#if defined(__ANDROID__) || defined(__APPLE__) && TARGET_OS_IPHONE
        // The converter runs on computers: the page has told the player.
        if (!mod.package) continue;
#endif
        const ModLayout layout = layout_of(mod.path);
        std::vector<Placed> placed;
        const auto note = [&plan, &mod](const std::string& text) { plan.notes.push_back(mod.id + ": " + text); };
        if (layout.unreadable) note("its folder could not be read in full");
        const auto set_file = [&placed](std::string disc, const fs::path& host) {
            if (!disc.starts_with('/')) disc.insert(disc.begin(), '/');
            placed.push_back({std::move(disc), host});
        };
        const auto patch_file = [&](const Loader& loader, const FilePatch& f) {
            const std::optional<fs::path> host = loader.resolve(f.external);
            std::error_code ec;
            if (!host || !fs::is_regular_file(*host, ec)) {
                note(f.external + ": not in the mod");
                return;
            }
            if ((f.offset & ~std::uint64_t{3}) != 0u || f.file_offset != 0u || f.length != 0u || !f.resize) {
                // The game's files are cooked: the disc file's offsets are not theirs.
                note(f.disc + ": patched in part by the disc file's offsets, which the game's files do not have");
                return;
            }
            if (lower(f.disc).ends_with(".dol")) {
                note(f.disc + ": the native game has no such file");
                return;
            }
            if (!f.disc.empty() && f.disc[0] == '/') {
                if (!f.create && !store.find(galaxy::assets::store_path(f.disc))) {
                    note(f.disc + ": the game has no such file and the patch does not create it");
                    return;
                }
                set_file(f.disc, *host);
                return;
            }
            std::vector<std::string> paths;
            find_names(store, 0, {}, f.disc, lower(galaxy::assets::store_path(f.disc)), paths);
            if (paths.empty()) note(f.disc + ": no file of that name in the game");
            for (const std::string& path : paths) set_file(path, *host);
        };
        const auto join = [](std::string a, std::string_view b) {
            if (a.empty()) return std::string(b);
            if (b.empty()) return a;
            if (a.back() == '/') a.pop_back();
            while (b.starts_with('/')) b.remove_prefix(1);
            return a + "/" + std::string(b);
        };
        const auto patch_folder = [&](auto&& self, const Loader& loader, const FolderPatch& folder,
                                      const std::string& disc, const std::string& external) -> void {
            const std::optional<fs::path> dir = loader.resolve(external);
            std::error_code ec;
            if (!dir || !fs::is_directory(*dir, ec)) {
                note(external + ": no such folder in the mod");
                return;
            }
            bool unreadable = false;
            std::vector<fs::directory_entry> entries = entries_of(*dir, &unreadable);
            if (unreadable) note(external + ": the folder could not be read in full");
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
            for (const auto& entry : entries) {
                const std::string name = utf8_name(entry.path());
                if (entry.is_directory(ec)) {
                    if (folder.recursive) self(self, loader, folder, join(disc, name), join(external, name));
                    continue;
                }
                FilePatch f;
                f.disc = join(disc, name);
                f.external = join(external, name);
                f.resize = folder.resize;
                f.create = true;  // a folder's files add to the game's
                f.length = folder.length;
                patch_file(loader, f);
            }
        };
        if (mod.package && mod.luma) {
            const luma::Situation situation = situation_of(mod, game_id, language);
            for (const luma::Layer& layer : mod.luma->layers) {
                if (!luma::layer_used(layer, situation)) continue;
                add_layer_rows(plan, mod, language, layer.rows);
                for (const luma::ObjectChange& change : layer.object_changes) {
                    plan.rows.object_changes.push_back({mod.id, change.where, change.object, change.becomes});
                }
                for (const luma::CometLimit& limit : layer.comet_time_limits) {
                    plan.rows.comet_limits.push_back({mod.id, limit.galaxy, limit.mission, limit.seconds});
                }
                for (const luma::StageRule& rule : layer.stages) {
                    plan.rows.stage_rules.push_back({mod.id, rule.stage, rule.scenario, rule.acts_as, rule.own_rules});
                }
                for (const luma::PlayerConstant& constant : layer.player_constants) {
                    float value = constant.value;
                    if (!constant.option.empty()) {
                        const auto choice = situation.options.find(constant.option);
                        if (choice == situation.options.end()) continue;
                        value = std::strtof(choice->second.c_str(), nullptr);
                    }
                    plan.rows.player_constants.push_back({mod.id, constant.character, constant.name, constant.index, value});
                }
                for (const luma::Tunable& tunable : layer.tunables) {
                    double value = tunable.value;
                    if (!tunable.option.empty()) {
                        const auto choice = situation.options.find(tunable.option);
                        if (choice == situation.options.end()) continue;
                        value = std::strtod(choice->second.c_str(), nullptr);
                    }
                    plan.rows.tunables.push_back({mod.id, tunable.key, value});
                }
                for (const luma::SoundEffect& sound : layer.sound_effects) {
                    plan.rows.sound_effects.push_back({mod.id, sound.name, sound.file});
                }
                for (const luma::PlayerAnimation& animation : layer.player_animations) {
                    plan.rows.player_animations.push_back({mod.id, animation.name, animation.file, animation.rate, animation.blend_frames});
                }
                for (const luma::ObjectClass& object : layer.object_classes) {
                    plan.rows.object_classes.push_back({mod.id, object.name, object.like});
                }
                for (const luma::Form& form : layer.forms) {
                    plan.rows.forms.push_back({mod.id, form.name, form.acts_like, form.looks_like, form.color_frame, form.frames, form.model,
                                               form.music});
                }
                for (const auto& [material, file] : layer.materials) {
                    plan.materials.push_back({material, mod.path / fs::path(from_utf8(file))});
                }
                if (layer.path.empty()) continue;
                if (const std::optional<fs::path> merge = child_any_case(mod.path / fs::path(from_utf8(layer.path)), "merge", true)) {
                    std::error_code walk_error;
                    std::vector<fs::path> found;
                    std::error_code status_error;
                    for (auto walk = fs::recursive_directory_iterator(*merge, walk_error);
                         !walk_error && walk != fs::recursive_directory_iterator(); walk.increment(walk_error)) {
                        if (walk->is_regular_file(status_error)) found.push_back(walk->path());
                    }
                    if (walk_error) note(layer.path + "/merge: the folder could not be read in full");
                    std::sort(found.begin(), found.end());
                    for (const fs::path& file : found) {
                        plan.merges.push_back({galaxy::path_text(file.lexically_relative(*merge).generic_string()), file});
                    }
                }
                // A layer's assets/ goes into the mod's own store at Luma/<id>/<path> (luma-format 5, G's decision of
                // 2026-11-11): one plan line a file, a later layer's same path winning as files/ does. assets/effects.json
                // is each layer's change instead, merged by the cook in layer order (contracts/effects.md).
                if (const std::optional<fs::path> assets = child_any_case(mod.path / fs::path(from_utf8(layer.path)), "assets", true)) {
                    std::error_code walk_error;
                    std::error_code status_error;
                    std::vector<fs::path> found;
                    for (auto walk = fs::recursive_directory_iterator(*assets, walk_error);
                         !walk_error && walk != fs::recursive_directory_iterator(); walk.increment(walk_error)) {
                        if (walk->is_regular_file(status_error)) found.push_back(walk->path());
                    }
                    if (walk_error) note(layer.path + "/assets: the folder could not be read in full");
                    std::sort(found.begin(), found.end());
                    const std::string own = "Luma/" + mod.luma->id + "/";
                    for (const fs::path& file : found) {
                        const std::string under = galaxy::path_text(file.lexically_relative(*assets).generic_string());
                        if (under == "effects.json") {
                            plan.merges.push_back({own + under, file});
                        } else {
                            set_file(own + under, file);
                        }
                    }
                }
                // Patches by key: the cooker applies them to the game's tables (contract table-keys).
                if (const std::optional<fs::path> patches = child_any_case(mod.path / fs::path(from_utf8(layer.path)), "patches", true)) {
                    std::error_code walk_error;
                    std::error_code status_error;
                    std::vector<fs::path> found;
                    for (auto walk = fs::recursive_directory_iterator(*patches, walk_error);
                         !walk_error && walk != fs::recursive_directory_iterator(); walk.increment(walk_error)) {
                        if (walk->is_regular_file(status_error)) found.push_back(walk->path());
                    }
                    if (walk_error) note(layer.path + "/patches: the folder could not be read in full");
                    std::sort(found.begin(), found.end());
                    for (const fs::path& file : found) {
                        const std::string relative = galaxy::path_text(file.lexically_relative(*patches).generic_string());
                        const std::string entry = layer.path + "/" + utf8_name(*patches) + "/" + relative;
                        if (relative.size() <= 5u || lower(relative.substr(relative.size() - 5u)) != ".json") {
                            // The validator refuses it (L306's place); a mod that got here anyway is named, not misread.
                            note(entry + ": a patch is <game path>.json; this file is not used");
                            continue;
                        }
                        plan.patches.push_back({mod.id, entry, relative.substr(0, relative.size() - 5u), file});
                    }
                }
                // Texture pack pictures, for the cooker to read as the pack reader does. A Dolphin pack's file (tex1_)
                // is converted first, and a converted pack's own folder holds no pictures of the mod's.
                if (const std::optional<fs::path> textures = child_any_case(mod.path / fs::path(from_utf8(layer.path)), "textures", true)) {
                    std::error_code walk_error;
                    std::error_code status_error;
                    std::vector<fs::path> found;
                    for (auto walk = fs::recursive_directory_iterator(*textures, walk_error);
                         !walk_error && walk != fs::recursive_directory_iterator(); walk.increment(walk_error)) {
                        if (walk->is_directory(status_error) && lower(utf8_name(walk->path())) == ".dolphin") {
                            walk.disable_recursion_pending();
                        } else if (walk->is_regular_file(status_error)) {
                            found.push_back(walk->path());
                        }
                    }
                    if (walk_error) note(layer.path + "/textures: the folder could not be read in full");
                    std::sort(found.begin(), found.end());
                    for (const fs::path& file : found) {
                        const std::string ending = lower(utf8_name(file.extension()));
                        if ((ending != ".png" && ending != ".dds") || galaxy::textures::is_dolphin_pack_name(utf8_name(file.stem()))) continue;
                        const std::string relative = galaxy::path_text(file.lexically_relative(*textures).generic_string());
                        plan.textures.push_back({mod.id, layer.path + "/" + utf8_name(*textures) + "/" + relative, std::string(), file});
                    }
                }
                const std::optional<fs::path> files = child_any_case(mod.path / fs::path(from_utf8(layer.path)), "files", true);
                if (!files) continue;
                const Loader loader{*files, *files};
                const FolderPatch whole{"/", "/", true, true, true, 0};
                std::error_code ec;
                bool files_unreadable = false;
                const std::vector<fs::directory_entry> top = entries_of(*files, &files_unreadable);
                if (files_unreadable) note(layer.path + "/files: the folder could not be read in full");
                for (const auto& entry : top) {
                    const std::string name = utf8_name(entry.path());
                    if (entry.is_directory(ec)) {
                        patch_folder(patch_folder, loader, whole, "/" + name, "/" + name);
                    } else {
                        FilePatch f;
                        f.disc = "/" + name;
                        f.external = "/" + name;
                        f.create = true;
                        patch_file(loader, f);
                    }
                }
            }
        }
        if (!layout.xmls.empty()) {
            const std::map<std::string, SavedMod> saved = read_saved(settings().file);
            const auto saved_it = saved.find(mod.id);
            for (const RiivDisc& d : mod_discs(layout, saved_it != saved.end() ? &saved_it->second : nullptr, game_id,
                                               nullptr)) {
                for (const Patch& p : generate_patches(d, game_id)) {
                    const Loader loader = make_loader(layout.sd_root, d.xml, p.root);
                    for (const FilePatch& f : p.files) patch_file(loader, f);
                    for (const FolderPatch& f : p.folders) {
                        patch_folder(patch_folder, loader, f, f.disc.empty() ? std::string("/") : f.disc, f.external);
                    }
                    for (const SavePatch& save : p.saves) {
                        // One save folder for the game: the mod higher in the list has it.
                        if (const std::optional<fs::path> folder = loader.resolve(save.external)) {
                            own_saves = true;
                            saves_for_galaxy = false;
                            plan.save_folder = *folder;
                            plan.save_clone = save.clone;
                            plan.save_start.clear();
                        }
                    }
                }
            }
        }
        if (!layout.files_root.empty()) {
            const Loader loader{layout.files_root, layout.files_root};
            const FolderPatch whole{"/", "/", true, true, true, 0};
            std::error_code ec;
            bool root_unreadable = false;
            const std::vector<fs::directory_entry> top = entries_of(layout.files_root, &root_unreadable);
            if (root_unreadable) note(utf8_name(layout.files_root) + ": the folder could not be read in full");
            for (const auto& entry : top) {
                const std::string name = utf8_name(entry.path());
                if (entry.is_directory(ec)) {
                    // Beside the game's folders a mod keeps its notes and pictures: only what the game has counts.
                    if (layout.files_root != mod.path || is_disc_folder(name)) {
                        patch_folder(patch_folder, loader, whole, "/" + name, "/" + name);
                    }
                } else if (!mod_note(entry.path())) {
                    FilePatch f;
                    f.disc = "/" + name;
                    f.external = "/" + name;
                    f.create = true;
                    patch_file(loader, f);
                }
            }
        }
        for (const fs::path& file : layout.loose) {
            std::vector<std::string> paths;
            find_names(store, 0, {}, utf8_name(file), lower(galaxy::assets::store_path(utf8_name(file))), paths);
            if (paths.empty()) note(utf8_name(file) + ": no file of that name in the game");
            for (const std::string& path : paths) set_file(path, file);
        }
        // Text in the language played: a mod with text for other languages only shows its own (the same language of
        // another region first, else English, else what it has) unless its TEXT choice keeps the game's.
        if (!language.empty() && mod.own_text) {
            std::set<std::string> covered;
            for (const Placed& p : placed) {
                if (const std::string l = language_of(p.disc); !l.empty()) covered.insert(l);
            }
            if (!covered.empty() && covered.count(language) == 0u) {
                const std::string twin = language.starts_with("Us")   ? "Eu" + language.substr(2)
                                         : language.starts_with("Eu") ? "Us" + language.substr(2)
                                                                      : std::string();
                std::string source;
                for (const std::string& candidate : {twin, std::string("UsEnglish"), std::string("EuEnglish")}) {
                    if (!candidate.empty() && covered.count(candidate) != 0u) {
                        source = candidate;
                        break;
                    }
                }
                if (source.empty()) source = *covered.begin();
                for (const Placed& p : std::vector<Placed>(placed)) {
                    if (language_of(p.disc) == source) {
                        set_file("/" + language + p.disc.substr(source.size() + 1u), p.host);
                    }
                }
            }
        }
        for (const Placed& p : placed) {
            // The game's own tables, but for the one that says where each galaxy is and what opens it.
            if (lower(p.disc).starts_with("/nebuladata/") && lower(p.disc) != "/nebuladata/galaxyinfotable.bcsv") continue;
            // A save's shape follows the galaxy list: a mod that adds a galaxy never plays on normal saves.
            if (!own_saves && (plan.save_folder.empty() || saves_for_galaxy) && lower(p.disc).starts_with("/stagedata/") && lower(p.disc).ends_with("scenario.arc") && !store.find(galaxy::assets::store_path(p.disc))) {
                plan.save_folder = saves_of(mod.luma ? mod.luma->save_tag : mod.id);
                plan.save_clone = false;
                plan.save_start.clear();
                own_saves = true;
                saves_for_galaxy = true;
                note("it adds a galaxy, so it keeps its own saves");
            }
            chosen[lower(galaxy::assets::store_path(p.disc))] = {p.disc, p.host, mod.id};
        }
    }
    for (auto& [key, file] : chosen) plan.files.push_back(std::move(file));
    // The plan is lines of tab-separated fields: a name holding a tab or a line end would be read as other fields or
    // another line by the cooker, so such a file is left out and named.
    const auto plain = [](const std::string& text) { return text.find_first_of("\t\r\n") == std::string::npos; };
    const auto keep_files = [&](std::vector<ModPlan::File>& list, const char* what) {
        std::erase_if(list, [&](const ModPlan::File& file) {
            if (plain(file.disc) && plain(galaxy::path_text(file.host))) return false;
            plan.notes.push_back(std::string(what) + " " + galaxy::path_text(file.host) + " is not used: its name holds a tab or a line end");
            return true;
        });
    };
    keep_files(plan.files, "the file");
    keep_files(plan.merges, "the merge file");
    keep_files(plan.materials, "the material file");
    std::erase_if(plan.patches, [&](const ModPlan::Patch& patch) {
        if (plain(patch.entry) && plain(patch.disc) && plain(galaxy::path_text(patch.host))) return false;
        plan.notes.push_back(patch.mod_id + ": " + galaxy::path_text(patch.host) + " is not used: its name holds a tab or a line end");
        return true;
    });
    std::erase_if(plan.textures, [&](const ModPlan::Patch& texture) {
        if (plain(texture.entry) && plain(galaxy::path_text(texture.host))) return false;
        plan.notes.push_back(texture.mod_id + ": " + galaxy::path_text(texture.host) + " is not checked: its name holds a tab or a line end");
        return true;
    });
    {
        Session& s = session();
        const std::lock_guard<std::mutex> lock(s.mutex);
        s.mods = SessionMods{mods, active, static_cast<unsigned>(plan.files.size()), plan.code_patches};
    }
    return plan;
}

std::string plan_text(const ModPlan& plan, const std::string& game_id) {
    std::string text = game_id + "\n";
    {
        Settings& s = settings();
        const std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.store.empty()) text += "base\t" + galaxy::path_text(s.store) + "\n";
    }
    for (const ModPlan::File& file : plan.files) {
        text += file.disc + "\t" + galaxy::path_text(file.host) + "\n";
    }
    for (const ModPlan::File& file : plan.merges) {
        text += "+" + file.disc + "\t" + galaxy::path_text(file.host) + "\n";
    }
    for (const ModPlan::File& file : plan.materials) {
        text += "=material\t" + file.disc + "\t" + galaxy::path_text(file.host) + "\n";
    }
    // Patches by key and the mod order they apply in (the cooker gives both to the applier).
    if (!plan.patches.empty()) {
        text += "%mods";
        for (const std::string& id : plan.used) text += "\t" + id;
        text += "\n";
    }
    for (const ModPlan::Patch& patch : plan.patches) {
        text += "%patch\t" + patch.mod_id + "\t" + patch.entry + "\t" + patch.disc + "\t" + galaxy::path_text(patch.host) + "\n";
    }
    for (const ModPlan::Patch& texture : plan.textures) {
        text += "%texture\t" + texture.mod_id + "\t" + texture.entry + "\t" + galaxy::path_text(texture.host) + "\n";
    }
    return text;
}

std::string plan_signature(const ModPlan& plan, const std::string& game_id) {
    // The store's version too: a mods.pak of another version is not opened (asset_store.cpp), so it is cooked again.
    std::string text = "1 " + game_id + " store " + std::to_string(galaxy::assets::kStoreVersion) + "\n";
    for (const ModPlan::File& file : plan.files) {
        std::error_code ec;
        const auto size = fs::file_size(file.host, ec);
        const auto time = fs::last_write_time(file.host, ec).time_since_epoch().count();
        text += file.disc + "\t" + galaxy::path_text(file.host) + "\t" + std::to_string(size) + "\t" +
                std::to_string(static_cast<long long>(time)) + "\n";
    }
    for (const ModPlan::File& file : plan.materials) {
        std::error_code ec;
        const auto size = fs::file_size(file.host, ec);
        const auto time = fs::last_write_time(file.host, ec).time_since_epoch().count();
        text += "=material\t" + file.disc + "\t" + galaxy::path_text(file.host) + "\t" + std::to_string(size) + "\t" +
                std::to_string(static_cast<long long>(time)) + "\n";
    }
    for (const ModPlan::File& file : plan.merges) {
        std::error_code ec;
        const auto size = fs::file_size(file.host, ec);
        const auto time = fs::last_write_time(file.host, ec).time_since_epoch().count();
        text += "+" + file.disc + "\t" + galaxy::path_text(file.host) + "\t" + std::to_string(size) + "\t" +
                std::to_string(static_cast<long long>(time)) + "\n";
    }
    // A patch's mod order matters too: the same files in another order cook another store.
    if (!plan.patches.empty()) {
        text += "%mods";
        for (const std::string& id : plan.used) text += "\t" + id;
        text += "\n";
    }
    for (const ModPlan::Patch& patch : plan.patches) {
        std::error_code ec;
        const auto size = fs::file_size(patch.host, ec);
        const auto time = fs::last_write_time(patch.host, ec).time_since_epoch().count();
        text += "%patch\t" + patch.mod_id + "\t" + patch.entry + "\t" + patch.disc + "\t" + galaxy::path_text(patch.host) + "\t" +
                std::to_string(size) + "\t" + std::to_string(static_cast<long long>(time)) + "\n";
    }
    for (const ModPlan::Patch& texture : plan.textures) {
        std::error_code ec;
        const auto size = fs::file_size(texture.host, ec);
        const auto time = fs::last_write_time(texture.host, ec).time_since_epoch().count();
        text += "%texture\t" + texture.mod_id + "\t" + texture.entry + "\t" + galaxy::path_text(texture.host) + "\t" +
                std::to_string(size) + "\t" + std::to_string(static_cast<long long>(time)) + "\n";
    }
    return text;
}

SessionMods session_mods() {
    Session& s = session();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.mods;
}

bool mods_differ_from_session(std::span<const ModInfo> mods) {
    Session& s = session();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return mods_signature(mods) != mods_signature(s.mods.started_with);
}

namespace {
fs::path code_layers_file() {
    const fs::path folder = mods_folder();
    return folder.empty() ? fs::path() : folder / ".cooked" / "code-layers";
}
}  // namespace

CodeLayers code_layers_record() {
    CodeLayers record;
    const fs::path file = code_layers_file();
    if (file.empty()) return record;
    const std::string text = read_text(file);
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string_view line(text.data() + at, end - at);
        at = end + 1u;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        std::vector<std::string> parts;
        for (std::size_t from = 0;;) {
            const std::size_t tab = line.find('\t', from);
            parts.emplace_back(line.substr(from, tab == std::string_view::npos ? std::string_view::npos : tab - from));
            if (tab == std::string_view::npos) break;
            from = tab + 1u;
        }
        std::vector<std::string>& names = record[parts[0]];
        names.assign(parts.begin() + 1, parts.end());
    }
    return record;
}

bool write_code_layers_record(const CodeLayers& record) {
    const fs::path file = code_layers_file();
    if (file.empty()) return false;
    std::string text;
    for (const auto& [mod, names] : record) {
        text += mod;
        for (const std::string& name : names) text += "\t" + name;
        text += "\n";
    }
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    return io::write_file_atomically(file, text);
}

std::filesystem::path code_layers_restart_file() {
    const fs::path file = code_layers_file();
    return file.empty() ? fs::path() : fs::path(file).replace_filename("code-layers.started-again");
}

}  // namespace galaxy::mods
