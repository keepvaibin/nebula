#include "galaxy/disc_files.h"

namespace galaxy::disc {

namespace {

std::uint32_t load_be32(const std::uint8_t* bytes) {
    return (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) | (std::uint32_t{bytes[2]} << 8) |
        std::uint32_t{bytes[3]};
}

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same_name(std::string_view a, const char* b) {
    std::size_t i = 0;
    for (; b[i] != '\0'; i++) {
        if (i == a.size() || lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return i == a.size();
}

}  // namespace

bool FileTable::load(const std::uint8_t* fst, std::size_t size) {
    entries_.clear();
    names_.clear();
    child_start_.clear();
    children_.clear();
    if (fst == nullptr || size < 12) {
        return false;
    }
    const std::uint32_t count = load_be32(fst + 8);
    if (count == 0 || count > size / 12 || (load_be32(fst) >> 24) == 0) {
        return false;
    }
    std::vector<Raw> entries(count);
    for (std::uint32_t i = 0; i < count; i++) {
        const std::uint8_t* raw = fst + std::size_t{i} * 12;
        const std::uint32_t word = load_be32(raw);
        entries[i] = {(word >> 24) != 0, word & 0xFFFFFFu, load_be32(raw + 4), load_be32(raw + 8)};
    }
    std::string names(reinterpret_cast<const char*>(fst) + std::size_t{count} * 12, size - std::size_t{count} * 12);
    names.push_back('\0');
    for (std::uint32_t i = 0; i < count; i++) {
        const Raw& e = entries[i];
        if (i != 0 && e.name >= names.size() - 1) {
            return false;
        }
        // A folder's entries run from the one after it to its "next" (exclusive), inside its parent's.
        if (e.folder && (e.next_or_length <= i || e.next_or_length > count ||
                         (i != 0 && (e.parent_or_position >= i || !entries[e.parent_or_position].folder)))) {
            return false;
        }
    }
    std::vector<std::uint32_t> child_start(count + 1, 0);
    std::vector<std::int32_t> children;
    for (std::uint32_t i = 0; i < count; i++) {
        child_start[i] = static_cast<std::uint32_t>(children.size());
        if (entries[i].folder) {
            for (std::uint32_t c = i + 1; c < entries[i].next_or_length;
                 c = entries[c].folder ? entries[c].next_or_length : c + 1) {
                children.push_back(static_cast<std::int32_t>(c));
            }
        }
    }
    child_start[count] = static_cast<std::uint32_t>(children.size());
    entries_ = std::move(entries);
    names_ = std::move(names);
    child_start_ = std::move(child_start);
    children_ = std::move(children);
    return true;
}

std::optional<Entry> FileTable::entry(std::int32_t index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) {
        return std::nullopt;
    }
    const Raw& e = entries_[static_cast<std::size_t>(index)];
    Entry out;
    out.index = index;
    out.folder = e.folder;
    out.name = names_.c_str() + (index == 0 ? names_.size() - 1 : e.name);
    if (!e.folder) {
        out.length = e.next_or_length;
        out.position = std::uint64_t{e.parent_or_position} << 2;
    }
    return out;
}

std::optional<Entry> FileTable::child(std::int32_t folder, std::uint32_t index) const {
    if (folder < 0 || static_cast<std::size_t>(folder) >= entries_.size() ||
        !entries_[static_cast<std::size_t>(folder)].folder) {
        return std::nullopt;
    }
    const std::uint32_t first = child_start_[static_cast<std::size_t>(folder)];
    const std::uint32_t end = child_start_[static_cast<std::size_t>(folder) + 1];
    if (index >= end - first) {
        return std::nullopt;
    }
    return entry(children_[first + index]);
}

// The disc's own walk (DVDConvertPathToEntrynum): a name followed by '/' matches folders only, a last name files and
// folders; '/' at a name's start returns to the root.
std::optional<Entry> FileTable::find(std::string_view path) const {
    if (entries_.empty()) {
        return std::nullopt;
    }
    std::uint32_t folder = 0;
    std::size_t at = 0;
    while (true) {
        const std::string_view rest = path.substr(at);
        if (rest.empty()) {
            return entry(static_cast<std::int32_t>(folder));
        }
        if (rest[0] == '/') {
            folder = 0;
            at++;
            continue;
        }
        if (rest.substr(0, 3) == "../") {
            folder = entries_[folder].parent_or_position;
            at += 3;
            continue;
        }
        if (rest == "..") {
            return entry(static_cast<std::int32_t>(entries_[folder].parent_or_position));
        }
        if (rest.substr(0, 2) == "./") {
            at += 2;
            continue;
        }
        if (rest == ".") {
            return entry(static_cast<std::int32_t>(folder));
        }
        const std::size_t slash = rest.find('/');
        const bool more = slash != std::string_view::npos;
        const std::string_view name = rest.substr(0, slash);
        std::int32_t found = -1;
        const std::uint32_t first = child_start_[folder];
        const std::uint32_t end = child_start_[folder + 1];
        for (std::uint32_t c = first; c < end; c++) {
            const Raw& e = entries_[static_cast<std::size_t>(children_[c])];
            if ((more && !e.folder) || !same_name(name, names_.c_str() + e.name)) {
                continue;
            }
            found = children_[c];
            break;
        }
        if (found < 0) {
            return std::nullopt;
        }
        if (!more) {
            return entry(found);
        }
        folder = static_cast<std::uint32_t>(found);
        at += name.size() + 1;
    }
}

}  // namespace galaxy::disc
