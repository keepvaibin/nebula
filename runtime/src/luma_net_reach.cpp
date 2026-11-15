// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/luma_net_reach.h"

#include <chrono>
#include <cstring>

#if defined(_WIN32)
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#endif

namespace galaxy::luma_net {
namespace {

// The IPv4 address an IPv4-mapped IPv6 address holds, into *out; false when it is not one.
bool mapped_v4(const in6_addr& address, in_addr* out) noexcept {
    static const unsigned char kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFu, 0xFFu};
    if (std::memcmp(address.s6_addr, kMapped, 12) != 0) {
        return false;
    }
    std::memcpy(&out->s_addr, address.s6_addr + 12, 4);
    return true;
}

// The address in the form the list is compared in: IPv4 (an IPv4-mapped IPv6 address as its IPv4 part) or IPv6, port
// and scope cleared. False for another family, the unspecified address, multicast and the IPv4 broadcast.
bool comparable(const sockaddr_storage& address, sockaddr_storage* out) noexcept {
    std::memset(out, 0, sizeof(*out));
    in_addr v4{};
    bool is_v4 = false;
    if (address.ss_family == AF_INET) {
        v4 = reinterpret_cast<const sockaddr_in*>(&address)->sin_addr;
        is_v4 = true;
    } else if (address.ss_family == AF_INET6) {
        const in6_addr& v6 = reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr;
        if (mapped_v4(v6, &v4)) {
            is_v4 = true;
        } else {
            static const unsigned char kZero[16] = {};
            if (std::memcmp(v6.s6_addr, kZero, 16) == 0 || v6.s6_addr[0] == 0xFFu) {
                return false;  // unspecified, multicast
            }
            auto* six = reinterpret_cast<sockaddr_in6*>(out);
            six->sin6_family = AF_INET6;
            six->sin6_addr = v6;
            return true;
        }
    } else {
        return false;
    }
    const std::uint32_t a = ntohl(v4.s_addr);
    if (a == 0u || a == 0xFFFFFFFFu || (a >> 28u) == 0xEu) {
        return false;  // unspecified, the broadcast, multicast
    }
    auto* four = reinterpret_cast<sockaddr_in*>(out);
    four->sin_family = AF_INET;
    four->sin_addr = v4;
    return is_v4;
}

bool same(const sockaddr_storage& a, const sockaddr_storage& b) noexcept {
    if (a.ss_family != b.ss_family) {
        return false;
    }
    if (a.ss_family == AF_INET) {
        return reinterpret_cast<const sockaddr_in*>(&a)->sin_addr.s_addr == reinterpret_cast<const sockaddr_in*>(&b)->sin_addr.s_addr;
    }
    return std::memcmp(&reinterpret_cast<const sockaddr_in6*>(&a)->sin6_addr, &reinterpret_cast<const sockaddr_in6*>(&b)->sin6_addr,
                       sizeof(in6_addr)) == 0;
}

void keep(const sockaddr* address, std::vector<sockaddr_storage>* out) {
    if (address == nullptr || (address->sa_family != AF_INET && address->sa_family != AF_INET6)) {
        return;
    }
    sockaddr_storage raw{};
    std::memcpy(&raw, address, address->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
    sockaddr_storage form{};
    if (comparable(raw, &form)) {
        out->push_back(form);
    }
}

std::uint64_t steady_ms() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

bool local_v4(const in_addr& address) noexcept {
    const std::uint32_t a = ntohl(address.s_addr);
    return (a >> 24u) == 127u || (a >> 24u) == 10u || (a >> 20u) == 0xAC1u /* 172.16/12 */ ||
           (a >> 16u) == 0xC0A8u /* 192.168/16 */ || (a >> 16u) == 0xA9FEu /* 169.254/16 */;
}

bool local_v6(const in6_addr& address) noexcept {
    const unsigned char* b = address.s6_addr;
    static const unsigned char kLoopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    if (std::memcmp(b, kLoopback, 16) == 0) {
        return true;
    }
    return (b[0] & 0xFEu) == 0xFCu /* fc00::/7 */ || (b[0] == 0xFEu && (b[1] & 0xC0u) == 0x80u) /* fe80::/10 */;
}

bool local_range(const sockaddr_storage& address) noexcept {
    if (address.ss_family == AF_INET) {
        return local_v4(reinterpret_cast<const sockaddr_in*>(&address)->sin_addr);
    }
    if (address.ss_family == AF_INET6) {
        const in6_addr& v6 = reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr;
        in_addr v4{};
        return mapped_v4(v6, &v4) ? local_v4(v4) : local_v6(v6);
    }
    return false;
}

bool interface_addresses(std::vector<sockaddr_storage>* out) noexcept {
    out->clear();
    try {
#if defined(_WIN32)
        ULONG size = 16u * 1024u;
        std::vector<unsigned char> buffer;
        ULONG got = ERROR_BUFFER_OVERFLOW;
        for (int attempt = 0; attempt < 3 && got == ERROR_BUFFER_OVERFLOW; ++attempt) {
            buffer.resize(size);
            got = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                       reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
        }
        if (got != NO_ERROR) {
            return false;
        }
        for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter != nullptr; adapter = adapter->Next) {
            for (IP_ADAPTER_UNICAST_ADDRESS* one = adapter->FirstUnicastAddress; one != nullptr; one = one->Next) {
                keep(one->Address.lpSockaddr, out);
            }
        }
        return true;
#else
        ifaddrs* list = nullptr;
        if (::getifaddrs(&list) != 0) {
            return false;
        }
        for (ifaddrs* one = list; one != nullptr; one = one->ifa_next) {
            keep(one->ifa_addr, out);
        }
        ::freeifaddrs(list);
        return true;
#endif
    } catch (...) {
        out->clear();
        return false;
    }
}

bool OwnAddresses::listed_locked(const sockaddr_storage& form) const noexcept {
    for (const sockaddr_storage& one : list_) {
        if (same(one, form)) {
            return true;
        }
    }
    return false;
}

bool OwnAddresses::contains(const sockaddr_storage& address, std::uint64_t now_ms) noexcept {
    sockaddr_storage form{};
    if (!comparable(address, &form)) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    if (read_ && listed_locked(form)) {
        return true;
    }
    // Not in the list read last (or none read yet): read again, at most once a second, so a flood of outside peers
    // costs one system list a second, not one per peer.
    if (read_ && now_ms - read_at_ms_ < 1000u) {
        return false;
    }
    std::vector<sockaddr_storage> fresh;
    if (source_ != nullptr && source_(&fresh)) {
        list_.swap(fresh);
    }
    read_ = true;
    read_at_ms_ = now_ms;
    return listed_locked(form);
}

bool own_address(const sockaddr_storage& address) noexcept {
    static OwnAddresses* const own = new OwnAddresses();  // never destroyed: asked from service threads until exit
    return own->contains(address, steady_ms());
}

bool reach_local(const sockaddr_storage& address) noexcept {
    return local_range(address) || own_address(address);
}

}  // namespace galaxy::luma_net
