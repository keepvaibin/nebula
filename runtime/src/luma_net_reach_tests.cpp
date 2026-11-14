// SPDX-License-Identifier: GPL-3.0-only
// net.local's one judgement of an address (galaxy/luma_net_reach.h) over a table: the ranges, the IPv4-mapped forms
// of a dual-stack socket's peer, carrier NAT and public addresses; then this machine's own addresses from a list (an
// injected one: an address the system would let a socket bind to, as with Linux's nonlocal bind, is not own unless
// listed), read again at most once a second.
#include "galaxy/luma_net_reach.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <vector>

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

// Injected interface lists: the test's own addresses, and how many times each was read.
int reads = 0;
int generation = 0;
bool listed(std::vector<sockaddr_storage>* out) {
    ++reads;
    out->clear();
    out->push_back(address("203.0.113.9"));
    out->push_back(address("2001:db8::5"));
    out->push_back(address("224.0.0.1"));  // a multicast entry a list may hold is never own
    if (generation > 0) {
        out->push_back(address("198.51.100.20"));
    }
    return true;
}

void own(galaxy::luma_net::OwnAddresses& list, const char* text, std::uint64_t now, bool want) {
    if (list.contains(address(text), now) != want) {
        std::fprintf(stderr, "FAILED: own(%s) at %llu should be %s\n", text, static_cast<unsigned long long>(now), want ? "true" : "false");
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
    // Own addresses by the list alone: a listed public address and its mapped form are own; an address not listed is
    // not, though nonlocal bind would let a socket bind to it; multicast and unspecified never are.
    {
        galaxy::luma_net::OwnAddresses list(listed);
        own(list, "203.0.113.9", 0, true);
        own(list, "::ffff:203.0.113.9", 10, true);
        own(list, "2001:db8::5", 20, true);
        own(list, "224.0.0.1", 30, false);
        own(list, "::", 40, false);
        if (reads != 1) {
            std::fprintf(stderr, "FAILED: the list was read %d times for questions it answered (want 1)\n", reads);
            ++failed;
        }
        // A miss within a second of the last reading compares only; after a second the list is read again.
        generation = 1;
        own(list, "198.51.100.20", 500, false);
        own(list, "198.51.100.7", 900, false);
        if (reads != 1) {
            std::fprintf(stderr, "FAILED: misses within a second read the list (%d reads, want 1)\n", reads);
            ++failed;
        }
        own(list, "198.51.100.20", 1500, true);
        if (reads != 2) {
            std::fprintf(stderr, "FAILED: a miss after a second did not read the list (%d reads, want 2)\n", reads);
            ++failed;
        }
        for (std::uint64_t t = 1600; t < 1900; ++t) {
            own(list, "198.51.100.7", t, false);  // a flood of outside peers: no further reading inside the second
        }
        if (reads != 2) {
            std::fprintf(stderr, "FAILED: a flood read the list %d times (want 2)\n", reads);
            ++failed;
        }
    }
    // The system's own list reads (its contents are this machine's).
    std::vector<sockaddr_storage> system;
    if (!galaxy::luma_net::interface_addresses(&system)) {
        std::fprintf(stderr, "FAILED: the system gave no interface list\n");
        ++failed;
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
