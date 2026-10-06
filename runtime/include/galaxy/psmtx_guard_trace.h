#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <ostream>

namespace galaxy::diagnostics {

// Generated leaves flush after their bounded sampling window closes. The
// runtime logger must retain those summaries before ordinary Trace filtering.
// No clock, allocation, output, or guest-state change occurs here.
class PsmtxGuardTrace {
public:
    static constexpr std::size_t kCapacity = 16u;
    static constexpr std::size_t kTextCapacity = 1'024u;

    bool capture(const char* message) noexcept {
        constexpr char psmtx_prefix[] = "[psmtx-local-guard] ";
        constexpr char psvec_normalize_prefix[] =
            "[psvec-normalize-local-guard] ";
        if (message == nullptr) {
            return false;
        }
        const bool is_psmtx =
            std::strncmp(message, psmtx_prefix, sizeof(psmtx_prefix) - 1u) == 0;
        const bool is_psvec_normalize = std::strncmp(
            message, psvec_normalize_prefix,
            sizeof(psvec_normalize_prefix) - 1u) == 0;
        if (!is_psmtx && !is_psvec_normalize) {
            return false;
        }
        if (is_psmtx) {
            ++psmtx_seen_;
        } else {
            ++psvec_normalize_seen_;
        }
        if (retained_ == records_.size()) {
            ++overflow_;
            return true;
        }
        auto& record = records_[retained_++];
        std::size_t length = 0u;
        while (length + 1u < record.size() && message[length] != '\0') {
            record[length] = message[length];
            ++length;
        }
        record[length] = '\0';
        if (message[length] != '\0') {
            ++truncated_;
        }
        return true;
    }

    void dump(std::ostream& output) const {
        if (retained_ == 0u && overflow_ == 0u) {
            return;
        }
        output << "[local-guard-capture] retained=" << retained_
               << " psmtxSeen=" << psmtx_seen_
               << " psvecNormalizeSeen=" << psvec_normalize_seen_
               << " overflow=" << overflow_ << " truncated=" << truncated_
               << " missing-flush-not-zero=1\n";
        for (std::size_t index = 0u; index < retained_; ++index) {
            output << records_[index].data() << '\n';
        }
    }

    [[nodiscard]] std::size_t retained() const noexcept { return retained_; }
    [[nodiscard]] std::size_t overflow() const noexcept { return overflow_; }
    [[nodiscard]] std::size_t truncated() const noexcept { return truncated_; }

private:
    std::array<std::array<char, kTextCapacity>, kCapacity> records_{};
    std::size_t retained_{};
    std::size_t overflow_{};
    std::size_t truncated_{};
    std::size_t psmtx_seen_{};
    std::size_t psvec_normalize_seen_{};
};

}  // namespace galaxy::diagnostics
