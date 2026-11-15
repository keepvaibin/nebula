// SPDX-License-Identifier: GPL-3.0-only
// The offline probe on Linux and Apple's systems (Transport::unreachable): the server's name looked up and one TCP
// connection opened, beside the request. A failure ends the start's wait for the check at once; success, or a proxy
// between, leaves the answer to the request.
#include "update_internal.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace galaxy::update::detail {

namespace {

// "https://host[:port]/…" to host and port.
bool split_url(const std::string& url, std::string& host, std::string& port) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) return false;
    const std::size_t start = scheme + 3u;
    const std::size_t end = url.find_first_of("/?#", start);
    const std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1u);
    } else {
        host = authority;
        port = url.compare(0, 8, "https://") == 0 ? "443" : "80";
    }
    return !host.empty() && !port.empty();
}

}  // namespace

// True when the server of `url` cannot be reached: its name does not resolve, or every address refuses or fails to
// connect within `connect_ms`. Called only without a proxy.
bool posix_unreachable(const std::string& url, int connect_ms) {
    std::string host, port;
    if (!split_url(url, host, port)) return false;
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0 || found == nullptr) return true;
    bool reached = false;
    for (addrinfo* address = found; address != nullptr && !reached; address = address->ai_next) {
        const int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int result = connect(fd, address->ai_addr, address->ai_addrlen);
        if (result == 0) {
            reached = true;
        } else if (errno == EINPROGRESS) {
            pollfd waiting{fd, POLLOUT, 0};
            if (poll(&waiting, 1, connect_ms) == 1) {
                int error = 0;
                socklen_t size = sizeof(error);
                reached = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
            } else {
                // No answer within connect_ms: not proof of offline (a slow network); the request decides.
                close(fd);
                freeaddrinfo(found);
                return false;
            }
        }
        close(fd);
    }
    freeaddrinfo(found);
    return !reached;
}

}  // namespace galaxy::update::detail
