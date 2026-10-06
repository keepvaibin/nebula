#pragma once

// Galaxy adapter for Aurora's ordered frame staging model:
// encounter/aurora@77326d45415a64c40e560cebd2cdef0a0f08d840,
// lib/gfx/frame_packet.hpp and lib/gfx/recording.cpp (MIT). The event format
// is implemented independently for GX. A packet is not replayable until every
// opaque resource binding has an owned lifetime.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

enum class DependencyReadSource : std::uint8_t {
    Parser,
    IndexedVertex,
    Texture,
    Tlut,
};

struct DependencyAliasKey {
    std::uint32_t guest_addr = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t format = 0;

    [[nodiscard]] bool operator==(const DependencyAliasKey&) const = default;
};

class DependencyEventSink {
public:
    virtual ~DependencyEventSink() = default;
    virtual void guest_read(
        DependencyReadSource source,
        std::uint32_t guest_addr,
        std::span<const std::byte> bytes) = 0;
    virtual void alias_copy(
        DependencyAliasKey key,
        std::uintptr_t resource) = 0;
    virtual void alias_bind(
        DependencyAliasKey key,
        std::uintptr_t resource) = 0;
    virtual void alias_retire(
        DependencyAliasKey key,
        std::uintptr_t resource) = 0;
    virtual void pe_finish() = 0;
    virtual void pe_token(std::uint16_t token, bool interrupt) = 0;
    virtual void xfb_copy(std::uint32_t guest_addr,
                          std::uint32_t exec_command) = 0;
};

// Bounded event stream from one render parse. Repeated reads of the same guest
// address stay distinct and own the bytes seen at that read. Bindings to
// resources created before capture use generation zero, so a packet builder
// cannot mistake them for guest bytes.
class OwnedDependencyEvents final : public DependencyEventSink {
public:
    enum class Kind : std::uint8_t {
        GuestRead,
        AliasCopy,
        AliasBind,
        AliasRetire,
        PeFinish,
        PeToken,
        XfbCopy,
    };

    struct Event {
        std::uint64_t ordinal = 0;
        Kind kind = Kind::GuestRead;
        DependencyReadSource read_source = DependencyReadSource::Parser;
        std::uint32_t guest_addr = 0;
        std::uint32_t byte_size = 0;
        std::uint32_t byte_offset = 0;
        DependencyAliasKey alias{};
        std::uintptr_t resource = 0;
        std::uint64_t generation = 0;
        std::uint32_t exec_command = 0;
        std::uint16_t token = 0;
        bool interrupt = false;
    };

    struct Limits {
        std::size_t events = 1'000'000u;
        std::size_t bytes = 256u * 1024u * 1024u;
    };

    OwnedDependencyEvents() = default;
    explicit OwnedDependencyEvents(Limits limits) : limits_(limits) {}

    void guest_read(DependencyReadSource source,
                    std::uint32_t guest_addr,
                    std::span<const std::byte> bytes) override {
        if (bytes.empty()) return;
        if (static_cast<std::uint64_t>(guest_addr) + bytes.size() >
            (std::uint64_t{1} << 32u)) {
            throw std::invalid_argument("GX dependency guest range overflows");
        }
        require_room(bytes_.size(), bytes.size(), limits_.bytes);
        require_room(events_.size(), 1u, limits_.events);
        const auto offset = static_cast<std::uint32_t>(bytes_.size());
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        Event event = next(Kind::GuestRead);
        event.read_source = source;
        event.guest_addr = guest_addr;
        event.byte_offset = offset;
        event.byte_size = static_cast<std::uint32_t>(bytes.size());
        events_.push_back(event);
    }

    void alias_copy(DependencyAliasKey key,
                    std::uintptr_t resource) override {
        if (resource == 0u) throw std::invalid_argument("null GX alias resource");
        require_room(events_.size(), 1u, limits_.events);
        const std::uint64_t generation = ++next_generation_;
        aliases_[key] = AliasVersion{resource, generation};
        Event event = next(Kind::AliasCopy);
        event.alias = key;
        event.resource = resource;
        event.generation = generation;
        events_.push_back(event);
    }

    void alias_bind(DependencyAliasKey key,
                    std::uintptr_t resource) override {
        require_room(events_.size(), 1u, limits_.events);
        Event event = next(Kind::AliasBind);
        event.alias = key;
        event.resource = resource;
        const auto found = aliases_.find(key);
        if (found != aliases_.end() && found->second.resource == resource) {
            event.generation = found->second.generation;
        } else {
            has_external_resources_ = true;
        }
        events_.push_back(event);
    }

    void alias_retire(DependencyAliasKey key,
                      std::uintptr_t resource) override {
        require_room(events_.size(), 1u, limits_.events);
        Event event = next(Kind::AliasRetire);
        event.alias = key;
        event.resource = resource;
        const auto found = aliases_.find(key);
        if (found != aliases_.end() && found->second.resource == resource) {
            event.generation = found->second.generation;
            aliases_.erase(found);
        } else {
            has_external_resources_ = true;
        }
        events_.push_back(event);
    }

    void pe_finish() override {
        append_simple(Kind::PeFinish);
    }

    void pe_token(std::uint16_t token, bool interrupt) override {
        require_room(events_.size(), 1u, limits_.events);
        Event event = next(Kind::PeToken);
        event.token = token;
        event.interrupt = interrupt;
        events_.push_back(event);
    }

    void xfb_copy(std::uint32_t guest_addr,
                  std::uint32_t exec_command) override {
        require_room(events_.size(), 1u, limits_.events);
        Event event = next(Kind::XfbCopy);
        event.guest_addr = guest_addr;
        event.exec_command = exec_command;
        events_.push_back(event);
    }

    [[nodiscard]] std::span<const Event> events() const noexcept {
        return events_;
    }
    [[nodiscard]] std::span<const std::byte> read_bytes(
        const Event& event) const {
        if (event.kind != Kind::GuestRead ||
            static_cast<std::uint64_t>(event.byte_offset) +
                    event.byte_size > bytes_.size()) {
            throw std::out_of_range("not an owned GX guest read");
        }
        return std::span<const std::byte>(bytes_)
            .subspan(event.byte_offset, event.byte_size);
    }
    [[nodiscard]] bool has_external_resources() const noexcept {
        return has_external_resources_;
    }
    [[nodiscard]] std::size_t captured_bytes() const noexcept {
        return bytes_.size();
    }

private:
    struct AliasHash {
        [[nodiscard]] std::size_t operator()(
            const DependencyAliasKey& key) const noexcept {
            std::uint64_t value = key.guest_addr;
            value = value * 1099511628211ull ^ key.width;
            value = value * 1099511628211ull ^ key.height;
            value = value * 1099511628211ull ^ key.format;
            return static_cast<std::size_t>(value);
        }
    };
    struct AliasVersion {
        std::uintptr_t resource = 0;
        std::uint64_t generation = 0;
    };

    static void require_room(std::size_t used, std::size_t incoming,
                             std::size_t limit) {
        if (used > limit || incoming > limit - used ||
            used + incoming > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("GX dependency capture limit exceeded");
        }
    }
    [[nodiscard]] Event next(Kind kind) noexcept {
        Event event{};
        event.ordinal = ++next_ordinal_;
        event.kind = kind;
        return event;
    }
    void append_simple(Kind kind) {
        require_room(events_.size(), 1u, limits_.events);
        events_.push_back(next(kind));
    }

    Limits limits_{};
    std::vector<Event> events_;
    std::vector<std::byte> bytes_;
    std::unordered_map<DependencyAliasKey, AliasVersion, AliasHash> aliases_;
    std::uint64_t next_ordinal_ = 0;
    std::uint64_t next_generation_ = 0;
    bool has_external_resources_ = false;
};

}  // namespace galaxy::gx
