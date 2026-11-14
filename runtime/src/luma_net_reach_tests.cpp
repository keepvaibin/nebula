// SPDX-License-Identifier: GPL-3.0-only
// net.local's one judgement of an address (galaxy/luma_net_reach.h) over a table: the ranges, the IPv4-mapped forms
// of a dual-stack socket's peer, carrier NAT and public addresses; then this machine's own addresses by binding.
#include "galaxy/luma_net_reach.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#if !defined(_WIN32)
#include <arpa/inet.h>
#endif

namespace {

int failed = 0;

sockaddr_storage address(const char* text) {
    sockaddr_storage out{};
    sockaddr_in v4{};
    sockaddr_in6 v6{};
    if (inet_pton(AF_INET, text, &v4.sin_addr) == 1) {
        v4.sin_family = AF_INET;
        std::memcpy(&out, &v4, sizeof(v4));
    } else if (inet_pton(AF_INET6, text, &v6.sin6_addr) == 1) {
        v6.sin6_family = AF_INET6;
        std::memcpy(&out, &v6, sizeof(v6));
    } else {
        std::fprintf(stderr, "FAILED: the table's %s is no address\n", text);
        ++failed;
    }
    return out;
}

void range(const char* text, bool local) {
    if (galaxy::luma_net::local_range(address(text)) != local) {
        std::fprintf(stderr, "FAILED: local_range(%s) should be %s\n", text, local ? "true" : "false");
        ++failed;
    }
}

void reach(const char* text, bool local) {
    if (galaxy::luma_net::reach_local(address(text)) != local) {
        std::fprintf(stderr, "FAILED: reach_local(%s) should be %s\n", text, local ? "true" : "false");
        ++failed;
    }
}

}  // namespace

int main() {
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::fprintf(stderr, "FAILED: WSAStartup\n");
        return 1;
    }
#endif
    // Local: loopback, the private ranges, link-local, unique-local; their mapped forms.
    for (const char* text : {"127.0.0.1", "127.255.255.254", "10.0.0.1", "10.255.255.255", "172.16.0.1", "172.31.255.254",
                             "192.168.0.1", "192.168.255.255", "169.254.1.1", "::1", "fc00::1", "fdff:ffff::1", "fe80::1",
                             "febf::1", "::ffff:127.0.0.1", "::ffff:10.1.2.3", "::ffff:192.168.1.20", "::ffff:172.20.0.5",
                             "::ffff:169.254.9.9"}) {
        range(text, true);
    }
    // Not local: public, carrier NAT, the edges of the private ranges, unspecified, broadcast, multicast, the old
    // IPv4-compatible form, NAT64 and 6to4 prefixes over private addresses, site-local, the mapped forms of public ones.
    for (const char* text : {"8.8.8.8", "1.1.1.1", "100.64.0.1", "100.127.255.254", "172.15.255.255", "172.32.0.0",
                             "192.167.255.255", "192.169.0.0", "169.253.255.255", "169.255.0.0", "11.0.0.1", "9.255.255.255",
                             "0.0.0.0", "255.255.255.255", "224.0.0.1", "239.255.255.250", "::", "::127.0.0.1",
                             "64:ff9b::7f00:1", "64:ff9b::c0a8:101", "2002:c0a8:101::1", "fec0::1", "ff02::1",
                             "2001:4860:4860::8888", "::ffff:8.8.8.8", "::ffff:100.64.0.1", "::ffff:0.0.0.0"}) {
        range(text, false);
    }
    // The whole rule: the same answers where binding cannot be (an address no machine has); loopback binds.
    reach("127.0.0.1", true);
    reach("::ffff:127.0.0.1", true);
    for (const char* text : {"0.0.0.0", "255.255.255.255", "224.0.0.1", "::", "ff02::1", "::ffff:0.0.0.0", "192.0.2.1",
                             "198.51.100.7", "203.0.113.9", "2001:db8::1", "100.64.0.1"}) {
        reach(text, false);  // unspecified, broadcast, multicast never count as own; documentation ranges no machine has
    }
    // A family that is not IP: never local.
    sockaddr_storage none{};
    if (galaxy::luma_net::reach_local(none)) {
        std::fprintf(stderr, "FAILED: an address of no family is local\n");
        ++failed;
    }
#if defined(_WIN32)
    WSACleanup();
#endif
    if (failed == 0) {
        std::printf("luma_net_reach: all passed\n");
    }
    return failed == 0 ? 0 : 1;
}
