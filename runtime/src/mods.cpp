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
    fs::path movies;
    std::vector<fs::path> loose;  // files placed by their names
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
    const std::string extension = lower(file.extension().string());
    for (const char* known : {".txt", ".md", ".pdf", ".url", ".htm", ".html", ".rtf", ".png", ".jpg", ".jpeg",
                              ".webp", ".gif", ".nfo", ".xml", ".ini", ".db", ".zip", ".7z", ".rar"}) {
        if (extension == known) return true;
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

// The texture names of a pack folder (tex1_..., mip levels left out, _arb dropped).
void list_textures(const fs::path& dir, std::set<std::string>& out) {
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        std::string stem = lower(utf8_name(it->path().stem()));
        if (!stem.starts_with("tex1_")) continue;
        if (const std::size_t mip = stem.rfind("_mip"); mip != std::string::npos) continue;
        if (stem.ends_with("_arb")) stem.resize(stem.size() - 4u);
        out.insert(stem);
    }
}

// tex1_ files within a few folders of `dir`.
bool has_pack_textures(const fs::path& dir) {
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (it.depth() > 4) {
            it.disable_recursion_pending();
            continue;
        }
        const std::string name = lower(utf8_name(it->path()));
        if (name.starts_with("tex1_") && (name.ends_with(".png") || name.ends_with(".dds"))) return true;
    }
    return false;
}

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
    } else if (has_disc_folder(mod)) {
        layout.files_root = mod;
    } else if (layout.xmls.empty()) {
        // The disc's folders one folder down (an archive's own top folder): that folder, when only one has them.
        std::vector<fs::path> with;
        for (const auto& entry : fs::directory_iterator(mod, error)) {
            if (entry.is_directory(error) && has_disc_folder(entry.path())) with.push_back(entry.path());
        }
        if (with.size() == 1u) layout.files_root = with.front();
    }
    if (std::optional<fs::path> textures = child_any_case(mod, "textures", true)) layout.textures = *textures;
    if (std::optional<fs::path> movies = child_any_case(mod, "movies", true)) layout.movies = *movies;
    if (layout.xmls.empty() && layout.files_root.empty()) {
        // Loose files beside nothing else: each goes where the disc has a file of its name.
        for (const auto& entry : fs::directory_iterator(mod, error)) {
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
    bool enabled = true;
    unsigned order = 0;
    bool has_order = false;
    bool own_text = true;
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
        if (!m->enabled || !changes_disc(*m)) continue;
        out += m->id + (m->own_text ? "" : " text=game") + '\n';
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
        } else if (key == "text") {
            current->own_text = value != "game";
        } else if (key.starts_with("option.")) {
            current->options[unescape_key(std::string_view(key).substr(7))] =
                static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        }
    }
    return saved;
}

bool dol_used(std::uint32_t address, std::size_t size, const std::string& game_id);
std::optional<std::size_t> dol_offset_of(std::span<const std::uint8_t> dol, std::uint32_t address, std::size_t size);
struct NativeCodePatch;
const NativeCodePatch* native_code_patch(const std::string& game_id, std::uint32_t address,
                                         std::span<const std::uint8_t> value);
// main.dol as this session's game has it (the scan checks a patch's `original` against it).
std::mutex s_dol_mutex;
std::vector<std::uint8_t> s_dol;

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

void set_game_dol(std::span<const std::uint8_t> dol) {
    const std::lock_guard<std::mutex> lock(s_dol_mutex);
    s_dol.assign(dol.begin(), dol.end());
}

std::string mods_game_id() {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.game_id;
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
        // Hidden folders: imports in progress (mod_import.cpp) and the system's own.
        if (entry.is_directory(error) && !utf8_name(entry.path()).starts_with('.')) dirs.push_back(entry.path());
    }
    std::sort(dirs.begin(), dirs.end());
    const std::string region_id = game_id.empty() ? std::string("RMGE01") : game_id;
    // What each mod changes, by its id: disc paths ("name:x.arc" for a file name alone) and texture names.
    std::map<std::string, std::set<std::string>> changed_paths;
    std::map<std::string, std::set<std::string>> changed_textures;
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
        const auto it = saved.find(mod.id);
        const SavedMod* s = it != saved.end() ? &it->second : nullptr;
        mod.enabled = s == nullptr || s->enabled;
        mod.own_text = s == nullptr || s->own_text;
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
                    for (const MemoryPatch& m : p.memory) {
                        const std::uint32_t address = m.offset | 0x80000000u;
                        {
                            // For another release (its original bytes differ from this disc's): skipped, as
                            // Riivolution does.
                            const std::lock_guard<std::mutex> lock(s_dol_mutex);
                            if (!m.original.empty() && !s_dol.empty()) {
                                const std::optional<std::size_t> at = dol_offset_of(s_dol, address, m.original.size());
                                if (!at || !std::equal(m.original.begin(), m.original.end(), s_dol.begin() + *at)) continue;
                            }
                        }
                        if (!m.ocarina && !m.search && !m.value.empty() && native_code_patch(region_id, address, m.value)) {
                            ++mod.native_patches;
                        } else if (m.ocarina || m.search ||
                                   !dol_used(address, std::max<std::size_t>(m.value.size(), 1u), region_id)) {
                            ++mod.code_patches;
                        }
                    }
                }
            }
        }
        mod.languages.assign(languages.begin(), languages.end());
        if (!layout.files_root.empty()) list_files(layout.files_root, std::string(), changed_paths[mod.id]);
        for (const fs::path& loose_file : layout.loose) changed_paths[mod.id].insert("name:" + lower(utf8_name(loose_file)));
        if (!layout.textures.empty()) list_textures(layout.textures, changed_textures[mod.id]);
        mod.changed_files = static_cast<unsigned>(changed_paths[mod.id].size());
        mod.changed_textures = static_cast<unsigned>(changed_textures[mod.id].size());
        if (!mod.riivolution && !mod.files && !mod.textures && !mod.movies && !mod.loose) {
            mod.problem = "NOTHING NEBULA CAN USE (NO RIIVOLUTION, GAME FILES, TEXTURES OR MOVIES)";
        }
        mods.push_back(std::move(mod));
    }
    // Disabled mods have no place: they follow the enabled ones by name.
    std::stable_sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) {
        if (a.enabled != b.enabled) return a.enabled;
        return a.enabled ? a.order < b.order : lower(a.id) < lower(b.id);
    });
    for (std::size_t i = 0; i < mods.size(); ++i) mods[i].order = static_cast<unsigned>(i + 1u);
    // Overlaps between enabled mods: the same disc file (a file name alone matches any path ending in it) or texture.
    const auto base = [](const std::string& path) {
        if (path.starts_with("name:")) return path.substr(5);
        const std::size_t slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1u);
    };
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (!mods[i].enabled) continue;
        for (std::size_t j = 0; j < mods.size(); ++j) {
            if (i == j || !mods[j].enabled) continue;
            ModOverlap overlap;
            overlap.other = mods[j].id;
            overlap.other_above = j < i;
            const std::set<std::string>& mine = changed_paths[mods[i].id];
            const std::set<std::string>& theirs = changed_paths[mods[j].id];
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
            const std::set<std::string>& my_textures = changed_textures[mods[i].id];
            const std::set<std::string>& their_textures = changed_textures[mods[j].id];
            for (const std::string& name : my_textures) overlap.textures += their_textures.count(name) != 0u ? 1u : 0u;
            if (overlap.files != 0u || overlap.textures != 0u) mods[i].overlaps.push_back(std::move(overlap));
        }
    }
    return mods;
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
        for (const ModOption& o : mod.options) out << "option." << escape_key(o.key) << '=' << o.selected << '\n';
    }
    return io::write_text_file_atomically(file, out.str());
}

namespace {

struct DolTable {
    std::uint32_t address;
    std::uint32_t size;
};

// Native equivalents of known patches to the console's code, matched by their exact bytes for a release (the disc's
// region): each sets the game's NEBULA_PATCH_* switch (nebula_game_api.h); id 0 has no native counterpart because
// Nebula does the job itself. Covers Kaizo Mario Galaxy Rebalanced 2.1 (NTSC and the shared UEJ patches) and Super
// Lillie Galaxy's crash debugger.
struct NativeCodePatch {
    char region;
    std::uint32_t address;
    const char* value;
    std::uint32_t id;
    std::uint32_t parameter;
    const char* what;
};

constexpr NativeCodePatch kNativeCodePatches[] = {
    {'E', 0x801DD690, "38C00080", kPatchFlipPanelObserverGroup, 0x80, "flip panel observer groups of 128"},
    {'E', 0x801DCE48, "38C00080", kPatchFlipPanelGroup, 0x80, "flip panel groups of 128"},
    {'E', 0x80153D84, "39000001", kPatchWaterBazookaShotFlag, 1, "water bazooka shots' flag"},
    {'E', 0x80001800, "C4BB8000C03E00103C608000C0031800EC00082AD01E0010809E008C4E800020", kPatchKameckBarrierOffsetY,
     0xC4BB8000u, "Kamek's barrier 1500 lower"},
    {'E', 0x800382B8, "4BFC954D", kPatchKameckBarrierOffsetY, 0xC4BB8000u, "Kamek's barrier 1500 lower"},
    {'E', 0x8007679C, "60000000", kPatchSkeletalFishBabyNoBubble, 1, "no air bubble from hit skeletal fish"},
    {'E', 0x80054490, "4836F0A9", kPatchDodoryuShellOnly, 1, "Dodoryu hurt by shells only"},
    {'E', 0x800564C4, "4836D075", kPatchDodoryuShellOnly, 1, "Dodoryu hurt by shells only"},
    {'E', 0x803F82D8, "4BC0A058", kPatchMissStarPieces, 0xFFFFFC19u, "every star bit lost after a miss"},
    {'E', 0x80002330, "3860FC19483ADFB980010034483F5FA0", kPatchMissStarPieces, 0xFFFFFC19u,
     "every star bit lost after a miss"},
    {'E', 0x803F6298, "38600000", kPatchNoHiddenStarTable, 1, "hidden stars without their table"},
    {'E', 0x803F5D40, "386000004E800020", kPatchNoSuddenDeathDodoryu, 1, "no sudden-death Dodoryu"},
    {'E', 0x800D02C0, "38600000", kPatchBegomanFireProof, 1, "Begomans survive fire ground"},
    {'E', 0x800D5DD8, "38600000", kPatchBegomanFireProof, 1, "Begomans survive fire ground"},
    {'E', 0x800D7D50, "38600000", kPatchBegomanFireProof, 1, "Begomans survive fire ground"},
    {'E', 0x80275440, "60000000", kPatchMessengerAlwaysEx, 1, "the 1-up messenger's larger supply"},
    {'E', 0x80275458, "38800063", kPatchMessengerExOneUps, 99, "99 1-ups from the messenger"},
    {'E', 0x802750F0, "2C1F0063", kPatchMessengerExOneUps, 99, "99 1-ups from the messenger"},
    {'E', 0x802750FC, "38800063", kPatchMessengerExOneUps, 99, "99 1-ups from the messenger"},
    {'E', 0x8039AA2C, "60000000", 0, 0, "the console's crash debugger (Nebula reports crashes itself)"},
    {'E', 0x8039AAF0, "60000000", 0, 0, "the console's crash debugger (Nebula reports crashes itself)"},
    {'E', 0x804A432C, "60000000", 0, 0, "the console's crash debugger (Nebula reports crashes itself)"},
};

const NativeCodePatch* native_code_patch(const std::string& game_id, std::uint32_t address,
                                         std::span<const std::uint8_t> value) {
    if (game_id.size() < 4u) return nullptr;
    for (const NativeCodePatch& patch : kNativeCodePatches) {
        if (patch.region != game_id[3] || patch.address != (address | 0x80000000u)) continue;
        const std::vector<std::uint8_t> bytes = hex_bytes(patch.value);
        if (bytes.size() == value.size() && std::equal(bytes.begin(), bytes.end(), value.begin())) return &patch;
    }
    return nullptr;
}

// sdk/dol_data.c: the tables the native game reads from main.dol, by release (the USA release's comet schedules and
// event flags too: __NebulaDolTable).
std::span<const DolTable> dol_tables(const std::string& game_id) {
    static constexpr DolTable kKorea[] = {{0x8053DC20, 0x1E0}, {0x8053DE00, 0xD20}, {0x8053F5E0, 0x1CAC0}};
    static constexpr DolTable kOthers[] = {{0x8053C620, 0x1E0}, {0x8053C800, 0xD20}, {0x8053DFE0, 0x9AC0}};
    static constexpr DolTable kUsa[] = {{0x8053C620, 0x1E0}, {0x8053C800, 0xD20}, {0x8053DFE0, 0x9AC0},
                                        {0x8053AEF0, 0x3F0}, {0x8053B320, 0xEB0}};
    if (game_id.size() >= 4u && game_id[3] == 'K') return kKorea;
    if (game_id.size() >= 4u && game_id[3] == 'E') return kUsa;
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

bool changes_disc(const ModInfo& mod) noexcept {
    return mod.riivolution || mod.files || mod.loose;
}

std::vector<std::filesystem::path> texture_folders(std::span<const ModInfo> mods) {
    std::vector<std::filesystem::path> out;
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        if (!it->enabled || !it->textures) continue;
        const ModLayout layout = layout_of(it->path);
        if (!layout.textures.empty()) out.push_back(layout.textures);
    }
    return out;
}

std::vector<std::filesystem::path> movie_folders(std::span<const ModInfo> mods) {
    std::vector<std::filesystem::path> out;
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        if (!it->enabled || !it->movies) continue;
        const ModLayout layout = layout_of(it->path);
        if (!layout.movies.empty()) out.push_back(layout.movies);
    }
    return out;
}

std::string language_folder(const std::string& game_id, unsigned language) {
    if (game_id.size() < 4u) return {};
    switch (game_id[3]) {
    case 'E':
        return language == 3u ? "UsFrench" : language == 4u ? "UsSpanish" : "UsEnglish";
    case 'P':
        switch (language) {
        case 2u: return "EuGerman";
        case 3u: return "EuFrench";
        case 4u: return "EuSpanish";
        case 5u: return "EuItalian";
        case 6u: return "EuDutch";
        default: return "EuEnglish";
        }
    case 'J': return "JpJapanese";
    case 'K': return "KrKorean";
    default: return {};  // no other release's folders are known
    }
}

std::vector<std::string> language_folders(std::span<const std::uint8_t> fst) {
    std::vector<std::string> folders;
    const auto u32 = [&fst](std::size_t at) {
        return static_cast<std::uint32_t>(fst[at]) << 24u | static_cast<std::uint32_t>(fst[at + 1u]) << 16u |
               static_cast<std::uint32_t>(fst[at + 2u]) << 8u | static_cast<std::uint32_t>(fst[at + 3u]);
    };
    if (fst.size() < 12u) return folders;
    const std::uint32_t count = u32(8u);
    if (count == 0u || static_cast<std::uint64_t>(count) * 12u > fst.size()) return folders;
    const std::size_t strings = static_cast<std::size_t>(count) * 12u;
    // A directory at the root is followed by its nodes, up to the index it gives.
    for (std::uint32_t index = 1u; index < count;) {
        const std::size_t entry = static_cast<std::size_t>(index) * 12u;
        if (fst[entry] == 0u) {
            ++index;
            continue;
        }
        const std::size_t name_at = strings + (u32(entry) & 0xFFFFFFu);
        std::string name;
        for (std::size_t at = name_at; at < fst.size() && fst[at] != 0u; ++at) name.push_back(static_cast<char>(fst[at]));
        static constexpr std::string_view kRegions[] = {"Us", "Eu", "Jp", "Kr", "Cn", "Tw"};
        for (const std::string_view region : kRegions) {
            const std::string_view rest = std::string_view(name).substr(std::min(name.size(), region.size()));
            if (name.starts_with(region) && !rest.empty() && std::isupper(static_cast<unsigned char>(rest[0])) &&
                std::all_of(rest.begin(), rest.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; })) {
                folders.push_back(name);
                break;
            }
        }
        const std::uint32_t next = u32(entry + 8u);
        index = next > index ? next : index + 1u;
    }
    return folders;
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
                                       const std::string& game_id, int revision, int disc_number,
                                       const std::string& language_folder) {
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

    // From the bottom of the list up: the mod on top changes a file last and wins it.
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        const ModInfo& mod = *it;
        if (!mod.enabled) continue;
        const ModLayout layout = layout_of(mod.path);
        unsigned changed = 0;
        unsigned missing = 0;
        std::vector<std::string> notes;
        // Whole files this mod placed at a disc path (the text-language fallback copies them).
        struct Placed {
            std::string disc;
            fs::path host;
            std::uint64_t size = 0;
            bool create = false;
        };
        std::vector<Placed> placed;
        const auto patch_file = [&](const Loader& loader, const FilePatch& f) {
            const std::optional<fs::path> host = loader.resolve(f.external);
            std::error_code ec;
            if (!host || !fs::is_regular_file(*host, ec)) {
                ++missing;
                return;
            }
            const std::uint64_t host_size = fs::file_size(*host, ec);
            const bool whole = (f.offset & ~std::uint64_t{3}) == 0u && f.file_offset == 0u && f.length == 0u && f.resize;
            std::vector<std::pair<DiscNode*, std::string>> nodes;
            if (!f.disc.empty() && f.disc[0] == '/') {
                nodes.emplace_back(active->disc.find_path(std::string_view(f.disc).substr(1), f.create), f.disc);
            } else if (same_name(f.disc, "main.dol")) {
                nodes.emplace_back(&dol_node, std::string());
            } else {
                // A file name alone: every disc file of that name, as Dolphin patches.
                nodes = active->disc.find_names(f.disc);
            }
            for (const auto& [node, path] : nodes) {
                if (node == nullptr || node->directory) continue;
                VirtualDisc::patch(*node, *host, host_size, f.offset & ~std::uint64_t{3}, f.file_offset, f.length,
                                   f.resize);
                ++changed;
                if (whole && !path.empty()) placed.push_back({path, *host, host_size, f.create});
            }
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
            const auto saved_it = saved.find(mod.id);
            for (const RiivDisc& d : mod_discs(layout, saved_it != saved.end() ? &saved_it->second : nullptr, game_id, &problem)) {
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
                            // One save folder per game: the mod higher in the list has it.
                            notes.push_back("savegame: replaces " + save_owner["save"] +
                                            "'s save folder (one per game; the higher mod's is used)");
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
        unsigned loose_unplaced = 0;
        for (const fs::path& file : layout.loose) {
            // Loose files: every disc file of the name (Riivolution's file name alone).
            std::error_code ec;
            const std::string name = utf8_name(file);
            const auto nodes = active->disc.find_names(name);
            if (nodes.empty()) {
                ++loose_unplaced;
                notes.push_back(name + ": no file of that name on the disc (not used)");
                continue;
            }
            const std::uint64_t size = fs::file_size(file, ec);
            for (const auto& [node, path] : nodes) {
                VirtualDisc::patch(*node, file, size, 0u, 0u, 0u, true);
                ++changed;
                placed.push_back({path, file, size, false});
            }
            if (nodes.size() > 1u) notes.push_back(name + ": replaces " + std::to_string(nodes.size()) + " disc files of that name");
        }
        // Text in the language played: a mod with text for other languages only shows its own (the same language
        // of another region first, else English, else what it has) unless its TEXT choice keeps the game's.
        if (!language_folder.empty() && mod.own_text) {
            std::set<std::string> covered;
            for (const Placed& p : placed) {
                if (const std::string language = language_of(p.disc); !language.empty()) covered.insert(language);
            }
            if (!covered.empty() && covered.count(language_folder) == 0u) {
                const std::string twin = language_folder.starts_with("Us") ? "Eu" + language_folder.substr(2)
                                       : language_folder.starts_with("Eu") ? "Us" + language_folder.substr(2)
                                                                           : std::string();
                std::string source;
                for (const std::string& candidate : {twin, std::string("UsEnglish"), std::string("EuEnglish")}) {
                    if (!candidate.empty() && covered.count(candidate) != 0u) {
                        source = candidate;
                        break;
                    }
                }
                if (source.empty()) source = *covered.begin();
                unsigned copied = 0;
                bool nebula_text = false;
                for (const Placed& p : std::vector<Placed>(placed)) {
                    if (language_of(p.disc) != source) continue;
                    const std::string target = "/" + language_folder + p.disc.substr(source.size() + 1u);
                    DiscNode* node = active->disc.find_path(std::string_view(target).substr(1), p.create);
                    if (node == nullptr || node->directory) continue;
                    VirtualDisc::patch(*node, p.host, p.size, 0u, 0u, 0u, true);
                    nebula_text |= same_name(fs::path(p.disc).filename().string(), "NebulaText.txt");
                    ++copied;
                }
                // Nebula's own text follows the mod's language (NebulaText reads "language = ..." from the played
                // folder) unless the mod brings its own NebulaText.txt.
                if (copied != 0u && !nebula_text) {
                    const fs::path note = fs::temp_directory_path() / "Nebula" / ("NebulaText-" + source + ".txt");
                    std::error_code ec;
                    fs::create_directories(note.parent_path(), ec);
                    const std::string text = "language = " + source + "\n";
                    if (io::write_text_file_atomically(note, text)) {
                        const std::string target = "/" + language_folder + "/MessageData/NebulaText.txt";
                        if (DiscNode* node = active->disc.find_path(std::string_view(target).substr(1), true);
                            node != nullptr && !node->directory) {
                            VirtualDisc::patch(*node, note, text.size(), 0u, 0u, 0u, true);
                        }
                    }
                }
                if (copied != 0u) {
                    notes.push_back("text: its " + source + " files stand in for " + language_folder + " (" +
                                    std::to_string(copied) + "; it has no " + language_folder + " text)");
                }
            }
        }
        if (!layout.textures.empty()) active->texture_folders.push_back(layout.textures);
        if (!layout.movies.empty()) active->movie_folders.push_back(layout.movies);
        active->files_changed += changed;
        active->active.push_back(mod.id);
        report << "mod " << mod.id << ": " << changed << " file patch(es)";
        if (missing != 0u) report << ", " << missing << " missing in the mod";
        if (!layout.textures.empty()) report << ", textures";
        if (!layout.movies.empty()) report << ", movies";
        if (!layout.loose.empty()) report << ", " << (layout.loose.size() - loose_unplaced) << " loose file(s) placed by name";
        if (!problem.empty()) report << " (" << problem << ")";
        report << '\n';
        for (const std::string& n : notes) report << "  " << n << '\n';
    }

    // Memory patches: the native game runs no PowerPC code; those over main.dol's data it reads are kept.
    std::vector<std::uint8_t> dol = dol_node.changed ? VirtualDisc::materialize(dol_node, partition, partition_size)
                                                     : std::vector<std::uint8_t>(partition_dol.begin(), partition_dol.end());
    bool dol_changed = dol_node.changed;
    {
        const std::lock_guard<std::mutex> lock(s_dol_mutex);
        s_dol.assign(partition_dol.begin(), partition_dol.end());
    }
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
        char line[200];
        // `original` names the bytes a patch expects (Riivolution skips it elsewhere): written for another release.
        const std::optional<std::size_t> at =
            m.original.empty() ? std::nullopt : dol_offset_of(dol, address, m.original.size());
        const bool other_release =
            !m.original.empty() && (!at || !std::equal(m.original.begin(), m.original.end(), dol.begin() + *at));
        if (other_release) {
            std::snprintf(line, sizeof(line), "  %s: memory patch at %08X skipped (for another release: its "
                          "original bytes differ)", mod.c_str(), address);
        } else if (const NativeCodePatch* native = value.empty() ? nullptr : native_code_patch(game_id, address, value)) {
            if (native->id != 0u) active->code_patches[native->id] = native->parameter;
            std::snprintf(line, sizeof(line), "  %s: memory patch at %08X: %s%s", mod.c_str(), address, native->what,
                          native->id != 0u ? " (native)" : " (not needed natively)");
        } else if (m.ocarina || m.search || m.offset == 0u || value.empty()) {
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
        // The three the game stops without (the comet schedules and event flags fall back to the game's own).
        const std::span<const DolTable> all = dol_tables(game_id);
        for (const DolTable& t : all.first(std::min<std::size_t>(3u, all.size()))) {
            tables = tables && dol_offset_of(dol, t.address, t.size).has_value();
        }
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
