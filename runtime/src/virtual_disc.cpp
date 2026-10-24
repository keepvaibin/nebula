// SPDX-License-Identifier: GPL-3.0-only
// The game's disc with mods' files (virtual_disc.h). Patching follows Dolphin's Riivolution patcher (DiscIO
// RiivolutionPatcher.cpp, GPL-2.0-or-later): a patch splits the pieces it overlaps and takes their place.

#include "galaxy/virtual_disc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <share.h>
#endif

namespace galaxy::mods {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) << 24u | static_cast<std::uint32_t>(p[1]) << 16u |
           static_cast<std::uint32_t>(p[2]) << 8u | static_cast<std::uint32_t>(p[3]);
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24u));
    out.push_back(static_cast<std::uint8_t>(v >> 16u));
    out.push_back(static_cast<std::uint8_t>(v >> 8u));
    out.push_back(static_cast<std::uint8_t>(v));
}

bool same_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// The disc's files are 32 KiB aligned; changed files are placed the same way.
constexpr std::uint64_t kPlaceAlign = 0x8000;
// File-table positions are words: the disc's reach.
constexpr std::uint64_t kDiscReach = 0x400000000ull;

void split_piece(DiscPiece& before, DiscPiece& after, std::uint64_t at) {
    const std::uint64_t end = before.offset + before.size;
    before.size = at - before.offset;
    after.offset = at;
    after.size = end - at;
    if (after.source != DiscPiece::Source::Zero) after.source_offset += before.size;
}

FILE* open_host(const std::filesystem::path& path) {
#if defined(_WIN32)
    // Shared: the player can still open or replace a mod's files while the game reads them.
    return _wfsopen(path.wstring().c_str(), L"rb", _SH_DENYNO);
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

bool seek_host(FILE* f, std::uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(offset), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

// Host files a disc read takes pieces from, kept open (the game streams movies and music in pieces).
struct HostFiles {
    std::mutex mutex;
    std::vector<std::pair<std::filesystem::path, FILE*>> open;  // most recent last

    bool read(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t size, std::uint8_t* dst) {
        const std::lock_guard<std::mutex> lock(mutex);
        auto it = std::find_if(open.begin(), open.end(), [&](const auto& e) { return e.first == path; });
        FILE* f = nullptr;
        if (it != open.end()) {
            f = it->second;
            std::rotate(it, it + 1, open.end());
        } else {
            f = open_host(path);
            if (f == nullptr) return false;
            if (open.size() >= 16u) {
                std::fclose(open.front().second);
                open.erase(open.begin());
            }
            open.emplace_back(path, f);
        }
        if (!seek_host(f, offset)) return false;
        const std::size_t got = std::fread(dst, 1, static_cast<std::size_t>(size), f);
        if (got < size) std::memset(dst + got, 0, static_cast<std::size_t>(size - got));
        return true;
    }
};

HostFiles& host_files() {
    static HostFiles files;
    return files;
}

}  // namespace

bool VirtualDisc::load(std::span<const std::uint8_t> fst, std::uint64_t partition_size, std::string* error) {
    partition_size_ = partition_size;
    root_ = DiscNode{};
    root_.directory = true;
    if (fst.size() < 12u) {
        if (error != nullptr) *error = "the file table is too short";
        return false;
    }
    const std::uint32_t count = be32(fst.data() + 8);
    if (count == 0u || static_cast<std::uint64_t>(count) * 12u > fst.size()) {
        if (error != nullptr) *error = "the file table is truncated";
        return false;
    }
    const std::uint8_t* strings = fst.data() + static_cast<std::size_t>(count) * 12u;
    const std::size_t strings_size = fst.size() - static_cast<std::size_t>(count) * 12u;
    const auto name_of = [&](std::uint32_t i, std::string& out) {
        const std::uint32_t at = be32(fst.data() + i * 12u) & 0x00FFFFFFu;
        if (at >= strings_size) return false;
        const auto* begin = reinterpret_cast<const char*>(strings + at);
        out.assign(begin, strnlen(begin, strings_size - at));
        return true;
    };
    // Entries [begin, end) into `dir`, recursively.
    std::string bad;
    const auto fill = [&](auto&& self, DiscNode& dir, std::uint32_t begin, std::uint32_t end, int depth) -> bool {
        if (depth > 64) {
            bad = "directories nested too deeply";
            return false;
        }
        for (std::uint32_t i = begin; i < end;) {
            const std::uint8_t* e = fst.data() + i * 12u;
            DiscNode node;
            if (!name_of(i, node.name)) {
                bad = "a name past the string table";
                return false;
            }
            if (e[0] != 0u) {
                const std::uint32_t next = be32(e + 8);
                if (next <= i || next > end) {
                    bad = "a directory's end out of order";
                    return false;
                }
                node.directory = true;
                if (!self(self, node, i + 1u, next, depth + 1)) return false;
                dir.children.push_back(std::move(node));
                i = next;
            } else {
                node.partition_offset = static_cast<std::uint64_t>(be32(e + 4)) << 2u;
                node.size = be32(e + 8);
                if (node.size != 0u) {
                    node.pieces.push_back({DiscPiece::Source::Partition, 0, node.size, node.partition_offset, {}});
                }
                dir.children.push_back(std::move(node));
                ++i;
            }
        }
        return true;
    };
    if (!fill(fill, root_, 1u, count, 0)) {
        if (error != nullptr) *error = "the file table is malformed: " + bad;
        return false;
    }
    return true;
}

DiscNode* VirtualDisc::find_path(std::string_view path, bool create) {
    DiscNode* dir = &root_;
    while (!path.empty()) {
        while (!path.empty() && path.front() == '/') path.remove_prefix(1);
        if (path.empty()) return dir;
        const std::size_t slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        const bool last = slash == std::string_view::npos || path.find_first_not_of('/', slash) == std::string_view::npos;
        auto it = std::find_if(dir->children.begin(), dir->children.end(),
                               [&](const DiscNode& n) { return same_name(n.name, part); });
        if (it == dir->children.end()) {
            if (!create) return nullptr;
            DiscNode node;
            node.name = std::string(part);
            node.directory = !last;
            node.changed = !node.directory;
            dir->children.push_back(std::move(node));
            it = dir->children.end() - 1;
        }
        if (last) return &*it;
        if (!it->directory) return nullptr;
        dir = &*it;
        path = path.substr(slash + 1);
    }
    return dir;
}

DiscNode* VirtualDisc::find_name(std::string_view name) {
    const auto search = [&](auto&& self, DiscNode& dir) -> DiscNode* {
        for (DiscNode& node : dir.children) {
            if (node.directory) {
                if (DiscNode* found = self(self, node)) return found;
            } else if (same_name(node.name, name)) {
                return &node;
            }
        }
        return nullptr;
    };
    return search(search, root_);
}
std::vector<std::pair<DiscNode*, std::string>> VirtualDisc::find_names(std::string_view name) {
    std::vector<std::pair<DiscNode*, std::string>> found;
    const auto search = [&](auto&& self, DiscNode& dir, const std::string& path) -> void {
        for (DiscNode& node : dir.children) {
            const std::string child = path + "/" + node.name;
            if (node.directory) {
                self(self, node, child);
            } else if (same_name(node.name, name)) {
                found.emplace_back(&node, child);
            }
        }
    };
    search(search, root_, std::string());
    return found;
}

void VirtualDisc::patch(DiscNode& file, const std::filesystem::path& host_file, std::uint64_t host_size,
                        std::uint64_t offset, std::uint64_t file_offset, std::uint64_t length, bool resize) {
    std::vector<DiscPiece>& pieces = file.pieces;
    const std::uint64_t external_offset = std::min(file_offset, host_size);
    const std::uint64_t external_size = host_size - external_offset;
    const std::uint64_t patch_start = offset;
    const std::uint64_t patch_size = length == 0u ? external_size : length;
    const std::uint64_t patch_end = patch_start + patch_size;
    const std::uint64_t target_size = resize ? patch_end : std::max(file.size, patch_end);

    std::size_t insert_at = 0;
    if (patch_start >= file.size) {
        if (patch_start > file.size) {
            pieces.push_back({DiscPiece::Source::Zero, file.size, patch_start - file.size, 0, {}});
        }
        insert_at = pieces.size();
    } else {
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const std::uint64_t start = pieces[i].offset;
            const std::uint64_t end = start + pieces[i].size;
            if (patch_start > start && patch_start < end) {
                pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(i) + 1, pieces[i]);
                split_piece(pieces[i], pieces[i + 1], patch_start);
                continue;
            }
            if (patch_end > start && patch_end < end) {
                pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(i) + 1, pieces[i]);
                split_piece(pieces[i], pieces[i + 1], patch_end);
            }
        }
        insert_at = pieces.size();
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (pieces[i].offset >= patch_start) {
                insert_at = i;
                std::size_t j = i;
                while (j < pieces.size() && patch_end >= pieces[j].offset + pieces[j].size) ++j;
                pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(i), pieces.begin() + static_cast<std::ptrdiff_t>(j));
                break;
            }
        }
    }
    if (patch_size > 0u && external_size > 0u) {
        pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(insert_at),
                      {DiscPiece::Source::HostFile, patch_start, std::min(patch_size, external_size), external_offset,
                       host_file});
        ++insert_at;
    }
    if (external_size < patch_size) {
        pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(insert_at),
                      {DiscPiece::Source::Zero, patch_start + external_size, patch_size - external_size, 0, {}});
    }
    file.size = target_size;
    while (!pieces.empty() && pieces.back().offset >= target_size) pieces.pop_back();
    if (!pieces.empty() && pieces.back().offset + pieces.back().size > target_size) {
        pieces.back().size = target_size - pieces.back().offset;
    }
    file.changed = true;
}

std::vector<std::uint8_t> VirtualDisc::build(std::uint64_t virtual_base) {
    virtual_base_ = (virtual_base + kPlaceAlign - 1u) & ~(kPlaceAlign - 1u);
    placed_.clear();
    std::vector<std::uint8_t> entries;
    std::vector<std::uint8_t> strings{0};  // the root's name
    std::uint64_t cursor = virtual_base_;
    bool fits = true;
    // Entry 0 (the root) first; its end is written once known.
    put32(entries, 0x01000000u);
    put32(entries, 0u);
    put32(entries, 0u);
    std::uint32_t count = 1;
    const auto add = [&](auto&& self, const DiscNode& dir, std::uint32_t parent, const std::string& dir_path) -> void {
        for (const DiscNode& node : dir.children) {
            const std::uint32_t index = count++;
            const auto name_at = static_cast<std::uint32_t>(strings.size());
            strings.insert(strings.end(), node.name.begin(), node.name.end());
            strings.push_back(0);
            const std::size_t at = entries.size();
            put32(entries, (node.directory ? 0x01000000u : 0u) | (name_at & 0x00FFFFFFu));
            if (node.directory) {
                put32(entries, parent);
                put32(entries, 0u);
                self(self, node, index, dir_path + "/" + node.name);
                const std::uint32_t next = count;
                entries[at + 8] = static_cast<std::uint8_t>(next >> 24u);
                entries[at + 9] = static_cast<std::uint8_t>(next >> 16u);
                entries[at + 10] = static_cast<std::uint8_t>(next >> 8u);
                entries[at + 11] = static_cast<std::uint8_t>(next);
            } else {
                std::uint64_t position = node.partition_offset;
                if (node.changed) {
                    position = cursor;
                    placed_.push_back({cursor, &node, dir_path + "/" + node.name});
                    cursor = (cursor + std::max<std::uint64_t>(node.size, 1u) + kPlaceAlign - 1u) & ~(kPlaceAlign - 1u);
                }
                fits = fits && position + node.size <= kDiscReach && node.size <= 0xFFFFFFFFull;
                put32(entries, static_cast<std::uint32_t>(position >> 2u));
                put32(entries, static_cast<std::uint32_t>(node.size));
            }
        }
    };
    add(add, root_, 0u, std::string());
    entries[8] = static_cast<std::uint8_t>(count >> 24u);
    entries[9] = static_cast<std::uint8_t>(count >> 16u);
    entries[10] = static_cast<std::uint8_t>(count >> 8u);
    entries[11] = static_cast<std::uint8_t>(count);
    if (!fits || strings.size() > 0x01000000u) {
        placed_.clear();
        return {};
    }
    entries.insert(entries.end(), strings.begin(), strings.end());
    return entries;
}

void VirtualDisc::read_pieces(const DiscNode& file, std::uint64_t at, std::uint64_t size, std::uint8_t* dst,
                              const std::uint8_t* partition, std::uint64_t partition_size) {
    std::memset(dst, 0, static_cast<std::size_t>(size));
    const std::uint64_t end = at + size;
    for (const DiscPiece& piece : file.pieces) {
        const std::uint64_t from = std::max(at, piece.offset);
        const std::uint64_t to = std::min(end, piece.offset + piece.size);
        if (from >= to) continue;
        const std::uint64_t within = from - piece.offset;
        std::uint8_t* out = dst + (from - at);
        if (piece.source == DiscPiece::Source::Partition) {
            const std::uint64_t source = piece.source_offset + within;
            if (source < partition_size) {
                std::memcpy(out, partition + source, static_cast<std::size_t>(std::min(to - from, partition_size - source)));
            }
        } else if (piece.source == DiscPiece::Source::HostFile) {
            (void)host_files().read(piece.path, piece.source_offset + within, to - from, out);
        }
    }
}

bool VirtualDisc::read(std::uint64_t offset, std::uint32_t size, std::uint8_t* dst, const std::uint8_t* partition) const {
    if (offset < virtual_base_ || placed_.empty()) {
        if (offset > partition_size_ || partition_size_ - offset < size) return false;
        std::memcpy(dst, partition + offset, size);
        return true;
    }
    const auto it = std::upper_bound(placed_.begin(), placed_.end(), offset,
                                     [](std::uint64_t value, const Placed& p) { return value < p.start; });
    if (it == placed_.begin()) return false;
    const Placed& placed = *(it - 1);
    const std::uint64_t within = offset - placed.start;
    const std::uint64_t span = (std::max<std::uint64_t>(placed.file->size, 1u) + kPlaceAlign - 1u) & ~(kPlaceAlign - 1u);
    if (within >= span || span - within < size) return false;
    read_pieces(*placed.file, within, size, dst, partition, partition_size_);
    return true;
}

const DiscNode* VirtualDisc::placed_file(std::uint64_t offset, const std::string** path) const {
    if (offset < virtual_base_ || placed_.empty()) return nullptr;
    const auto it = std::upper_bound(placed_.begin(), placed_.end(), offset,
                                     [](std::uint64_t value, const Placed& p) { return value < p.start; });
    if (it == placed_.begin()) return nullptr;
    const Placed& placed = *(it - 1);
    const std::uint64_t span = (std::max<std::uint64_t>(placed.file->size, 1u) + kPlaceAlign - 1u) & ~(kPlaceAlign - 1u);
    if (offset - placed.start >= span) return nullptr;
    if (path != nullptr) *path = &placed.path;
    return placed.file;
}

std::vector<std::uint8_t> VirtualDisc::materialize(const DiscNode& file, const std::uint8_t* partition,
                                                   std::uint64_t partition_size) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.size));
    if (!bytes.empty()) read_pieces(file, 0, file.size, bytes.data(), partition, partition_size);
    return bytes;
}

}  // namespace galaxy::mods
