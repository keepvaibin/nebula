// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/luma_net_reach.h"

#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <unistd.h>
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

#if defined(_WIN32)
using Sock = SOCKET;
const Sock kNone = INVALID_SOCKET;
void close_sock(Sock s) noexcept { closesocket(s); }
#else
using Sock = int;
const Sock kNone = -1;
void close_sock(Sock s) noexcept { ::close(s); }
#endif

// Whether a datagram socket binds to `address` (port 0): the system allows it only for an address of its own.
template <class Address>
bool binds(int family, Address address) noexcept {
    const Sock s = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNone) {
        return false;
    }
    const bool ok = ::bind(s, reinterpret_cast<const sockaddr*>(&address), static_cast<socklen_t>(sizeof(address))) == 0;
    close_sock(s);
    return ok;
}

bool own_v4(const in_addr& address) noexcept {
    const std::uint32_t a = ntohl(address.s_addr);
    if (a == 0u || a == 0xFFFFFFFFu || (a >> 28u) == 0xEu) {
        return false;  // unspecified (binds as "every interface"), the broadcast, multicast
    }
    sockaddr_in v4{};
    v4.sin_family = AF_INET;
    v4.sin_addr = address;
    return binds(AF_INET, v4);
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

bool own_address(const sockaddr_storage& address) noexcept {
    if (address.ss_family == AF_INET) {
        return own_v4(reinterpret_cast<const sockaddr_in*>(&address)->sin_addr);
    }
    if (address.ss_family == AF_INET6) {
        const in6_addr& v6 = reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr;
        in_addr v4{};
        if (mapped_v4(v6, &v4)) {
            return own_v4(v4);
        }
        static const unsigned char kZero[16] = {};
        if (std::memcmp(v6.s6_addr, kZero, 16) == 0 || v6.s6_addr[0] == 0xFFu) {
            return false;  // unspecified, multicast
        }
        sockaddr_in6 bind_to{};
        bind_to.sin6_family = AF_INET6;
        bind_to.sin6_addr = v6;
        return binds(AF_INET6, bind_to);
    }
    return false;
}

bool reach_local(const sockaddr_storage& address) noexcept {
    return local_range(address) || own_address(address);
}

}  // namespace galaxy::luma_net
