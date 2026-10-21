// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h). Riivolution's patch format follows Dolphin's implementation (DiscIO RiivolutionParser.cpp and
// RiivolutionPatcher.cpp, GPL-2.0-or-later): external paths, filename-only disc paths, option parameters, config
// defaults and patch order are Riivolution's as Dolphin reproduces them.

#include "galaxy/mods.h"

#include "galaxy/atomic_file.h"
#include "galaxy/mod_xml.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
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

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A child of `dir` named `name` whatever its case (Riivolution assumes an SD card's case-insensitive names).
std::optional<fs::path> child_any_case(const fs::path& dir, std::string_view name, bool want_directory) {
    std::error_code error;
    const fs::path exact = dir / from_utf8(name);
    if (fs::exists(exact, error) && fs::is_directory(exact, error) == want_directory) return exact;
    for (const auto& entry : fs::directory_iterator(dir, error)) {
        if (same_name(utf8_name(entry.path()), name) && entry.is_directory(error) == want_directory) return entry.path();
    }
    return std::nullopt;
}

// --- Riivolution --------------------------------------------------------------------------------------------------

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
    std::vector<std::uint8_t> value, original;
    std::string valuefile;
    bool ocarina = false, search = false;
    std::uint32_t align = 1;
};
struct Patch {
    std::string id, root;
    std::vector<FilePatch> files;
    std::vector<FolderPatch> folders;
    std::vector<SavePatch> saves;
    std::vector<MemoryPatch> memory;
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
                m.valuefile = n.attribute("valuefile");
                m.original = hex_bytes(n.attribute("original"));
                m.ocarina = n.attribute_bool("ocarina", false);
                m.search = n.attribute_bool("search", false);
                m.align = static_cast<std::uint32_t>(n.attribute_uint("align", 1));
            }
        }
    }
    return disc;
}

bool valid_for_game(const RiivDisc& d, const std::string& game_id, int revision, int disc_number) {
    if (game_id.size() != 6u) return false;
    if (d.game && !game_id.starts_with(*d.game)) return false;
    if (d.developer && game_id.substr(4, 2) != *d.developer) return false;
    if (d.disc && *d.disc != disc_number) return false;
    if (d.version && *d.version != revision) return false;
    if (d.regions && std::find(d.regions->begin(), d.regions->end(), game_id.substr(3, 1)) == d.regions->end()) {
        return false;
    }
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

// The SD card's and the patch's roots of a Riivolution mod, and its external paths (Dolphin FileDataLoaderHostFS).
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
            if (std::all_of(element.begin(), element.end(), [](char c) { return c == '.'; })) return std::nullopt;
            ++depth;
            std::error_code error;
            const fs::path exact = result / from_utf8(element);
            if (fs::exists(exact, error)) {
                result = exact;
                continue;
            }
            bool found = false;
            for (const auto& entry : fs::directory_iterator(result, error)) {
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
                for (MemoryPatch& m : p.memory) m.valuefile = replace_variables(m.valuefile, with);
                active.push_back(std::move(p));
            }
        }
    }
    return active;
}

// --- Mod folders ---------------------------------------------------------------------------------------------------

// The disc's top-level folders of every release: a mod folder holding one of them is files in the disc's layout.
constexpr const char* kDiscFolders[] = {"AudioRes", "LayoutData", "MapPartsData", "MovieData", "ObjectData",
                                         "ParticleData", "StageData", "SystemData", "UsEnglish", "UsFrench",
                                         "UsSpanish", "EuEnglish", "EuFrench", "EuGerman", "EuItalian", "EuSpanish",
                                         "JpJapanese", "KrKorean", "CnSimpChinese", "TwTradChinese", "HomeButton2"};

struct ModLayout {
    fs::path sd_root;           // Riivolution: where riivolution/ is
    fs::path riivolution_dir;
    std::vector<fs::path> xmls;
    fs::path files_root;        // the disc's layout
    fs::path textures;
};

ModLayout layout_of(const fs::path& mod) {
    ModLayout layout;
    std::error_code error;
    // riivolution/ in the mod, or one folder down (an archive's own top folder).
    std::vector<fs::path> roots{mod};
    for (const auto& entry : fs::directory_iterator(mod, error)) {
        if (entry.is_directory(error)) roots.push_back(entry.path());
    }
    for (const fs::path& root : roots) {
        if (std::optional<fs::path> riiv = child_any_case(root, "riivolution", true)) {
            layout.sd_root = root;
            layout.riivolution_dir = *riiv;
            for (const auto& entry : fs::directory_iterator(*riiv, error)) {
                if (entry.is_regular_file(error) && lower(entry.path().extension().string()) == ".xml") {
                    layout.xmls.push_back(entry.path());
                }
            }
            std::sort(layout.xmls.begin(), layout.xmls.end());
            break;
        }
    }
    if (std::optional<fs::path> files = child_any_case(mod, "files", true)) {
        layout.files_root = *files;
    } else {
        for (const char* folder : kDiscFolders) {
            if (child_any_case(mod, folder, true)) {
                layout.files_root = mod;
                break;
            }
        }
    }
    if (std::optional<fs::path> textures = child_any_case(mod, "textures", true)) layout.textures = *textures;
    return layout;
}

struct SavedMod {
    bool enabled = true;
    unsigned order = 0;
    bool has_order = false;
    std::map<std::string, unsigned> options;
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
    std::mutex mutex;
    fs::path folder;
    fs::path file;
    std::string game_id;
};

Settings& settings() {
    static Settings s;
    return s;
}

struct Session {
    std::mutex mutex;
    SessionMods mods;
};

Session& session() {
    static Session s;
    return s;
}

// What a restart would change: the enabled mods, their order and their choices.
std::string mods_signature(std::span<const ModInfo> mods) {
    std::vector<const ModInfo*> sorted;
    for (const ModInfo& m : mods) sorted.push_back(&m);
    std::stable_sort(sorted.begin(), sorted.end(), [](const ModInfo* a, const ModInfo* b) { return a->order < b->order; });
    std::string out;
    for (const ModInfo* m : sorted) {
        if (!m->enabled) continue;
        out += m->id + '\n';
        for (const ModOption& o : m->options) out += o.key + '=' + std::to_string(o.selected) + '\n';
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
        } else if (key.starts_with("option.")) {
            current->options[unescape_key(std::string_view(key).substr(7))] =
                static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        }
    }
    return saved;
}

// A mod's Riivolution discs with its choices (saved, else its config's, else its XML's defaults).
std::vector<RiivDisc> mod_discs(const ModLayout& layout, const SavedMod* saved, const std::string& game_id,
                                std::string* problem) {
    std::vector<RiivDisc> discs;
    unsigned other_game = 0;
    for (const fs::path& xml : layout.xmls) {
        std::string error;
        std::optional<RiivDisc> d = parse_riivolution(xml, &error);
        if (!d) {
            if (problem != nullptr && problem->empty()) *problem = utf8_name(xml) + ": " + error;
            continue;
        }
        if (!game_id.empty() && !valid_for_game(*d, game_id, -1, -1)) {
            ++other_game;
            continue;
        }
        apply_config(*d, layout.riivolution_dir, game_id.empty() ? std::string("RMGE01") : game_id);
        if (saved != nullptr) {
            for (Section& s : d->sections) {
                for (Option& o : s.options) {
                    const auto it = saved->options.find(option_key(s, o));
                    if (it != saved->options.end()) o.selected = std::min<unsigned>(it->second, static_cast<unsigned>(o.choices.size()));
                }
            }
        }
        discs.push_back(std::move(*d));
    }
    if (discs.empty() && other_game != 0u && problem != nullptr && problem->empty()) *problem = "FOR ANOTHER GAME";
    return discs;
}

}  // namespace

void set_mods_folder(const std::filesystem::path& folder, const std::filesystem::path& settings_file,
                     const std::string& game_id) {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.folder = folder;
    s.file = settings_file;
    s.game_id = game_id;
}

std::filesystem::path mods_folder() {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.folder;
}

std::vector<ModInfo> scan_mods(const std::string& requested_game_id) {
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
    for (const auto& entry : fs::directory_iterator(folder, error)) {
        if (entry.is_directory(error)) dirs.push_back(entry.path());
    }
    std::sort(dirs.begin(), dirs.end());
    for (const fs::path& dir : dirs) {
        ModInfo mod;
        mod.id = utf8_name(dir);
        mod.path = dir;
        const ModLayout layout = layout_of(dir);
        mod.riivolution = !layout.xmls.empty();
        mod.files = !layout.files_root.empty();
        mod.textures = !layout.textures.empty();
        const auto it = saved.find(mod.id);
        const SavedMod* s = it != saved.end() ? &it->second : nullptr;
        mod.enabled = s == nullptr || s->enabled;
        mod.order = s != nullptr && s->has_order ? s->order : 1000000u + static_cast<unsigned>(mods.size());
        if (mod.riivolution) {
            for (const RiivDisc& d : mod_discs(layout, s, game_id, &mod.problem)) {
                for (const Section& sec : d.sections) {
                    for (const Option& o : sec.options) {
                        ModOption option;
                        option.key = option_key(sec, o);
                        option.section = sec.name;
                        option.name = o.name;
                        for (const Choice& c : o.choices) option.choices.push_back(c.name);
                        option.selected = o.selected;
                        mod.options.push_back(std::move(option));
                    }
                }
            }
        }
        if (!mod.riivolution && !mod.files && !mod.textures) mod.problem = "NO RIIVOLUTION, FILES OR TEXTURES FOLDER";
        mods.push_back(std::move(mod));
    }
    std::stable_sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) { return a.order < b.order; });
    for (std::size_t i = 0; i < mods.size(); ++i) mods[i].order = static_cast<unsigned>(i + 1u);
    return mods;
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
    out << "; Nebula mods: load order (later wins), on/off and Riivolution choices. They apply at the next start.\n";
    for (const ModInfo& mod : mods) {
        out << '[' << escape_key(mod.id) << "]\n";
        out << "enabled=" << (mod.enabled ? 1 : 0) << '\n';
        out << "order=" << mod.order << '\n';
        for (const ModOption& o : mod.options) out << "option." << escape_key(o.key) << '=' << o.selected << '\n';
    }
    return io::write_text_file_atomically(file, out.str());
}

namespace {

struct DolTable {
    std::uint32_t address;
    std::uint32_t size;
};

// sdk/dol_data.c: the tables the native game reads from main.dol, by release.
std::span<const DolTable> dol_tables(const std::string& game_id) {
    static constexpr DolTable kKorea[] = {{0x8053DC20, 0x1E0}, {0x8053DE00, 0xD20}, {0x8053F5E0, 0x1CAC0}};
    static constexpr DolTable kOthers[] = {{0x8053C620, 0x1E0}, {0x8053C800, 0xD20}, {0x8053DFE0, 0x9AC0}};
    if (game_id.size() >= 4u && game_id[3] == 'K') return kKorea;
    return kOthers;
}

bool dol_used(std::uint32_t address, std::size_t size, const std::string& game_id) {
    for (const DolTable& t : dol_tables(game_id)) {
        if (address >= t.address && address - t.address < t.size && size <= t.size - (address - t.address)) return true;
    }
    return false;
}

// A console address's offset in main.dol's image.
std::optional<std::size_t> dol_offset_of(std::span<const std::uint8_t> dol, std::uint32_t address, std::size_t size) {
    if (dol.size() < 0x100u) return std::nullopt;
    const auto be = [&](std::size_t at) {
        return static_cast<std::uint32_t>(dol[at]) << 24u | static_cast<std::uint32_t>(dol[at + 1]) << 16u |
               static_cast<std::uint32_t>(dol[at + 2]) << 8u | static_cast<std::uint32_t>(dol[at + 3]);
    };
    for (unsigned s = 0; s < 18u; ++s) {
        const std::uint32_t offset = be(s * 4u);
        const std::uint32_t start = be(0x48 + s * 4u);
        const std::uint32_t length = be(0x90 + s * 4u);
        if (length == 0u || address < start || address - start >= length || size > length - (address - start)) continue;
        const std::size_t at = offset + (address - start);
        if (at + size > dol.size()) return std::nullopt;
        return at;
    }
    return std::nullopt;
}

}  // namespace

bool patch_dol(std::vector<std::uint8_t>& dol, std::uint32_t address, std::span<const std::uint8_t> value,
               std::span<const std::uint8_t> original) {
    const std::optional<std::size_t> at = dol_offset_of(dol, address, value.size());
    if (!at) return false;
    if (!original.empty()) {
        if (original.size() > value.size() && !dol_offset_of(dol, address, original.size())) return false;
        if (!std::equal(original.begin(), original.end(), dol.begin() + static_cast<std::ptrdiff_t>(*at))) return false;
    }
    std::copy(value.begin(), value.end(), dol.begin() + static_cast<std::ptrdiff_t>(*at));
    return true;
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

std::unique_ptr<ActiveMods> apply_mods(std::span<const std::uint8_t> partition_fst,
                                       std::span<const std::uint8_t> partition_dol, std::uint64_t dol_offset,
                                       const std::uint8_t* partition, std::uint64_t partition_size,
                                       const std::string& game_id, int revision, int disc_number) {
    std::vector<ModInfo> mods = scan_mods(game_id);
    {
        Session& s = session();
        const std::lock_guard<std::mutex> lock(s.mutex);
        s.mods = SessionMods{mods, {}, 0, 0};
    }
    if (std::none_of(mods.begin(), mods.end(), [](const ModInfo& m) { return m.enabled; })) return nullptr;
    auto active = std::make_unique<ActiveMods>();
    std::string error;
    std::ostringstream report;
    if (!active->disc.load(partition_fst, partition_size, &error)) {
        active->report = "mods: the disc's file table could not be read (" + error + "); no mod applied\n";
        return active;
    }
    // main.dol as a file Riivolution can patch ("main.dol" disc paths).
    DiscNode dol_node;
    dol_node.name = "main.dol";
    dol_node.size = partition_dol.size();
    dol_node.partition_offset = dol_offset;
    dol_node.pieces.push_back({DiscPiece::Source::Partition, 0, dol_node.size, dol_offset, {}});
    std::vector<std::pair<std::string, MemoryPatch>> memory;  // (mod, patch) in load order
    std::vector<Loader> memory_loaders;
    std::map<std::string, std::string> save_owner;

    for (const ModInfo& mod : mods) {
        if (!mod.enabled) continue;
        const ModLayout layout = layout_of(mod.path);
        unsigned changed = 0;
        unsigned missing = 0;
        std::vector<std::string> notes;
        const auto patch_file = [&](const Loader& loader, const FilePatch& f) {
            const std::optional<fs::path> host = loader.resolve(f.external);
            std::error_code ec;
            if (!host || !fs::is_regular_file(*host, ec)) {
                ++missing;
                return;
            }
            const std::uint64_t host_size = fs::file_size(*host, ec);
            DiscNode* node = nullptr;
            if (!f.disc.empty() && f.disc[0] == '/') {
                node = active->disc.find_path(std::string_view(f.disc).substr(1), f.create);
            } else if (same_name(f.disc, "main.dol")) {
                node = &dol_node;
            } else {
                node = active->disc.find_name(f.disc);
            }
            if (node == nullptr || node->directory) return;
            VirtualDisc::patch(*node, *host, host_size, f.offset & ~std::uint64_t{3}, f.file_offset, f.length, f.resize);
            ++changed;
        };
        const auto patch_folder = [&](auto&& self, const Loader& loader, const FolderPatch& folder, const std::string& disc_path,
                                      const std::string& external) -> void {
            const std::optional<fs::path> dir = loader.resolve(external);
            std::error_code ec;
            if (!dir || !fs::is_directory(*dir, ec)) {
                ++missing;
                return;
            }
            const auto join = [](std::string a, std::string_view b) {
                if (a.empty()) return std::string(b);
                if (b.empty()) return a;
                if (a.back() == '/') a.pop_back();
                while (b.starts_with('/')) b.remove_prefix(1);
                return a + "/" + std::string(b);
            };
            std::vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(*dir, ec)) entries.push_back(entry);
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.path() < b.path(); });
            for (const auto& entry : entries) {
                const std::string name = utf8_name(entry.path());
                if (entry.is_directory(ec)) {
                    if (folder.recursive) self(self, loader, folder, join(disc_path, name), join(external, name));
                } else {
                    FilePatch f;
                    f.disc = join(disc_path, name);
                    f.external = join(external, name);
                    f.resize = folder.resize;
                    f.create = folder.create;
                    f.length = folder.length;
                    patch_file(loader, f);
                }
            }
        };
        std::string problem;
        if (!layout.xmls.empty()) {
            const std::map<std::string, SavedMod> saved = read_saved(settings().file);
            const auto it = saved.find(mod.id);
            for (const RiivDisc& d : mod_discs(layout, it != saved.end() ? &it->second : nullptr, game_id, &problem)) {
                if (!valid_for_game(d, game_id, revision, disc_number)) {
                    notes.push_back(utf8_name(d.xml) + ": for another revision or disc");
                    continue;
                }
                for (const Patch& p : generate_patches(d, game_id)) {
                    const Loader loader = make_loader(layout.sd_root, d.xml, p.root);
                    for (const FilePatch& f : p.files) patch_file(loader, f);
                    for (const FolderPatch& f : p.folders) patch_folder(patch_folder, loader, f, f.disc, f.external);
                    for (const SavePatch& s : p.saves) {
                        if (!active->save_folder.empty()) {
                            notes.push_back("savegame: " + save_owner["save"] + "'s redirect kept (one per game)");
                            continue;
                        }
                        if (std::optional<fs::path> folder = loader.resolve(s.external)) {
                            active->save_folder = *folder;
                            active->save_clone = s.clone;
                            save_owner["save"] = mod.id;
                            notes.push_back("savegame: saves in " + folder->string());
                        }
                    }
                    for (const MemoryPatch& m : p.memory) {
                        memory.emplace_back(mod.id, m);
                        memory_loaders.push_back(loader);
                    }
                }
            }
        }
        if (!layout.files_root.empty()) {
            // The disc's layout: every file over the game's, or added.
            const Loader loader{layout.files_root, layout.files_root};
            patch_folder(patch_folder, loader, FolderPatch{"/", "/", true, true, true, 0}, "/", "/");
        }
        if (!layout.textures.empty()) active->texture_folders.push_back(layout.textures);
        active->files_changed += changed;
        active->active.push_back(mod.id);
        report << "mod " << mod.id << ": " << changed << " file patch(es)";
        if (missing != 0u) report << ", " << missing << " missing in the mod";
        if (!layout.textures.empty()) report << ", textures";
        if (!problem.empty()) report << " (" << problem << ")";
        report << '\n';
        for (const std::string& n : notes) report << "  " << n << '\n';
    }

    // Memory patches: the native game runs no PowerPC code; those over main.dol's data it reads are kept.
    std::vector<std::uint8_t> dol = dol_node.changed ? VirtualDisc::materialize(dol_node, partition, partition_size)
                                                     : std::vector<std::uint8_t>(partition_dol.begin(), partition_dol.end());
    bool dol_changed = dol_node.changed;
    for (std::size_t i = 0; i < memory.size(); ++i) {
        const auto& [mod, m] = memory[i];
        std::vector<std::uint8_t> value = m.value;
        if (!m.valuefile.empty()) {
            if (std::optional<fs::path> file = memory_loaders[i].resolve(m.valuefile)) {
                const std::string bytes = read_text(*file);
                value.assign(bytes.begin(), bytes.end());
            }
        }
        const std::uint32_t address = m.offset | 0x80000000u;
        char line[160];
        if (m.ocarina || m.search || m.offset == 0u || value.empty()) {
            std::snprintf(line, sizeof(line), "  %s: memory patch %s%s not supported (the native game runs no PowerPC code)",
                          mod.c_str(), m.ocarina ? "(ocarina) " : m.search ? "(search) " : "",
                          m.offset != 0u ? "" : "without an address");
            ++active->unsupported;
        } else if (dol_used(address, value.size(), game_id) && patch_dol(dol, address, value, m.original)) {
            std::snprintf(line, sizeof(line), "  %s: memory patch at %08X (%zu bytes) applied to main.dol's data", mod.c_str(),
                          address, value.size());
            dol_changed = true;
        } else {
            std::snprintf(line, sizeof(line), "  %s: memory patch at %08X (%zu bytes) not supported (code the native game "
                          "does not run)", mod.c_str(), address, value.size());
            ++active->unsupported;
        }
        report << line << '\n';
    }
    if (dol_changed) {
        // The native game reads its data tables from it: a main.dol without them would stop it at boot.
        bool tables = true;
        for (const DolTable& t : dol_tables(game_id)) tables = tables && dol_offset_of(dol, t.address, t.size).has_value();
        if (tables) {
            active->dol = std::move(dol);
        } else {
            report << "  main.dol from a mod lacks the game's data tables: the game's own kept\n";
        }
    }
    const std::uint64_t virtual_base = partition_size + 0x100000u;
    active->fst = active->disc.build(virtual_base);
    if (active->fst.empty()) {
        report << "mods: the changed disc does not fit the disc's 16 GiB reach; no mod applied\n";
        active->dol.clear();
        active->files_changed = 0;
    }
    active->report = report.str();
    {
        Session& s = session();
        const std::lock_guard<std::mutex> lock(s.mutex);
        s.mods.active = active->active;
        s.mods.files_changed = active->files_changed;
        s.mods.unsupported = active->unsupported;
    }
    return active;
}

}  // namespace galaxy::mods
