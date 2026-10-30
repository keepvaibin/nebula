// The game partition's file table (the disc's FST): its files and folders by path, by index and by folder, as the
// native game reads them (B-12). Paths follow the disc's rules: from the root, '/' returning to it, "." and ".."
// steps, letters matched in either case.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::disc {

struct Entry {
    std::int32_t index = -1;    // in the file table; 0 is the root folder
    bool folder = false;
    std::uint32_t length = 0;   // a file's length in bytes
    std::uint64_t position = 0; // a file's first byte in the partition
    const char* name = nullptr; // valid while the table lives
};

class FileTable {
public:
    // A Wii disc's table: 12-byte big-endian entries (positions in 4-byte units), then the names. False when it is
    // malformed (the table is then empty).
    bool load(const std::uint8_t* fst, std::size_t size);

    [[nodiscard]] std::optional<Entry> entry(std::int32_t index) const;
    [[nodiscard]] std::optional<Entry> find(std::string_view path) const;
    // The index-th entry of a folder, in the table's order.
    [[nodiscard]] std::optional<Entry> child(std::int32_t folder, std::uint32_t index) const;

private:
    struct Raw {
        bool folder;
        std::uint32_t name;
        std::uint32_t parent_or_position;
        std::uint32_t next_or_length;
    };

    std::vector<Raw> entries_;
    std::string names_;
    std::vector<std::uint32_t> child_start_;  // per entry: its children's first slot in children_ (folders only)
    std::vector<std::int32_t> children_;
};

}  // namespace galaxy::disc
