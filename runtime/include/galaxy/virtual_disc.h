// SPDX-License-Identifier: GPL-3.0-only
// The game's disc with mods' files: its file table (FST) as a tree whose files are pieces of the game partition, of
// files on the host or zeros. Untouched files keep their place in the partition; changed and new files are placed
// past its end, where reads come from their pieces. The game reads it as the disc (no file is copied).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::mods {

// A piece of a disc file: `size` bytes at `offset` in the file, from the game partition, a host file or zeros.
struct DiscPiece {
    enum class Source : std::uint8_t { Partition, HostFile, Zero };
    Source source = Source::Zero;
    std::uint64_t offset = 0;         // in the disc file
    std::uint64_t size = 0;
    std::uint64_t source_offset = 0;  // in the partition or the host file
    std::filesystem::path path;       // HostFile
};

struct DiscNode {
    std::string name;
    bool directory = false;
    std::vector<DiscNode> children;   // directory
    std::uint64_t size = 0;           // file
    std::vector<DiscPiece> pieces;    // file, in order, covering [0, size) with gaps read as zeros
    std::uint64_t partition_offset = 0;  // file: where the game partition holds it (untouched files keep it)
    bool changed = false;             // file: changed or new (placed past the partition)
};

class VirtualDisc {
public:
    // The game partition's file table (as on the disc, big-endian) and its size; false when the table is malformed.
    bool load(std::span<const std::uint8_t> fst, std::uint64_t partition_size, std::string* error);
    // The file or directory at `path` ("/a/b", case-insensitive as the game looks files up); with `create`, a file
    // (and its directories) is added when missing. Null when absent, or when a file and a directory clash.
    DiscNode* find_path(std::string_view path, bool create);
    // The first file of that name anywhere.
    DiscNode* find_name(std::string_view name);
    // Every file of that name (Riivolution's disc paths without a leading '/': Dolphin patches each), with its path.
    std::vector<std::pair<DiscNode*, std::string>> find_names(std::string_view name);
    // Writes `length` bytes (0: the rest of the host file) of a host file from `file_offset` over a disc file at
    // `offset`; `resize`: the disc file ends where the patch does (else it keeps its size when longer). As Riivolution.
    static void patch(DiscNode& file, const std::filesystem::path& host_file, std::uint64_t host_size, std::uint64_t offset,
                      std::uint64_t file_offset, std::uint64_t length, bool resize);
    // The changed table (as on the disc) with changed files placed from `virtual_base`; the reads then resolve.
    [[nodiscard]] std::vector<std::uint8_t> build(std::uint64_t virtual_base);
    // Fills `dst` with `size` bytes at disc offset `offset` (partition or placed files); false past every file.
    bool read(std::uint64_t offset, std::uint32_t size, std::uint8_t* dst, const std::uint8_t* partition) const;
    // The changed file placed at disc offset `offset` and its disc path ("/a/b"); null in the partition or past it.
    [[nodiscard]] const DiscNode* placed_file(std::uint64_t offset, const std::string** path = nullptr) const;

    [[nodiscard]] std::uint64_t changed_files() const noexcept { return placed_.size(); }
    // A disc file's bytes, its pieces read (the DOL's patches: main.dol is a file Riivolution can patch).
    static std::vector<std::uint8_t> materialize(const DiscNode& file, const std::uint8_t* partition,
                                                 std::uint64_t partition_size);

private:
    struct Placed {
        std::uint64_t start;  // disc offset
        const DiscNode* file;
        std::string path;     // on the disc
    };
    static void read_pieces(const DiscNode& file, std::uint64_t at, std::uint64_t size, std::uint8_t* dst,
                            const std::uint8_t* partition, std::uint64_t partition_size);

    DiscNode root_;
    std::uint64_t partition_size_ = 0;
    std::uint64_t virtual_base_ = 0;
    std::vector<Placed> placed_;  // by start
};

}  // namespace galaxy::mods
