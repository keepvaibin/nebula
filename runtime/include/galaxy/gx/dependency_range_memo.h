#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_set>

namespace galaxy::gx::detail {

// Exact address/size keys for one dependency scan. At most half of the inline
// slots are occupied, bounding probes and avoiding heap work for small scans.
// Large scans retain the standard hash table, constructed only on overflow.
class DependencyRangeMemo {
public:
    [[nodiscard]] bool insert(std::uint64_t key) {
        if (overflow_) return overflow_->insert(key).second;

        // Mix both address and size; compare the complete key after probing.
        const std::uint64_t hash = (key ^ (key >> 32u)) * 0x9E3779B97F4A7C15ull;
        std::size_t slot = static_cast<std::size_t>(hash >> 58u);
        for (;;) {
            const std::uint64_t bit = std::uint64_t{1} << slot;
            if ((occupied_ & bit) == 0u) {
                if (count_ < 32u) {
                    keys_[slot] = key;
                    occupied_ |= bit;
                    ++count_;
                    return true;
                }
                break;
            }
            if (keys_[slot] == key) return false;
            slot = (slot + 1u) & 63u;
        }

        try {
            overflow_.emplace();
            overflow_->reserve(64u);
            for (std::size_t i = 0u; i < keys_.size(); ++i) {
                if ((occupied_ & (std::uint64_t{1} << i)) != 0u) {
                    overflow_->insert(keys_[i]);
                }
            }
            return overflow_->insert(key).second;
        } catch (...) {
            // Keep the complete inline set if allocation fails during promotion.
            overflow_.reset();
            throw;
        }
    }

private:
    std::array<std::uint64_t, 64u> keys_{};
    std::uint64_t occupied_ = 0u;
    std::uint8_t count_ = 0u;
    std::optional<std::unordered_set<std::uint64_t>> overflow_;
};

}  // namespace galaxy::gx::detail
