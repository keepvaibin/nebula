// SPDX-License-Identifier: GPL-3.0-only
// dolphin_pack.cpp: Dolphin texture-pack names, and their one-time conversion to the game's names (dolphin_pack.h).

#include "galaxy/dolphin_pack.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace galaxy::textures {

namespace fs = std::filesystem;

namespace {

bool parse_hex64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.size() != 16u) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        value <<= 4u;
        if (c >= '0' && c <= '9') value |= static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint64_t>(c - 'A' + 10);
        else return false;
    }
    out = value;
    return true;
}

bool parse_uint(std::string_view text, std::uint32_t limit, std::uint32_t& out) noexcept {
    if (text.empty()) return false;
    std::uint32_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint32_t>(c - '0');
        if (value > (limit - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    out = value;
    return true;
}

bool known_format(std::uint32_t format) noexcept {
    return format <= 6u || format == 8u || format == 9u || format == 10u || format == 14u;
}

std::string utf8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

fs::path from_utf8(std::string_view text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

// A Dolphin pack's game folder: the disc id (RMGE01, RMGP01, ...) or its region-free prefix.
bool game_folder_name(std::string_view lower) noexcept {
    return lower == "rmg" || (lower.size() == 6u && lower.substr(0, 3u) == "rmg");
}

constexpr std::string_view kKept = ".dolphin";

// A tex1_ image (any case) outside .dolphin/.
template <typename Visit>
void each_dolphin_file(const fs::path& folder, Visit&& visit) {
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        std::error_code kind;
        if (it->is_directory(kind)) {
            if (utf8(it->path().filename()) == kKept) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(kind)) continue;
        const std::string extension = lower_ascii(utf8(it->path().extension()));
        if (extension != ".png" && extension != ".dds") continue;
        if (!is_dolphin_pack_name(utf8(it->path().stem()))) continue;
        if (!visit(it->path(), extension)) return;
    }
}

}  // namespace

bool dolphin_format_palettized(std::uint8_t format) noexcept {
    return format == static_cast<std::uint8_t>(DolphinFormat::Palette4) ||
           format == static_cast<std::uint8_t>(DolphinFormat::Palette8) ||
           format == static_cast<std::uint8_t>(DolphinFormat::Palette14);
}

std::optional<DolphinName> parse_dolphin_name(std::string_view stem) noexcept {
    if (!is_dolphin_pack_name(stem)) return std::nullopt;
    std::string_view rest = stem.substr(5u);
    std::array<std::string_view, 8> parts{};
    std::size_t count = 0;
    while (!rest.empty()) {
        if (count == parts.size()) return std::nullopt;
        const std::size_t cut = rest.find('_');
        parts[count++] = rest.substr(0, cut);
        if (cut == std::string_view::npos) break;
        rest = rest.substr(cut + 1u);
    }
    if (count == 0u) return std::nullopt;
    DolphinName name{};
    std::size_t at = 0;
    {
        const std::string_view size = parts[at++];
        const std::size_t x = size.find('x');
        if (x == std::string_view::npos || !parse_uint(size.substr(0, x), UINT32_MAX, name.width) ||
            !parse_uint(size.substr(x + 1u), UINT32_MAX, name.height) || name.width == 0u || name.height == 0u) {
            return std::nullopt;
        }
    }
    if (at < count && parts[at] == "m") {
        name.mipmapped = true;
        ++at;
    }
    if (at >= count || !parse_hex64(parts[at++], name.hash)) return std::nullopt;
    if (at < count && parts[at] == "$") {
        name.any_palette = true;
        ++at;
    } else if (at < count && parts[at].size() == 16u) {
        if (!parse_hex64(parts[at++], name.palette)) return std::nullopt;
    }
    if (at >= count) return std::nullopt;
    std::uint32_t format = 0;
    if (const std::string_view token = parts[at++]; !parse_uint(token, 255u, format)) {
        std::size_t digits = 0;
        while (digits < token.size() && token[digits] >= '0' && token[digits] <= '9') ++digits;
        bool letters = digits != 0u && digits < token.size();
        for (std::size_t i = digits; letters && i < token.size(); ++i) {
            const char c = token[i];
            letters = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }
        if (!letters || !parse_uint(token.substr(0, digits), 255u, format)) return std::nullopt;
    }
    if (!known_format(format)) return std::nullopt;
    name.format = static_cast<std::uint8_t>(format);
    if (!dolphin_format_palettized(name.format) && (name.any_palette || name.palette != 0u)) return std::nullopt;
    if (at < count && parts[at] == "arb") {
        name.arbitrary = true;
        ++at;
    }
    if (at < count) {
        const std::string_view tail = parts[at++];
        std::uint32_t level = 0;
        if (tail.size() <= 3u || tail.substr(0, 3u) != "mip" || !parse_uint(tail.substr(3u), 31u, level) || level == 0u) {
            return std::nullopt;
        }
        name.level = static_cast<std::uint8_t>(level);
    }
    if (at != count) return std::nullopt;
    return name;
}

std::string dolphin_name(const DolphinName& name) {
    const auto hex16 = [](std::uint64_t value) {
        char text[17]{};
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
        return std::string(text);
    };
    std::string out = "tex1_" + std::to_string(name.width) + "x" + std::to_string(name.height);
    if (name.mipmapped) out += "_m";
    out += "_" + hex16(name.hash);
    if (dolphin_format_palettized(name.format)) out += name.any_palette ? std::string("_$") : "_" + hex16(name.palette);
    out += "_" + std::to_string(name.format);
    if (name.arbitrary) out += "_arb";
    if (name.level != 0u) out += "_mip" + std::to_string(name.level);
    return out;
}

bool dolphin_matches(const DolphinName& name, const TextureName& texture) noexcept {
    return texture.image == name.hash && texture.width == name.width && texture.height == name.height &&
           texture.format == name.format &&
           (!dolphin_format_palettized(name.format) || name.any_palette || texture.palette == name.palette);
}

std::string converted_file(const TextureName& texture, bool arbitrary, std::uint8_t level, std::string_view extension) {
    std::string out = texture.name;
    if (arbitrary) out += "_arb";
    if (level != 0u) out += "_mip" + std::to_string(level);
    out += extension;
    return out;
}

bool has_dolphin_files(const fs::path& folder) {
    bool found = false;
    each_dolphin_file(folder, [&found](const fs::path&, const std::string&) {
        found = true;
        return false;
    });
    return found;
}

DolphinConversion convert_dolphin_pack(const fs::path& folder, std::span<const TextureName> names) {
    DolphinConversion out;
    struct Source {
        fs::path path;
        std::string relative;  // in the pack, '/' separated
        std::string extension;
        std::optional<DolphinName> name;
    };
    std::vector<Source> sources;
    each_dolphin_file(folder, [&](const fs::path& path, const std::string& extension) {
        sources.push_back({path, utf8(path.lexically_relative(folder)), extension, parse_dolphin_name(utf8(path.stem()))});
        return true;
    });
    if (sources.empty()) return out;
    // A palette named before "any palette", a DDS before a PNG, then by path: the first file to give a name keeps it.
    std::sort(sources.begin(), sources.end(), [](const Source& a, const Source& b) {
        const bool a_any = a.name && a.name->any_palette;
        const bool b_any = b.name && b.name->any_palette;
        if (a_any != b_any) return b_any;
        if (a.extension != b.extension) return a.extension == ".dds";
        return a.relative < b.relative;
    });
    // The game's textures by image.
    std::vector<const TextureName*> by_image;
    by_image.reserve(names.size());
    for (const TextureName& texture : names) by_image.push_back(&texture);
    std::sort(by_image.begin(), by_image.end(), [](const TextureName* a, const TextureName* b) { return a->image < b->image; });

    const fs::path kept = folder / from_utf8(kKept);
    std::vector<fs::path> emptied;
    for (const Source& source : sources) {
        std::error_code error;
        // The game folder that held it: <folder>/<..>/RMGE01 within three folders, else the pack's own root.
        fs::path base = folder;
        {
            fs::path walked = folder;
            std::size_t depth = 0;
            for (const fs::path& part : from_utf8(source.relative).parent_path()) {
                walked /= part;
                if (++depth > 3u) break;
                if (game_folder_name(lower_ascii(utf8(part)))) {
                    base = walked;
                    break;
                }
            }
        }
        const fs::path moved = kept / from_utf8(source.relative);
        fs::create_directories(moved.parent_path(), error);
        fs::rename(source.path, moved, error);
        if (error) {
            out.failed.push_back(source.relative);
            continue;
        }
        ++out.files;
        emptied.push_back(source.path.parent_path());
        bool matched = false;
        if (source.name) {
            const DolphinName& name = *source.name;
            auto it = std::lower_bound(by_image.begin(), by_image.end(), name.hash,
                                       [](const TextureName* row, std::uint64_t image) { return row->image < image; });
            for (; it != by_image.end() && (*it)->image == name.hash; ++it) {
                const TextureName& texture = **it;
                if (!dolphin_matches(name, texture)) continue;
                matched = true;
                const fs::path target = base / from_utf8(converted_file(texture, name.arbitrary, name.level, source.extension));
                if (fs::exists(target, error)) {
                    ++out.taken;
                    continue;
                }
                fs::create_directories(target.parent_path(), error);
                error.clear();
                fs::create_hard_link(moved, target, error);
                if (error) {
                    error.clear();
                    fs::copy_file(moved, target, error);
                }
                if (error) {
                    out.failed.push_back(source.relative + " -> " + utf8(target.lexically_relative(folder)));
                } else {
                    ++out.textures;
                }
            }
        }
        if (!matched) out.unmatched.push_back(source.relative);
    }
    // The folders the files left, deepest first, while empty.
    std::sort(emptied.begin(), emptied.end());
    emptied.erase(std::unique(emptied.begin(), emptied.end()), emptied.end());
    std::sort(emptied.begin(), emptied.end(), [](const fs::path& a, const fs::path& b) {
        return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
    });
    const auto inside = [&folder](const fs::path& dir) {
        const fs::path relative = dir.lexically_relative(folder);
        return !relative.empty() && relative != "." && *relative.begin() != "..";
    };
    for (fs::path dir : emptied) {
        std::error_code error;
        while (inside(dir) && fs::is_empty(dir, error) && !error && fs::remove(dir, error) && !error) dir = dir.parent_path();
    }
    if (!out.unmatched.empty()) {
        std::ofstream list(kept / "unmatched.txt", std::ios::binary | std::ios::trunc);
        list << "# These files stand for none of the game's textures (another game, release or edit of it).\n";
        for (const std::string& file : out.unmatched) list << file << '\n';
    }
    return out;
}

}  // namespace galaxy::textures
