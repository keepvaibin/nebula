// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h). Riivolution's patch format follows Dolphin's implementation (DiscIO RiivolutionParser.cpp and
// RiivolutionPatcher.cpp, GPL-2.0-or-later): external paths, filename-only disc paths, option parameters, config
// defaults and patch order are Riivolution's as Dolphin reproduces them.

#include "galaxy/mods.h"
#include "galaxy/path_text.h"

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
struct Patch {
    std::string id, root;
    std::vector<FilePatch> files;
    std::vector<FolderPatch> folders;
    std::vector<SavePatch> saves;
    unsigned memory = 0;  // patches over the game's original code: not supported
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
                ++patch.memory;
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
    const std::string extension = lower(galaxy::path_text(file.extension()));
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
    for (auto it = fs::recursive_directory_iterator(textures, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const std::string stem = lower(utf8_name(it->path().stem()));
        if (stem.find("_mip") != std::string::npos) continue;
        const std::optional<std::uint64_t> hash = galaxy::textures::hash_of_pack_name(stem);
        if (!hash) continue;
        PackTexture texture;
        texture.hash = *hash;
        const std::string folder = utf8_name(it->path().parent_path().lexically_relative(textures));
        if (const auto named = galaxy::textures::category_of_pack_folder(galaxy::path_text(
                it->path().parent_path().lexically_relative(textures)))) {
            texture.folder_category = static_cast<std::uint8_t>(*named);
        }
        (void)folder;
        list->push_back(texture);
    }
    mod.pack_textures = std::move(list);
    // A resource pack's manifest, in the mod or one folder down.
    std::vector<fs::path> places{mod.path, textures};
    for (const auto& entry : fs::directory_iterator(mod.path, error)) {
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
    std::uint32_t categories_off = 0;
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

struct Session {
    std::mutex mutex;
    SessionMods mods;
};

void start_texture_sorting() {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.store.empty() && !s.file.empty()) {
        galaxy::textures::start_index(s.store, s.file.parent_path() / "texture_index.bin");
    }
}

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
        } else if (key == "textures_off") {
            current->categories_off = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
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

std::filesystem::path mods_folder() {
    Settings& s = settings();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.folder;
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
        mod.categories_off = s != nullptr ? s->categories_off : 0u;
        if (mod.textures) {
            read_pack(mod, layout.textures);
            start_texture_sorting();
            update_pack_categories(mod);
        }
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
                    mod.code_patches += p.memory;
                }
            }
        }
        mod.languages.assign(languages.begin(), languages.end());
        if (!layout.files_root.empty()) list_files(layout.files_root, std::string(), changed_paths[mod.id]);
        for (const fs::path& loose_file : layout.loose) changed_paths[mod.id].insert("name:" + lower(utf8_name(loose_file)));
        if (!layout.textures.empty()) list_textures(layout.textures, changed_textures[mod.id]);
        mod.changed_files = static_cast<unsigned>(changed_paths[mod.id].size());
        mod.changed_textures = static_cast<unsigned>(changed_textures[mod.id].size());
        if (mod.code_patches != 0u) {
            mod.enabled = false;
            mod.problem = "CHANGES THE GAME'S CODE: NOT SUPPORTED";
        }
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
    if (index >= mods.size() || mods[index].enabled == enabled || mods[index].code_patches != 0u) return;
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
        if (mod.categories_off != 0u) out << "textures_off=" << mod.categories_off << '\n';
        for (const ModOption& o : mod.options) out << "option." << escape_key(o.key) << '=' << o.selected << '\n';
    }
    return io::write_file_atomically(file, out.str());
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

std::vector<std::uint32_t> texture_folder_categories_off(std::span<const ModInfo> mods) {
    std::vector<std::uint32_t> out;
    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        if (!it->enabled || !it->textures) continue;
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
        std::optional<galaxy::textures::Category> category = galaxy::textures::category_of_hash(texture.hash);
        if (category) {
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
    if (sorted && game_textures == 0u && !mod.pack_textures->empty() && mod.problem.empty()) {
        mod.problem = "THESE TEXTURES ARE FOR ANOTHER GAME";
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

}  // namespace galaxy::mods
