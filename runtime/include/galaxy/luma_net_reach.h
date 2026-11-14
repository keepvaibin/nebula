// SPDX-License-Identifier: GPL-3.0-only
// What `net.local` reaches, as one judgement used for every address it meets: a connect's and a send's target, a peer a
// listener accepted, the sender of a datagram. Local is this machine (loopback and the machine's own addresses) and the
// local network (10/8, 172.16/12, 192.168/16, 169.254/16, fc00::/7, fe80::/10); an IPv4-mapped IPv6 address
// (::ffff:a.b.c.d) is judged as its IPv4 address. Everything else is not local, 100.64/10 (carrier NAT) included.
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace galaxy::luma_net {

// 127/8, 10/8, 172.16/12, 192.168/16, 169.254/16.
[[nodiscard]] bool local_v4(const in_addr& address) noexcept;
// ::1, fc00::/7, fe80::/10 (an IPv4-mapped address is not judged here: see local_range).
[[nodiscard]] bool local_v6(const in6_addr& address) noexcept;
// The address in the ranges above, an IPv4-mapped IPv6 address by its IPv4 part. False for any other family.
[[nodiscard]] bool local_range(const sockaddr_storage& address) noexcept;
// One of this machine's own addresses (a public address of its own interface): the system lets a socket bind to it.
// Never the unspecified address, a multicast address or the IPv4 broadcast.
[[nodiscard]] bool own_address(const sockaddr_storage& address) noexcept;
// local_range or own_address: the one rule for net.local.
[[nodiscard]] bool reach_local(const sockaddr_storage& address) noexcept;

}  // namespace galaxy::luma_net
