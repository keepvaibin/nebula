// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's net services (contract services-net; plan 2.7), layer 1: `net.local`, TCP on this machine and the local
// network; layer 2: `net.internet`, TCP to the hosts a mod's luma.json names, by name (looked up on a thread of its
// own, one per lookup) or address, public addresses only. Non-blocking sockets: the game thread makes them (socket,
// connect, bind, listen return at once; an internet connect's sockets are made by the I/O thread once its name is
// looked up) and one I/O thread, started at the first socket, polls them, reads, writes and closes. Minor 1 adds UDP
// sockets (udp_open, udp_connect): datagrams in and out whole, on the same thread and under the same caps. The game
// thread never closes a system socket and the I/O thread never frees a Socket: the game thread frees one once the I/O
// thread closed it, its node is not queued and no lookup holds it, so a poll never meets a closed descriptor and a
// delivery never meets freed memory.

#include "galaxy/luma_net_service.h"

#include "galaxy/luma.h"
#include "galaxy/mods.h"

#include <luma_net.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace galaxy::luma_net {
namespace {

// ---- The system's sockets ------------------------------------------------------------------------------------------

#if defined(_WIN32)
using Sock = SOCKET;
const Sock kNoSock = INVALID_SOCKET;
using PollFd = WSAPOLLFD;
int system_error() noexcept { return WSAGetLastError(); }
bool would_block(int error) noexcept { return error == WSAEWOULDBLOCK; }
bool connecting(int error) noexcept { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }
bool address_in_use(int error) noexcept { return error == WSAEADDRINUSE || error == WSAEACCES; }
bool refused(int error) noexcept {
    return error == WSAECONNREFUSED || error == WSAENETUNREACH || error == WSAEHOSTUNREACH;
}
void close_system(Sock s) noexcept { closesocket(s); }
int poll_system(PollFd* fds, std::size_t count, int ms) noexcept { return WSAPoll(fds, static_cast<ULONG>(count), ms); }
bool make_nonblocking(Sock s) noexcept {
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
}
int send_system(Sock s, const char* data, std::size_t bytes) noexcept {
    return ::send(s, data, static_cast<int>(std::min<std::size_t>(bytes, 1u << 30u)), 0);
}
int receive_system(Sock s, char* data, std::size_t bytes) noexcept {
    return ::recv(s, data, static_cast<int>(std::min<std::size_t>(bytes, 1u << 30u)), 0);
}
int receive_from_system(Sock s, char* data, std::size_t bytes, sockaddr_storage* from, socklen_t* length) noexcept {
    return ::recvfrom(s, data, static_cast<int>(std::min<std::size_t>(bytes, 1u << 30u)), 0,
                      reinterpret_cast<sockaddr*>(from), length);
}
// A null `to`: the socket's own peer (udp_connect).
int send_to_system(Sock s, const char* data, std::size_t bytes, const sockaddr_storage* to, socklen_t length) noexcept {
    if (to == nullptr) {
        return ::send(s, data, static_cast<int>(bytes), 0);
    }
    return ::sendto(s, data, static_cast<int>(bytes), 0, reinterpret_cast<const sockaddr*>(to), length);
}
// A datagram socket's error that ends one datagram, not the socket (an earlier send's "port unreachable" answer).
bool datagram_lost(int error) noexcept {
    return error == WSAECONNRESET || error == WSAENETRESET || error == WSAEMSGSIZE || error == WSAEHOSTUNREACH ||
           error == WSAENETUNREACH;
}
bool system_ready() noexcept {
    static const bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}
#else
using Sock = int;
const Sock kNoSock = -1;
using PollFd = pollfd;
int system_error() noexcept { return errno; }
bool would_block(int error) noexcept { return error == EWOULDBLOCK || error == EAGAIN || error == EINTR; }
bool connecting(int error) noexcept { return error == EINPROGRESS || error == EINTR; }
bool address_in_use(int error) noexcept { return error == EADDRINUSE || error == EACCES; }
bool refused(int error) noexcept { return error == ECONNREFUSED || error == ENETUNREACH || error == EHOSTUNREACH; }
void close_system(Sock s) noexcept { ::close(s); }
int poll_system(PollFd* fds, std::size_t count, int ms) noexcept { return ::poll(fds, static_cast<nfds_t>(count), ms); }
bool make_nonblocking(Sock s) noexcept {
    const int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) != 0) {
        return false;
    }
#if defined(__APPLE__)
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));  // a peer gone is an error, never a signal
#endif
    return true;
}
int send_system(Sock s, const char* data, std::size_t bytes) noexcept {
#if defined(MSG_NOSIGNAL)
    return static_cast<int>(::send(s, data, std::min<std::size_t>(bytes, 1u << 30u), MSG_NOSIGNAL));
#else
    return static_cast<int>(::send(s, data, std::min<std::size_t>(bytes, 1u << 30u), 0));
#endif
}
int receive_system(Sock s, char* data, std::size_t bytes) noexcept {
    return static_cast<int>(::recv(s, data, std::min<std::size_t>(bytes, 1u << 30u), 0));
}
int receive_from_system(Sock s, char* data, std::size_t bytes, sockaddr_storage* from, socklen_t* length) noexcept {
    return static_cast<int>(
        ::recvfrom(s, data, std::min<std::size_t>(bytes, 1u << 30u), 0, reinterpret_cast<sockaddr*>(from), length));
}
// A null `to`: the socket's own peer (udp_connect).
int send_to_system(Sock s, const char* data, std::size_t bytes, const sockaddr_storage* to, socklen_t length) noexcept {
    if (to == nullptr) {
        return static_cast<int>(::send(s, data, bytes, 0));
    }
    return static_cast<int>(::sendto(s, data, bytes, 0, reinterpret_cast<const sockaddr*>(to), length));
}
// A datagram socket's error that ends one datagram, not the socket (an earlier send's "port unreachable" answer).
bool datagram_lost(int error) noexcept {
    return error == ECONNREFUSED || error == EMSGSIZE || error == EHOSTUNREACH || error == ENETUNREACH;
}
bool system_ready() noexcept { return true; }
#endif

// ---- What a mod may reach (layer 1: this machine and the local network, by address) --------------------------------

bool local_v4(const in_addr& address) noexcept {
    const std::uint32_t a = ntohl(address.s_addr);
    return (a >> 24u) == 127u || (a >> 24u) == 10u || (a >> 20u) == 0xAC1u /* 172.16/12 */ ||
           (a >> 16u) == 0xC0A8u /* 192.168/16 */ || (a >> 16u) == 0xA9FEu /* 169.254/16 */;
}

bool local_v6(const in6_addr& address) noexcept {
    const unsigned char* b = address.s6_addr;
    static const unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    if (std::memcmp(b, loopback, 16) == 0) {
        return true;
    }
    return (b[0] & 0xFEu) == 0xFCu /* fc00::/7 */ || (b[0] == 0xFEu && (b[1] & 0xC0u) == 0x80u) /* fe80::/10 */;
}

// The address of `host` (text, never a name looked up: layer 1) and port into *out; false when it is not local.
bool local_address(const char* host, std::uint16_t port, sockaddr_storage* out, socklen_t* length) noexcept {
    std::memset(out, 0, sizeof(*out));
    std::string_view text(host);
    if (text == "localhost") {
        text = "127.0.0.1";
    }
    const std::string copy(text);
    sockaddr_in v4{};
    if (inet_pton(AF_INET, copy.c_str(), &v4.sin_addr) == 1) {
        if (!local_v4(v4.sin_addr)) {
            return false;
        }
        v4.sin_family = AF_INET;
        v4.sin_port = htons(port);
        std::memcpy(out, &v4, sizeof(v4));
        *length = static_cast<socklen_t>(sizeof(v4));
        return true;
    }
    sockaddr_in6 v6{};
    if (inet_pton(AF_INET6, copy.c_str(), &v6.sin6_addr) == 1) {
        if (!local_v6(v6.sin6_addr)) {
            return false;
        }
        v6.sin6_family = AF_INET6;
        v6.sin6_port = htons(port);
        std::memcpy(out, &v6, sizeof(v6));
        *length = static_cast<socklen_t>(sizeof(v6));
        return true;
    }
    return false;
}

// ---- Sockets -------------------------------------------------------------------------------------------------------

constexpr auto kConnectTime = std::chrono::seconds(10);
// A closed socket's queued bytes go for at most this long; a peer that never reads cannot hold the descriptor (and the
// mod's queued budget) past it.
constexpr auto kCloseTime = std::chrono::seconds(10);
constexpr int kPollMs = 10;  // the I/O thread's wait while it holds sockets (a socket made meanwhile waits at most this)

std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
void (*g_log)(const char*) = nullptr;

enum class Kind : std::uint8_t { Resolving, Connecting, Open, Listener, Udp };

// One datagram: received (its sender) or queued (its address; unused on a udp_connect socket).
struct Datagram {
    sockaddr_storage address{};
    socklen_t length = 0;
    std::string bytes;
};
constexpr std::size_t kDatagramsWaitingMost = 4096;  // datagrams a socket holds not taken (0-byte ones count too)

struct Socket;
struct ModState;

struct Node {
    LumaEventNode event;  // first, and both plain members: the kernel's node is the Node
    Socket* socket;
};

struct Socket {
    Node node{};
    ModState* owner = nullptr;  // g_lock; null once the mod is gone
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    Kind kind = Kind::Connecting;
    Sock sock = kNoSock;         // closed by the I/O thread only
    std::uint16_t port = 0;      // a listener's
    std::chrono::steady_clock::time_point started{};  // a connect's start; from close() on, the close's
    // What the I/O thread found, for the next delivery (g_lock).
    bool posted = false;         // the node is queued with the kernel
    bool connected = false;      // CONNECTED to give
    std::int32_t failed = LUMA_OK;  // ERROR to give (its status)
    bool closed_event = false;   // CLOSED to give
    std::int32_t closed_status = LUMA_OK;
    std::vector<Socket*> accepted;  // ACCEPTED to give, in order
    std::string received;        // bytes read, not yet taken
    std::string outgoing;        // bytes queued to send
    std::uint64_t sent_since = 0;  // sent since the last SENT
    bool sent_event = false;     // SENT to give
    bool peer_closed = false;    // the other side ended (or broke): nothing more is read
    bool closing = false;        // the mod closed it (or its mod is gone): no event from now on
    bool finished = false;       // its last event (CLOSED or ERROR) given: the handle is closed
    bool system_closed = false;  // the I/O thread closed `sock`
    // net.internet's (layer 2).
    bool internet = false;       // made by net.internet: its calls only, its name in the kernel's calls
    bool resolving = false;      // its lookup thread still holds it: never freed before that thread lets go
    bool looked_up = false;      // the lookup's answer is in: `addresses` and `lookup_status`
    std::int32_t lookup_status = LUMA_OK;
    std::vector<sockaddr_storage> addresses;  // public ones, in the lookup's order; tried one after another
    std::size_t next_address = 0;
    // UDP's (minor 1).
    bool udp = false;            // a datagram socket: datagrams in and out, never a stream
    bool tied = false;           // made by udp_connect: one peer, `send`; else udp_open's: `send_to`
    std::deque<Datagram> datagrams_in;   // received, not taken: their bytes count against LUMA_NET_RECEIVED_MOST
    std::size_t datagrams_in_bytes = 0;
    std::deque<Datagram> datagrams_out;  // queued, not sent: their bytes count against the mod's queue
    std::size_t datagrams_out_bytes = 0;
};

struct ModState {
    const LumaMod* mod = nullptr;
    std::uint32_t open = 0;  // g_lock: sockets counted against LUMA_NET_SOCKETS_MOST
    std::vector<std::string> hosts;  // game thread: luma.json's net.internet hosts, in lower case
};

std::mutex g_lock;  // every Socket's fields but node.event's (the kernel's), g_sockets, ModState::open
std::vector<Socket*> g_sockets;  // g_lock
std::vector<std::unique_ptr<ModState>> g_mods;  // game thread
std::atomic<LumaHandle> g_next_handle{1};
const Socket* g_in_data = nullptr;  // game thread: the socket whose DATA call runs

// The I/O thread: made at the first socket and never freed (a thread still polling at the process's end meets no
// destroyed lock).
struct Io {
    std::condition_variable ready;  // with g_lock: a socket exists
    bool started = false;
};
Io* g_io = nullptr;  // game thread makes it

void io_loop();

bool io_started() {
    if (g_io != nullptr) {
        return g_io->started;
    }
    g_io = new (std::nothrow) Io();
    if (g_io == nullptr) {
        return false;
    }
    try {
        std::thread(io_loop).detach();
        g_io->started = true;
    } catch (...) {
        if (g_log != nullptr) {
            g_log("LumaCore net: the I/O thread could not start; every socket answers LUMA_E_IO");
        }
    }
    return g_io->started;
}

ModState* state_of(const LumaMod* mod) {
    for (const std::unique_ptr<ModState>& state : g_mods) {
        if (state->mod == mod) {
            return state.get();
        }
    }
    return nullptr;
}

void deliver(LumaEventNode* node);
void drop(LumaEventNode* node);

// With g_lock: queue the socket's node for its mod's next frame (once).
void post_locked(Socket* socket) {
    if (socket->posted || socket->owner == nullptr) {
        return;
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel != nullptr && kernel->post(&socket->node.event) == LUMA_OK) {
        socket->posted = true;
    }
}

// With g_lock: the socket leaves its mod's count (once).
void uncount_locked(Socket* socket) {
    if (socket->owner != nullptr && socket->owner->open > 0u && !socket->finished) {
        socket->owner->open--;
    }
}

// Game thread, with g_lock: frees the sockets nothing uses any more (closed by the I/O thread, not queued, and closed
// by the mod, finished or without a mod).
void reap_locked() {
    for (auto it = g_sockets.begin(); it != g_sockets.end();) {
        Socket* socket = *it;
        if (socket->system_closed && !socket->posted && !socket->resolving &&
            (socket->closing || socket->finished || socket->owner == nullptr)) {
            it = g_sockets.erase(it);
            delete socket;
        } else {
            ++it;
        }
    }
}

// ---- The I/O thread ------------------------------------------------------------------------------------------------

std::int32_t connect_status(int error) noexcept {
    return refused(error) ? LUMA_E_UNREACHABLE : LUMA_E_IO;
}

// With g_lock, I/O thread: ends the system socket (once).
void close_locked(Socket* socket) {
    if (!socket->system_closed) {
        if (socket->sock != kNoSock) {
            close_system(socket->sock);
        }
        socket->sock = kNoSock;
        socket->system_closed = true;
    }
}

// With g_lock, I/O thread: an internet socket's connect to its next looked-up address, the last one's system socket
// closed first. True: a connect runs (or ended at once with an error the poll shows); false: no address is left.
bool start_next_locked(Socket* socket) {
    while (socket->next_address < socket->addresses.size()) {
        const sockaddr_storage& address = socket->addresses[socket->next_address++];
        if (socket->sock != kNoSock) {
            close_system(socket->sock);
            socket->sock = kNoSock;
        }
        const Sock sock = ::socket(address.ss_family, socket->udp ? SOCK_DGRAM : SOCK_STREAM,
                                   socket->udp ? IPPROTO_UDP : IPPROTO_TCP);
        if (sock == kNoSock) {
            continue;
        }
        if (!make_nonblocking(sock)) {
            close_system(sock);
            continue;
        }
        if (!socket->udp) {
            int on = 1;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
        }
        socket->sock = sock;
        const socklen_t length =
            static_cast<socklen_t>(address.ss_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
        if (::connect(sock, reinterpret_cast<const sockaddr*>(&address), length) == 0 || connecting(system_error())) {
            return true;
        }
    }
    return false;
}

// I/O thread: room for one datagram as it is read (made at the first, never freed, as g_io; not on the stack, R9).
std::vector<char>* g_datagram = nullptr;
constexpr std::size_t kDatagramRoom = 65536;

// With g_lock, I/O thread: a UDP socket's turn: datagrams read while there is room, the queued ones sent.
void udp_turn_locked(Socket* socket, short revents) {
    if (!socket->closing && (revents & (POLLIN | POLLERR)) != 0) {
        if (g_datagram == nullptr) {
            g_datagram = new (std::nothrow) std::vector<char>();
        }
        if (g_datagram != nullptr && g_datagram->size() < kDatagramRoom) {
            try {
                g_datagram->resize(kDatagramRoom);
            } catch (...) {
                // No memory now: the datagrams stay in the system's buffer for a later turn.
            }
        }
        for (int read = 0; g_datagram != nullptr && g_datagram->size() >= kDatagramRoom && read < 256 &&
                           socket->datagrams_in_bytes < LUMA_NET_RECEIVED_MOST &&
                           socket->datagrams_in.size() < kDatagramsWaitingMost;
             read++) {
            Datagram datagram;
            datagram.length = static_cast<socklen_t>(sizeof(datagram.address));
            const int got = receive_from_system(socket->sock, g_datagram->data(), kDatagramRoom, &datagram.address,
                                                &datagram.length);
            if (got < 0) {
                if (datagram_lost(system_error())) {
                    continue;  // an answer to an earlier send ("port unreachable"): UDP tells the mod nothing
                }
                break;  // none waiting (or an error the next poll shows again)
            }
            try {
                datagram.bytes.assign(g_datagram->data(), static_cast<std::size_t>(got));
                socket->datagrams_in.push_back(std::move(datagram));
            } catch (...) {
                break;  // no memory: this one is lost, as the network may lose it
            }
            socket->datagrams_in_bytes += static_cast<std::size_t>(got);
        }
        if (!socket->datagrams_in.empty()) {
            post_locked(socket);
        }
    }
    if (!socket->datagrams_out.empty() && (revents & POLLOUT) != 0) {
        while (!socket->datagrams_out.empty()) {
            const Datagram& datagram = socket->datagrams_out.front();
            const int put =
                send_to_system(socket->sock, datagram.bytes.data(), datagram.bytes.size(),
                               socket->tied ? nullptr : &datagram.address, socket->tied ? 0 : datagram.length);
            if (put < 0 && would_block(system_error())) {
                break;  // the system's buffer is full: the next POLLOUT
            }
            // Sent, or refused by the system (no route, too large for the path): either way it left the queue, and
            // UDP never tells whether a datagram arrived.
            socket->sent_since += datagram.bytes.size();
            socket->datagrams_out_bytes -= datagram.bytes.size();
            socket->datagrams_out.pop_front();
        }
        if (socket->datagrams_out.empty() && !socket->closing) {
            socket->sent_event = true;
            post_locked(socket);
        }
    }
    if (socket->closing &&
        (socket->datagrams_out.empty() || std::chrono::steady_clock::now() - socket->started > kCloseTime)) {
        close_locked(socket);  // the mod closed it: what it queued went first (or kCloseTime passed)
        socket->datagrams_out.clear();
        socket->datagrams_out_bytes = 0;
    }
}

// With g_lock, I/O thread: one socket's turn after the poll (`revents` its poll result, 0 when not polled).
void turn_locked(Socket* socket, short revents) {
    if (socket->system_closed) {
        return;
    }
    if (socket->owner == nullptr) {
        close_locked(socket);  // its mod is gone: nothing is flushed for nobody
        return;
    }
    switch (socket->kind) {
    case Kind::Resolving: {
        if (socket->closing) {
            close_locked(socket);  // no system socket yet: nothing to send; freed once its lookup lets go
            return;
        }
        if (!socket->looked_up) {
            if (std::chrono::steady_clock::now() - socket->started > kConnectTime) {
                close_locked(socket);
                socket->failed = LUMA_E_TIMEOUT;
                post_locked(socket);
            }
            return;
        }
        if (socket->lookup_status != LUMA_OK || !start_next_locked(socket)) {
            close_locked(socket);
            socket->failed = socket->lookup_status != LUMA_OK ? socket->lookup_status : LUMA_E_UNREACHABLE;
            post_locked(socket);
            return;
        }
        socket->kind = Kind::Connecting;  // polled from the next turn
        return;
    }
    case Kind::Connecting: {
        if (socket->failed != LUMA_OK) {
            close_locked(socket);  // refused at the connect itself: given at the next frame
            if (!socket->closing) {
                post_locked(socket);
            }
            return;
        }
        if (revents == 0) {
            if (std::chrono::steady_clock::now() - socket->started > kConnectTime) {
                close_locked(socket);
                if (!socket->closing) {
                    socket->failed = LUMA_E_TIMEOUT;
                    post_locked(socket);
                }
            }
            return;
        }
        int error = 0;
        socklen_t length = sizeof(error);
        getsockopt(socket->sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
        if (error != 0 || ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (revents & POLLOUT) == 0)) {
            if (socket->internet && !socket->closing && start_next_locked(socket)) {
                return;  // the name's next address, within the same 10 s
            }
            close_locked(socket);
            if (!socket->closing) {
                socket->failed = error != 0 ? connect_status(error) : LUMA_E_UNREACHABLE;
                post_locked(socket);
            }
            return;
        }
        socket->kind = socket->udp ? Kind::Udp : Kind::Open;  // a UDP socket's connect only sets its peer
        if (socket->closing) {
            break;  // closed by the mod while connecting: what it queued (nothing) is flushed below
        }
        socket->connected = true;
        post_locked(socket);
        break;
    }
    case Kind::Listener: {
        if ((revents & POLLIN) == 0) {
            return;
        }
        for (;;) {
            sockaddr_storage from{};
            socklen_t length = sizeof(from);
            const Sock got = ::accept(socket->sock, reinterpret_cast<sockaddr*>(&from), &length);
            if (got == kNoSock) {
                return;  // none waiting (or an error the next poll shows again)
            }
            if (socket->closing || socket->owner->open >= LUMA_NET_SOCKETS_MOST || !make_nonblocking(got)) {
                close_system(got);  // full: the other side sees its connection end at once
                continue;
            }
            Socket* fresh = new (std::nothrow) Socket();
            if (fresh == nullptr) {
                close_system(got);
                continue;
            }
            fresh->node.event.mod = socket->node.event.mod;
            fresh->node.event.deliver = deliver;
            fresh->node.event.drop = drop;
            fresh->node.socket = fresh;
            fresh->owner = socket->owner;
            fresh->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
            fresh->fn = socket->fn;
            fresh->user = socket->user;
            fresh->kind = Kind::Open;
            fresh->sock = got;
            try {
                g_sockets.push_back(fresh);
                socket->accepted.push_back(fresh);
            } catch (...) {
                if (!g_sockets.empty() && g_sockets.back() == fresh) {
                    g_sockets.pop_back();
                }
                close_system(got);
                delete fresh;
                continue;
            }
            socket->owner->open++;
            post_locked(socket);
        }
    }
    case Kind::Open:
    case Kind::Udp:
        break;
    }
    if (socket->kind == Kind::Udp) {
        udp_turn_locked(socket, revents);
        return;
    }
    if (socket->kind != Kind::Open) {
        return;
    }
    // Open: read while there is room, write what is queued.
    if (!socket->peer_closed && !socket->closing && (revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        while (socket->received.size() < LUMA_NET_RECEIVED_MOST) {
            // Read straight into the socket's buffer (no copy on the stack, R9): grown by up to 64 KiB, cut back after.
            const std::size_t before = socket->received.size();
            const std::size_t room = std::min<std::size_t>(64u << 10u, LUMA_NET_RECEIVED_MOST - before);
            try {
                socket->received.resize(before + room);
            } catch (...) {
                break;  // no memory now: the bytes stay in the system's buffer for a later turn
            }
            const int got = receive_system(socket->sock, socket->received.data() + before, room);
            socket->received.resize(before + (got > 0 ? static_cast<std::size_t>(got) : 0u));
            if (got > 0) {
                continue;
            }
            if (got == 0) {
                socket->peer_closed = true;
                socket->closed_status = LUMA_OK;
            } else if (!would_block(system_error())) {
                socket->peer_closed = true;
                socket->closed_status = LUMA_E_IO;
            }
            break;
        }
        if (!socket->received.empty() || socket->peer_closed) {
            post_locked(socket);
        }
    }
    if (!socket->outgoing.empty() && (revents & POLLOUT) != 0) {
        std::size_t done = 0;
        while (done < socket->outgoing.size()) {
            const int put = send_system(socket->sock, socket->outgoing.data() + done, socket->outgoing.size() - done);
            if (put > 0) {
                done += static_cast<std::size_t>(put);
                continue;
            }
            if (put < 0 && !would_block(system_error())) {
                socket->peer_closed = true;
                socket->closed_status = LUMA_E_IO;
                socket->outgoing.clear();
                done = 0;
            }
            break;
        }
        if (done > 0) {
            socket->outgoing.erase(0, done);
            socket->sent_since += done;
        }
        if (socket->outgoing.empty() && socket->sent_since > 0u && !socket->closing) {
            socket->sent_event = true;
            post_locked(socket);
        }
    }
    if (socket->closing &&
        (socket->outgoing.empty() || std::chrono::steady_clock::now() - socket->started > kCloseTime)) {
        close_locked(socket);  // the mod closed it: what it queued went first (or kCloseTime passed)
        socket->outgoing.clear();
    } else if (socket->peer_closed && socket->received.empty() && !socket->closing) {
        close_locked(socket);
        socket->closed_event = true;
        post_locked(socket);
    } else if (socket->peer_closed && socket->closing) {
        close_locked(socket);
    }
}

void io_loop() {
    std::vector<Socket*> polled;
    std::vector<PollFd> fds;
    for (;;) {
        polled.clear();
        fds.clear();
        {
            std::unique_lock<std::mutex> lock(g_lock);
            g_io->ready.wait(lock, [] {
                return std::any_of(g_sockets.begin(), g_sockets.end(), [](const Socket* s) { return !s->system_closed; });
            });
            try {
                for (Socket* socket : g_sockets) {
                    if (socket->system_closed) {
                        continue;
                    }
                    PollFd fd{};
                    fd.fd = socket->sock;
                    switch (socket->kind) {
                    case Kind::Resolving:
                        break;  // no system socket yet: its turn comes below
                    case Kind::Connecting:
                        fd.events = POLLOUT;
                        break;
                    case Kind::Listener:
                        fd.events = POLLIN;
                        break;
                    case Kind::Open:
                        fd.events = static_cast<short>(
                            (!socket->peer_closed && socket->received.size() < LUMA_NET_RECEIVED_MOST ? POLLIN : 0) |
                            (!socket->outgoing.empty() ? POLLOUT : 0));
                        break;
                    case Kind::Udp:
                        fd.events = static_cast<short>(
                            (!socket->closing && socket->datagrams_in_bytes < LUMA_NET_RECEIVED_MOST &&
                                     socket->datagrams_in.size() < kDatagramsWaitingMost
                                 ? POLLIN
                                 : 0) |
                            (!socket->datagrams_out.empty() ? POLLOUT : 0));
                        break;
                    }
                    if (fd.events == 0) {
                        continue;  // nothing to wait for (its turn comes below); WSAPoll takes no empty entry
                    }
                    polled.push_back(socket);
                    fds.push_back(fd);
                }
            } catch (...) {
                polled.clear();
                fds.clear();
            }
        }
        // Outside the lock: the descriptors stay open (only this thread closes them).
        if (!fds.empty()) {
            poll_system(fds.data(), fds.size(), kPollMs);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        }
        const std::lock_guard<std::mutex> lock(g_lock);
        for (std::size_t i = 0; i < polled.size(); i++) {
            // A socket the game thread freed meanwhile is gone from g_sockets (it was closed by this thread first).
            if (std::find(g_sockets.begin(), g_sockets.end(), polled[i]) == g_sockets.end()) {
                continue;
            }
            turn_locked(polled[i], fds[i].revents);
        }
        // Sockets made since the poll's list (none polled yet) and those the mod closed with nothing queued.
        for (Socket* socket : g_sockets) {
            if (std::find(polled.begin(), polled.end(), socket) == polled.end()) {
                turn_locked(socket, 0);
            }
        }
    }
}

// ---- Delivery (game thread) ----------------------------------------------------------------------------------------

void call(const LumaMod* mod, const char* service, LumaEventFn fn, void* user, std::uint32_t kind, LumaHandle handle,
          std::int32_t status, std::uint64_t value) {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel == nullptr) {
        return;
    }
    LumaEvent event{};
    event.size = sizeof(event);
    event.kind = kind;
    event.status = status;
    event.handle = handle;
    event.value = value;
    kernel->call(mod, service, fn, user, &event);
}

void deliver(LumaEventNode* node) {
    Socket* const socket = reinterpret_cast<Node*>(node)->socket;
    const LumaMod* const mod = node->mod;
    bool connected = false;
    std::int32_t failed = LUMA_OK;
    std::vector<LumaHandle> accepted;
    bool data = false;
    std::uint64_t datagrams = 0;
    bool sent_given = false;
    std::uint64_t sent = 0;
    bool closed = false;
    std::int32_t closed_status = LUMA_OK;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    LumaHandle handle = 0;
    const char* const service = socket->internet ? LUMA_NET_INTERNET_NAME : LUMA_NET_LOCAL_NAME;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        socket->posted = false;
        if (socket->closing || socket->finished || socket->owner == nullptr) {
            reap_locked();
            return;
        }
        fn = socket->fn;
        user = socket->user;
        handle = socket->handle;
        connected = std::exchange(socket->connected, false);
        failed = std::exchange(socket->failed, LUMA_OK);
        try {
            for (Socket* fresh : socket->accepted) {
                accepted.push_back(fresh->handle);
            }
        } catch (...) {
        }
        socket->accepted.clear();
        data = !socket->received.empty();
        datagrams = socket->datagrams_in.size();
        if (std::exchange(socket->sent_event, false)) {
            sent_given = true;  // a UDP queue of 0-byte datagrams gives SENT with value 0
            sent = std::exchange(socket->sent_since, 0u);
        }
        closed = std::exchange(socket->closed_event, false);
        closed_status = socket->closed_status;
        if (failed != LUMA_OK || (closed && !data)) {
            uncount_locked(socket);
            socket->finished = true;  // the handle is closed after this event (header)
        }
    }
    const auto open = [socket] {
        const std::lock_guard<std::mutex> lock(g_lock);
        return !socket->closing;  // the mod may close it inside a call
    };
    if (connected) {
        call(mod, service, fn, user, LUMA_NET_EVENT_CONNECTED, handle, LUMA_OK, 0u);
    }
    for (const LumaHandle fresh : accepted) {
        if (!open()) {
            break;
        }
        call(mod, service, fn, user, LUMA_NET_EVENT_ACCEPTED, handle, LUMA_OK, fresh);
    }
    if (data && open()) {
        std::uint64_t waiting = 0;
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            waiting = socket->received.size();
        }
        g_in_data = socket;
        call(mod, service, fn, user, LUMA_NET_EVENT_DATA, handle, LUMA_OK, waiting);
        g_in_data = nullptr;
    }
    if (datagrams > 0u && open()) {
        g_in_data = socket;
        call(mod, service, fn, user, LUMA_NET_EVENT_DATAGRAM, handle, LUMA_OK, datagrams);
        g_in_data = nullptr;
    }
    if (sent_given && (sent > 0u || socket->udp) && open()) {
        call(mod, service, fn, user, LUMA_NET_EVENT_SENT, handle, LUMA_OK, sent);
    }
    if (failed != LUMA_OK) {
        call(mod, service, fn, user, LUMA_NET_EVENT_ERROR, handle, failed, 0u);
    } else if (closed && !data) {
        call(mod, service, fn, user, LUMA_NET_EVENT_CLOSED, handle, closed_status, 0u);
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    if (!socket->closing && !socket->finished) {
        if (closed && data) {
            socket->closed_event = true;  // the bytes first (header): CLOSED once they are taken
        }
        if (!socket->received.empty() || !socket->datagrams_in.empty() || socket->closed_event) {
            post_locked(socket);  // bytes or datagrams left, or the CLOSED after them: a later frame
        }
    }
    reap_locked();
}

void drop(LumaEventNode* node) {
    Socket* const socket = reinterpret_cast<Node*>(node)->socket;
    const std::lock_guard<std::mutex> lock(g_lock);
    socket->posted = false;
    reap_locked();
}

// ---- The calls (game thread) ---------------------------------------------------------------------------------------

void report(const LumaMod* mod, const char* where, std::uint32_t code, const char* a = nullptr) noexcept {
    const char* args[1] = {a};
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, code, args, a == nullptr ? 0u : 1u);
    }
}

std::int32_t enter(const LumaMod* mod, const char* where) noexcept {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel == nullptr) {
        return LUMA_E_CANCELLED;
    }
    if (mod == nullptr || kernel->mod_id(mod) == nullptr) {
        kernel->unknown_mod(where);
        return LUMA_E_ARGUMENT;
    }
    if (kernel->mod_live(mod) == 0) {
        return LUMA_E_CANCELLED;
    }
    if (kernel->game_thread() == 0) {
        report(mod, where, 100u);  // Service.Thread
        return LUMA_E_THREAD;
    }
    return LUMA_OK;
}

std::int32_t argument(const LumaMod* mod, const char* where, const char* name) noexcept {
    report(mod, where, 101u, name);  // Service.Argument
    return LUMA_E_ARGUMENT;
}

std::int32_t bad_handle(const LumaMod* mod, const char* where) noexcept {
    report(mod, where, 102u);  // Service.Handle
    return LUMA_E_HANDLE;
}

// With g_lock: this mod's open socket of that handle (any kind) made by that service, or null.
Socket* find_locked(const ModState* state, LumaHandle handle, bool internet = false) {
    for (Socket* socket : g_sockets) {
        if (socket->owner == state && socket->handle == handle && socket->internet == internet && !socket->closing &&
            !socket->finished) {
            return socket;
        }
    }
    return nullptr;
}

// A new Socket for the mod, counted, its system socket made non-blocking; LUMA_OK or the status to return.
std::int32_t make(const LumaMod* mod, const char* where, ModState* state, int family, LumaEventFn fn, void* user,
                  Socket** out, bool udp = false) {
    if (!system_ready() || !io_started()) {
        return LUMA_E_IO;
    }
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        if (state->open >= LUMA_NET_SOCKETS_MOST) {
            report(mod, where, 104u, "16");  // Service.Limit
            return LUMA_E_LIMIT;
        }
    }
    const Sock sock = ::socket(family, udp ? SOCK_DGRAM : SOCK_STREAM, udp ? IPPROTO_UDP : IPPROTO_TCP);
    if (sock == kNoSock) {
        return LUMA_E_IO;
    }
    if (!make_nonblocking(sock)) {
        close_system(sock);
        return LUMA_E_IO;
    }
    if (!udp) {
        int on = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));  // small messages
    }
    Socket* socket = new (std::nothrow) Socket();
    if (socket == nullptr) {
        close_system(sock);
        return LUMA_E_MEMORY;
    }
    socket->node.event.mod = mod;
    socket->node.event.deliver = deliver;
    socket->node.event.drop = drop;
    socket->node.socket = socket;
    socket->owner = state;
    socket->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
    socket->fn = fn;
    socket->user = user;
    socket->sock = sock;
    socket->udp = udp;
    *out = socket;
    return LUMA_OK;
}

// With the Socket made: it joins the polled list (the I/O thread owns its descriptor from here).
std::int32_t join(Socket* socket, LumaHandle* out) {
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        try {
            g_sockets.push_back(socket);
        } catch (...) {
            close_system(socket->sock);
            delete socket;
            return LUMA_E_MEMORY;
        }
        socket->owner->open++;
    }
    g_io->ready.notify_one();
    *out = socket->handle;
    return LUMA_OK;
}

// tcp_connect and udp_connect of net.local: a stream, or a datagram socket tied to one peer.
std::int32_t local_connect(const LumaMod* mod, const char* where, bool udp, const char* host, std::uint16_t port,
                           LumaEventFn fn, void* user, LumaHandle* out) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (host == nullptr) {
        return argument(mod, where, "host");
    }
    if (port == 0u) {
        return argument(mod, where, "port");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        sockaddr_storage address{};
        socklen_t length = 0;
        if (!local_address(host, port, &address, &length)) {
            return argument(mod, where, "host");  // not this machine's or the local network's (layer 1: by address)
        }
        ModState* state = state_of(mod);
        if (state == nullptr) {
            return LUMA_E_CANCELLED;
        }
        Socket* socket = nullptr;
        if (const std::int32_t status = make(mod, where, state, address.ss_family, fn, user, &socket, udp);
            status != LUMA_OK) {
            return status;
        }
        socket->tied = udp;
        socket->kind = Kind::Connecting;  // a UDP connect only sets the peer: CONNECTED at the I/O thread's next turn
        socket->started = std::chrono::steady_clock::now();
        if (::connect(socket->sock, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
            const int error = system_error();
            if (!connecting(error)) {
                // Refused at once (loopback with nobody listening, on some systems): an ERROR at the next frame.
                socket->failed = connect_status(error);
            }
        }
        const std::int32_t status = join(socket, out);
        return status != LUMA_OK ? status : LUMA_PENDING;  // a failed connect: the I/O thread closes it and posts ERROR
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t net_tcp_connect(const LumaMod* mod, const char* host, std::uint16_t port, LumaEventFn fn, void* user,
                             LumaHandle* out) noexcept {
    return local_connect(mod, "net.local.tcp_connect", false, host, port, fn, user, out);
}
std::int32_t net_udp_connect(const LumaMod* mod, const char* host, std::uint16_t port, LumaEventFn fn, void* user,
                             LumaHandle* out) noexcept {
    return local_connect(mod, "net.local.udp_connect", true, host, port, fn, user, out);
}

std::int32_t net_udp_open(const LumaMod* mod, std::uint16_t port, std::uint32_t local_network, LumaEventFn fn,
                          void* user, LumaHandle* out) noexcept {
    static constexpr const char* where = "net.local.udp_open";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (local_network > 1u) {
        return argument(mod, where, "local_network");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return LUMA_E_CANCELLED;
    }
    Socket* socket = nullptr;
    if (const std::int32_t status = make(mod, where, state, AF_INET, fn, user, &socket, true); status != LUMA_OK) {
        return status;
    }
#if defined(_WIN32)
    int exclusive = 1;  // no other program may take the same port beside it
    setsockopt(socket->sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
               sizeof(exclusive));
#endif
    // Elsewhere no SO_REUSEADDR: a UDP port has no wait after close, and two sockets on one port would split its
    // datagrams between them.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(local_network != 0u ? INADDR_ANY : INADDR_LOOPBACK);
    if (::bind(socket->sock, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const int error = system_error();
        close_system(socket->sock);
        delete socket;
        return address_in_use(error) ? LUMA_E_EXISTS : LUMA_E_IO;
    }
    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    if (::getsockname(socket->sock, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
        socket->port = ntohs(bound.sin_port);
    }
    socket->kind = Kind::Udp;
    return join(socket, out);
}

std::int32_t net_tcp_listen(const LumaMod* mod, std::uint16_t port, std::uint32_t local_network, LumaEventFn fn,
                            void* user, LumaHandle* out) noexcept {
    static constexpr const char* where = "net.local.tcp_listen";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (local_network > 1u) {
        return argument(mod, where, "local_network");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return LUMA_E_CANCELLED;
    }
    Socket* socket = nullptr;
    if (const std::int32_t status = make(mod, where, state, AF_INET, fn, user, &socket); status != LUMA_OK) {
        return status;
    }
#if defined(_WIN32)
    int exclusive = 1;  // no other program may take the same port beside it
    setsockopt(socket->sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
    int reuse = 1;  // a port this game used a moment ago is free again at once
    setsockopt(socket->sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(local_network != 0u ? INADDR_ANY : INADDR_LOOPBACK);
    if (::bind(socket->sock, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(socket->sock, 16) != 0) {
        const int error = system_error();
        close_system(socket->sock);
        delete socket;
        return address_in_use(error) ? LUMA_E_EXISTS : LUMA_E_IO;
    }
    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    if (::getsockname(socket->sock, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
        socket->port = ntohs(bound.sin_port);
    }
    socket->kind = Kind::Listener;
    return join(socket, out);
}

std::int32_t port_on(const LumaMod* mod, const char* where, bool internet, LumaHandle listener,
                     std::uint16_t* out) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    const ModState* state = state_of(mod);
    const std::lock_guard<std::mutex> lock(g_lock);
    const Socket* socket = state != nullptr ? find_locked(state, listener, internet) : nullptr;
    if (socket == nullptr || (socket->kind != Kind::Listener && !(socket->udp && !socket->tied))) {
        return bad_handle(mod, where);  // a listener's or a udp_open socket's
    }
    *out = socket->port;
    return LUMA_OK;
}

// With g_lock: LUMA_OK when `bytes` more fit the mod's send queue (streams and datagrams together), else LIMIT,
// reported.
std::int32_t room_locked(const LumaMod* mod, const char* where, const ModState* state, std::uint64_t bytes) noexcept {
    std::uint64_t queued = 0;
    for (const Socket* other : g_sockets) {
        if (other->owner == state) {
            queued += other->outgoing.size() + other->datagrams_out_bytes;
        }
    }
    if (bytes > LUMA_NET_SEND_QUEUED_MOST || queued + bytes > LUMA_NET_SEND_QUEUED_MOST) {
        report(mod, where, 104u, "4194304");  // Service.Limit
        return LUMA_E_LIMIT;
    }
    return LUMA_OK;
}

std::int32_t send_on(const LumaMod* mod, const char* where, bool internet, LumaHandle handle, const void* data,
                     std::uint64_t bytes) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (data == nullptr && bytes != 0u) {
        return argument(mod, where, "data");
    }
    const ModState* state = state_of(mod);
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        Socket* socket = state != nullptr ? find_locked(state, handle, internet) : nullptr;
        if (socket == nullptr || socket->kind == Kind::Listener || (socket->udp && !socket->tied)) {
            return bad_handle(mod, where);  // a udp_open socket sends with send_to
        }
        if (socket->kind == Kind::Resolving || socket->kind == Kind::Connecting || socket->peer_closed ||
            socket->system_closed) {
            return LUMA_E_CLOSED;  // an answer (header)
        }
        if (socket->udp && bytes > LUMA_NET_DATAGRAM_MOST) {
            return LUMA_E_TOO_LARGE;  // an answer (header)
        }
        if (bytes == 0u && !socket->udp) {
            return LUMA_OK;  // a stream sends nothing; a 0-byte datagram is a datagram
        }
        if (const std::int32_t status = room_locked(mod, where, state, bytes); status != LUMA_OK) {
            return status;
        }
        try {
            if (socket->udp) {
                Datagram datagram;
                datagram.bytes.assign(static_cast<const char*>(data), static_cast<std::size_t>(bytes));
                socket->datagrams_out.push_back(std::move(datagram));
                socket->datagrams_out_bytes += static_cast<std::size_t>(bytes);
            } else {
                socket->outgoing.append(static_cast<const char*>(data), static_cast<std::size_t>(bytes));
            }
        } catch (...) {
            return LUMA_E_MEMORY;
        }
    }
    g_io->ready.notify_one();
    return LUMA_OK;
}

std::int64_t take_on(const LumaMod* mod, const char* where, bool internet, LumaHandle handle, void* buf,
                     std::uint64_t size) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    const ModState* state = state_of(mod);
    const std::lock_guard<std::mutex> lock(g_lock);
    Socket* socket = state != nullptr ? find_locked(state, handle, internet) : nullptr;
    if (socket == nullptr || socket != g_in_data || socket->udp) {
        return bad_handle(mod, where);  // a UDP socket's datagrams: take_datagram
    }
    if (buf == nullptr) {
        if (size != 0u) {
            report(mod, where, 101u, "buf");
            return LUMA_E_ARGUMENT;
        }
        return static_cast<std::int64_t>(socket->received.size());
    }
    const std::size_t taken = static_cast<std::size_t>(std::min<std::uint64_t>(size, socket->received.size()));
    std::memcpy(buf, socket->received.data(), taken);
    socket->received.erase(0, taken);
    return static_cast<std::int64_t>(taken);
}

std::int32_t close_on(const LumaMod* mod, const char* where, bool internet, LumaHandle handle) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    const ModState* state = state_of(mod);
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        Socket* socket = state != nullptr ? find_locked(state, handle, internet) : nullptr;
        if (socket == nullptr) {
            return bad_handle(mod, where);
        }
        uncount_locked(socket);
        socket->closing = true;  // no event from now on; what it queued is still sent (I/O thread), then it ends
        socket->started = std::chrono::steady_clock::now();  // kCloseTime from here
        socket->received.clear();
        socket->datagrams_in.clear();
        socket->datagrams_in_bytes = 0;
        // Sockets it accepted that the mod never heard of (their ACCEPTED not given yet) close with it.
        for (Socket* fresh : socket->accepted) {
            uncount_locked(fresh);
            fresh->closing = true;
            fresh->started = socket->started;
        }
        socket->accepted.clear();
        reap_locked();
    }
    g_io->ready.notify_one();
    return LUMA_OK;
}

std::int32_t net_port(const LumaMod* mod, LumaHandle listener, std::uint16_t* out) noexcept {
    return port_on(mod, "net.local.port", false, listener, out);
}
std::int32_t net_send(const LumaMod* mod, LumaHandle handle, const void* data, std::uint64_t bytes) noexcept {
    return send_on(mod, "net.local.send", false, handle, data, bytes);
}
std::int64_t net_take(const LumaMod* mod, LumaHandle handle, void* buf, std::uint64_t size) noexcept {
    return take_on(mod, "net.local.take", false, handle, buf, size);
}
std::int32_t net_close(const LumaMod* mod, LumaHandle handle) noexcept {
    return close_on(mod, "net.local.close", false, handle);
}

std::int32_t net_send_to(const LumaMod* mod, LumaHandle handle, const char* host, std::uint16_t port, const void* data,
                         std::uint64_t bytes) noexcept {
    static constexpr const char* where = "net.local.send_to";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (host == nullptr) {
        return argument(mod, where, "host");
    }
    if (port == 0u) {
        return argument(mod, where, "port");
    }
    if (data == nullptr && bytes != 0u) {
        return argument(mod, where, "data");
    }
    try {
        Datagram datagram;
        if (!local_address(host, port, &datagram.address, &datagram.length) || datagram.address.ss_family != AF_INET) {
            return argument(mod, where, "host");  // this machine's or the local network's, IPv4 (udp_open's family)
        }
        const ModState* state = state_of(mod);
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            Socket* socket = state != nullptr ? find_locked(state, handle) : nullptr;
            if (socket == nullptr || !socket->udp || socket->tied) {
                return bad_handle(mod, where);  // udp_open's sockets only (a udp_connect socket: send)
            }
            if (socket->system_closed) {
                return LUMA_E_CLOSED;
            }
            if (bytes > LUMA_NET_DATAGRAM_MOST) {
                return LUMA_E_TOO_LARGE;  // an answer (header)
            }
            if (const std::int32_t status = room_locked(mod, where, state, bytes); status != LUMA_OK) {
                return status;
            }
            datagram.bytes.assign(static_cast<const char*>(data), static_cast<std::size_t>(bytes));
            socket->datagrams_out.push_back(std::move(datagram));
            socket->datagrams_out_bytes += static_cast<std::size_t>(bytes);
        }
        g_io->ready.notify_one();
        return LUMA_OK;
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

// A datagram's sender as text and port.
void peer_of(const Datagram& datagram, LumaNetPeer* out) noexcept {
    std::memset(out, 0, sizeof(*out));
    if (datagram.address.ss_family == AF_INET) {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(&datagram.address);
        inet_ntop(AF_INET, &v4->sin_addr, out->host, sizeof(out->host));
        out->port = ntohs(v4->sin_port);
    } else if (datagram.address.ss_family == AF_INET6) {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&datagram.address);
        inet_ntop(AF_INET6, &v6->sin6_addr, out->host, sizeof(out->host));
        out->port = ntohs(v6->sin6_port);
    }
}

std::int64_t take_datagram_on(const LumaMod* mod, const char* where, bool internet, LumaHandle handle, void* buf,
                              std::uint64_t size, LumaNetPeer* from) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    const ModState* state = state_of(mod);
    const std::lock_guard<std::mutex> lock(g_lock);
    Socket* socket = state != nullptr ? find_locked(state, handle, internet) : nullptr;
    if (socket == nullptr || socket != g_in_data || !socket->udp) {
        return bad_handle(mod, where);  // inside this socket's DATAGRAM call only
    }
    if (buf == nullptr && size != 0u) {
        return argument(mod, where, "buf");
    }
    if (socket->datagrams_in.empty()) {
        return LUMA_E_NOT_FOUND;  // an answer: all taken
    }
    const Datagram& datagram = socket->datagrams_in.front();
    const std::size_t length = datagram.bytes.size();
    if (buf != nullptr) {
        std::memcpy(buf, datagram.bytes.data(), static_cast<std::size_t>(std::min<std::uint64_t>(size, length)));
    }
    if (from != nullptr) {
        peer_of(datagram, from);
    }
    socket->datagrams_in_bytes -= length;
    socket->datagrams_in.pop_front();
    return static_cast<std::int64_t>(length);
}

std::int64_t net_take_datagram(const LumaMod* mod, LumaHandle handle, void* buf, std::uint64_t size,
                               LumaNetPeer* from) noexcept {
    return take_datagram_on(mod, "net.local.take_datagram", false, handle, buf, size, from);
}

// ---- net.internet (layer 2): the hosts luma.json names, public addresses only ---------------------------------------

// Not this machine's, not the local network's (net.local's), not an address no host has (0/8, 100.64/10 shared,
// 224/4 multicast, 240/4 reserved and the broadcast).
bool public_v4(const in_addr& address) noexcept {
    const std::uint32_t a = ntohl(address.s_addr);
    return !local_v4(address) && (a >> 24u) != 0u && (a >> 22u) != 0x191u /* 100.64/10 */ && (a >> 28u) != 0xEu &&
           (a >> 28u) != 0xFu;
}

bool public_v6(const in6_addr& address) noexcept {
    const unsigned char* b = address.s6_addr;
    static const unsigned char zero[12] = {};
    if (std::memcmp(b, zero, 10) == 0 && b[10] == 0xFFu && b[11] == 0xFFu) {  // ::ffff:a.b.c.d, a v4 address
        in_addr v4{};
        std::memcpy(&v4.s_addr, b + 12, 4);
        return public_v4(v4);
    }
    if (std::memcmp(b, zero, 12) == 0) {
        return false;  // ::, ::1 and the old v4-compatible ::a.b.c.d
    }
    return !local_v6(address) && b[0] != 0xFFu /* ff00::/8 multicast */;
}

// `host` (lower case) as luma.json's entries name it: the same text, "*.<name>" for a name under <name>, "*" any.
bool host_named(const std::vector<std::string>& hosts, std::string_view host) {
    for (const std::string& entry : hosts) {
        if (entry == "*" || entry == host) {
            return true;
        }
        if (entry.size() > 2u && entry[0] == '*' && entry[1] == '.') {
            const std::string_view under = std::string_view(entry).substr(1u);  // ".<name>"
            if (host.size() > under.size() && host.ends_with(under)) {
                return true;
            }
        }
    }
    return false;
}

// A host as given in lower case without a final dot, or empty when it is neither a name (letters, digits, '-', '.',
// no empty label, at most 63 bytes a label) nor an address in text.
std::string host_text(const char* given) {
    const std::size_t length = ::strnlen(given, LUMA_NET_HOST_MOST + 2u);
    if (length == 0u || length > LUMA_NET_HOST_MOST + 1u) {
        return {};
    }
    std::string host(given, length);
    for (char& c : host) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (host.back() == '.') {
        host.pop_back();
    }
    if (host.empty() || host.size() > LUMA_NET_HOST_MOST) {
        return {};
    }
    in_addr v4{};
    in6_addr v6{};
    if (inet_pton(AF_INET, host.c_str(), &v4) == 1 || inet_pton(AF_INET6, host.c_str(), &v6) == 1) {
        return host;
    }
    std::size_t label = 0;
    for (const char c : host) {
        if (c == '.') {
            if (label == 0u) {
                return {};
            }
            label = 0;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') || ++label > 63u) {
            return {};
        }
    }
    return label != 0u ? host : std::string();
}

// A lookup's own thread: the name's public addresses into the socket (it holds the socket until `resolving` is false).
void look_up(Socket* socket, std::string host, std::uint16_t port) {
    std::vector<sockaddr_storage> found;
    std::int32_t status = LUMA_OK;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    char service[8];
    std::snprintf(service, sizeof(service), "%u", static_cast<unsigned>(port));
    addrinfo* list = nullptr;
    if (::getaddrinfo(host.c_str(), service, &hints, &list) != 0 || list == nullptr) {
        status = LUMA_E_NOT_FOUND;  // no such name, or no answer for it
    } else {
        bool any = false;
        try {
            for (const addrinfo* at = list; at != nullptr; at = at->ai_next) {
                sockaddr_storage address{};
                if (at->ai_family == AF_INET && at->ai_addrlen >= sizeof(sockaddr_in)) {
                    any = true;
                    std::memcpy(&address, at->ai_addr, sizeof(sockaddr_in));
                    if (public_v4(reinterpret_cast<const sockaddr_in*>(&address)->sin_addr)) {
                        found.push_back(address);
                    }
                } else if (at->ai_family == AF_INET6 && at->ai_addrlen >= sizeof(sockaddr_in6)) {
                    any = true;
                    std::memcpy(&address, at->ai_addr, sizeof(sockaddr_in6));
                    if (public_v6(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr)) {
                        found.push_back(address);
                    }
                }
            }
            status = !found.empty() ? LUMA_OK : any ? LUMA_E_DENIED : LUMA_E_NOT_FOUND;
        } catch (...) {
            status = LUMA_E_MEMORY;
        }
        ::freeaddrinfo(list);
    }
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        socket->addresses = std::move(found);
        socket->lookup_status = status;
        socket->looked_up = true;
        socket->resolving = false;
    }
    g_io->ready.notify_one();
}

// tcp_connect and udp_connect of net.internet: the host's name looked up, then a stream or a datagram socket tied to
// its first public address that takes the connect.
std::int32_t internet_connect(const LumaMod* mod, const char* where, bool udp, const char* host, std::uint16_t port,
                              LumaEventFn fn, void* user, LumaHandle* out) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (host == nullptr) {
        return argument(mod, where, "host");
    }
    if (port == 0u) {
        return argument(mod, where, "port");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        const std::string name = host_text(host);
        if (name.empty()) {
            return argument(mod, where, "host");
        }
        ModState* state = state_of(mod);
        if (state == nullptr) {
            return LUMA_E_CANCELLED;
        }
        if (!host_named(state->hosts, name)) {
            report(mod, where, 250u, name.c_str());  // Net.HostNotNamed
            return LUMA_E_DENIED;
        }
        if (!system_ready() || !io_started()) {
            return LUMA_E_IO;
        }
        Socket* socket = new (std::nothrow) Socket();
        if (socket == nullptr) {
            return LUMA_E_MEMORY;
        }
        socket->node.event.mod = mod;
        socket->node.event.deliver = deliver;
        socket->node.event.drop = drop;
        socket->node.socket = socket;
        socket->owner = state;
        socket->fn = fn;
        socket->user = user;
        socket->internet = true;
        socket->udp = udp;
        socket->tied = udp;
        socket->kind = Kind::Resolving;
        socket->started = std::chrono::steady_clock::now();  // the 10 s include the lookup
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            if (state->open >= LUMA_NET_SOCKETS_MOST) {
                delete socket;
                report(mod, where, 104u, "16");  // Service.Limit
                return LUMA_E_LIMIT;
            }
            socket->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
            socket->resolving = true;
            g_sockets.push_back(socket);
            state->open++;
        }
        try {
            std::thread(look_up, socket, name, port).detach();
        } catch (...) {
            const std::lock_guard<std::mutex> lock(g_lock);
            socket->resolving = false;
            socket->looked_up = true;
            socket->lookup_status = LUMA_E_IO;  // no thread: the I/O thread gives the ERROR
        }
        g_io->ready.notify_one();
        *out = socket->handle;
        return LUMA_PENDING;
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t internet_tcp_connect(const LumaMod* mod, const char* host, std::uint16_t port, LumaEventFn fn, void* user,
                                  LumaHandle* out) noexcept {
    return internet_connect(mod, "net.internet.tcp_connect", false, host, port, fn, user, out);
}
std::int32_t internet_udp_connect(const LumaMod* mod, const char* host, std::uint16_t port, LumaEventFn fn, void* user,
                                  LumaHandle* out) noexcept {
    return internet_connect(mod, "net.internet.udp_connect", true, host, port, fn, user, out);
}
std::int32_t internet_udp_open(const LumaMod* mod, std::uint16_t, std::uint32_t, LumaEventFn, void*,
                               LumaHandle*) noexcept {
    if (const std::int32_t status = enter(mod, "net.internet.udp_open"); status != LUMA_OK) {
        return status;
    }
    return LUMA_E_DENIED;  // a socket anyone may send to is a server (an answer)
}
std::int32_t internet_send_to(const LumaMod* mod, LumaHandle, const char*, std::uint16_t, const void*,
                              std::uint64_t) noexcept {
    if (const std::int32_t status = enter(mod, "net.internet.send_to"); status != LUMA_OK) {
        return status;
    }
    return LUMA_E_DENIED;  // only udp_open's sockets send_to (an answer)
}
std::int64_t internet_take_datagram(const LumaMod* mod, LumaHandle handle, void* buf, std::uint64_t size,
                                    LumaNetPeer* from) noexcept {
    return take_datagram_on(mod, "net.internet.take_datagram", true, handle, buf, size, from);
}
std::int32_t internet_tcp_listen(const LumaMod* mod, std::uint16_t, std::uint32_t, LumaEventFn, void*,
                                 LumaHandle*) noexcept {
    if (const std::int32_t status = enter(mod, "net.internet.tcp_listen"); status != LUMA_OK) {
        return status;
    }
    return LUMA_E_DENIED;  // no server on the open internet in 1.0 (an answer)
}
std::int32_t internet_port(const LumaMod* mod, LumaHandle listener, std::uint16_t* out) noexcept {
    return port_on(mod, "net.internet.port", true, listener, out);
}
std::int32_t internet_send(const LumaMod* mod, LumaHandle handle, const void* data, std::uint64_t bytes) noexcept {
    return send_on(mod, "net.internet.send", true, handle, data, bytes);
}
std::int64_t internet_take(const LumaMod* mod, LumaHandle handle, void* buf, std::uint64_t size) noexcept {
    return take_on(mod, "net.internet.take", true, handle, buf, size);
}
std::int32_t internet_close(const LumaMod* mod, LumaHandle handle) noexcept {
    return close_on(mod, "net.internet.close", true, handle);
}

// ---- The providers -------------------------------------------------------------------------------------------------

// luma.json's net.internet hosts of `id`, in lower case without a final dot (the manifest the session started with).
std::vector<std::string> hosts_of(const char* id) {
    std::vector<std::string> hosts;
    if (id == nullptr) {
        return hosts;
    }
    for (const galaxy::mods::ModInfo& info : galaxy::mods::session_mods().started_with) {
        if (info.id != id || !info.luma) {
            continue;
        }
        for (const galaxy::luma::Permission& permission : info.luma->permissions) {
            if (permission.name != "net.internet") {
                continue;
            }
            for (std::string host : permission.hosts) {
                for (char& c : host) {
                    if (c >= 'A' && c <= 'Z') {
                        c = static_cast<char>(c - 'A' + 'a');
                    }
                }
                if (!host.empty() && host.back() == '.') {
                    host.pop_back();
                }
                hosts.push_back(std::move(host));
            }
        }
    }
    return hosts;
}

// Both services' (a mod importing both gets one state: the 16 sockets are the mod's, both services together).
void mod_begin(void*, const LumaMod* mod) {
    if (state_of(mod) != nullptr) {
        return;
    }
    try {
        auto state = std::make_unique<ModState>();
        state->mod = mod;
        const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
        state->hosts = hosts_of(kernel != nullptr ? kernel->mod_id(mod) : nullptr);
        g_mods.push_back(std::move(state));
    } catch (...) {
        // Memory: every call of this mod answers LUMA_E_CANCELLED (no state).
    }
}

// After the mod's stop: every socket of it ends (the I/O thread closes them without flushing); nothing is posted or
// called for it from here on (a node already queued is dropped by the kernel).
void mod_gone(void*, const LumaMod* mod) {
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        for (Socket* socket : g_sockets) {
            if (socket->owner == state) {
                socket->owner = nullptr;
                socket->closing = true;
                socket->accepted.clear();
            }
        }
        state->open = 0;
        reap_locked();
    }
    if (g_io != nullptr) {
        g_io->ready.notify_one();
    }
}

void mod_end(void*, const LumaMod* mod) {
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        reap_locked();
    }
    for (auto it = g_mods.begin(); it != g_mods.end(); ++it) {
        if ((*it)->mod == mod) {
            g_mods.erase(it);
            return;
        }
    }
}

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

const LumaNetLocalService kLocal = {
    {sizeof(LumaNetLocalService), LUMA_NET_LOCAL_MAJOR, LUMA_NET_LOCAL_MINOR},
    net_tcp_connect,
    net_tcp_listen,
    net_port,
    net_send,
    net_take,
    net_close,
    net_udp_open,
    net_udp_connect,
    net_send_to,
    net_take_datagram,
};

// net.local reports only the registry's rows (100-104).
const LumaServiceProvider kLocalProvider = {
    sizeof(LumaServiceProvider), LUMA_NET_LOCAL_NAME, &kLocal.base, nullptr, nullptr, 0u, nullptr,
    mod_begin, mod_gone, mod_end, registered,
};

const LumaNetInternetService kInternet = {
    {sizeof(LumaNetInternetService), LUMA_NET_INTERNET_MAJOR, LUMA_NET_INTERNET_MINOR},
    internet_tcp_connect,
    internet_tcp_listen,
    internet_port,
    internet_send,
    internet_take,
    internet_close,
    internet_udp_open,
    internet_udp_connect,
    internet_send_to,
    internet_take_datagram,
};

// net.internet's own row (the net range is 250-299).
const LumaServiceRow kInternetRows[] = {
    {250u, "Net.HostNotNamed", {"where", "host", nullptr, nullptr}},
};
const LumaServiceProvider kInternetProvider = {
    sizeof(LumaServiceProvider), LUMA_NET_INTERNET_NAME, &kInternet.base, nullptr, kInternetRows,
    sizeof(kInternetRows) / sizeof(kInternetRows[0]), nullptr, mod_begin, mod_gone, mod_end, registered,
};
const LumaServiceProvider* const kProviders[] = {&kLocalProvider, &kInternetProvider};

}  // namespace

std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept {
    g_log = log;
    return kProviders;
}

}  // namespace galaxy::luma_net
