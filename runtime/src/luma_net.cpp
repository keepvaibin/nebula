// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's net services (contract services-net; plan 2.7), layer 1: `net.local`, TCP on this machine and the local
// network; layer 2: `net.internet`, TCP to the hosts a mod's luma.json names, by name (looked up on a thread of its
// own, one per lookup) or address, public addresses only. Non-blocking sockets: the game thread makes them (socket,
// connect, bind, listen return at once; an internet connect's sockets are made by the I/O thread once its name is
// looked up) and one I/O thread, started at the first socket, polls them, reads, writes and closes. Minor 1 adds UDP
// sockets (udp_open, udp_connect): datagrams in and out whole, on the same thread and under the same caps. Minor 2
// adds endpoints on this device by name (ipc_connect, ipc_listen): Unix sockets, polled as the others, and on Windows
// named pipes, which are not sockets: the I/O thread drives them by overlapped calls it checks at each turn. The game
// thread never closes a system socket and the I/O thread never frees a Socket: the game thread frees one once the I/O
// thread closed it, its node is not queued and no lookup holds it, so a poll never meets a closed descriptor and a
// delivery never meets freed memory. Minor 3 adds TLS (BoringSSL, third_party/boringssl) and HTTP/1.1 requests on
// the same sockets: the I/O thread moves bytes between a socket and its TLS or request state under the lock, then does
// the handshake, the encryption, the answer's parsing and gzip outside it (that state is the I/O thread's alone), then
// settles what came of it under the lock again; the game thread never waits on cryptography.

#include "galaxy/luma_net_service.h"
#include "galaxy/luma_net_reach.h"

#include "galaxy/luma.h"
#include "galaxy/mods.h"
#include "galaxy/path_text.h"

#include <luma_net.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

// zlib's inflate as third_party/zlib_inflate.cmake builds it (nebula_zinflate: z_ names).
#ifndef Z_PREFIX
#define Z_PREFIX
#endif
#include <zlib.h>
#undef Byte  // zconf.h's z_Byte would rename the Windows in6_addr member behind s6_addr (u.Byte)

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
// A TCP connect to this machine (127.0.0.0/8, ::1): Windows answers a refusing port's reset by sending the SYN again
// (about 10 s before WSAECONNREFUSED); this machine's refusal is final, so its connect fails at once, as it does on
// Linux, macOS and Android. Other destinations keep the system's retries (a lost SYN on a real network).
void refuse_fast(Sock s, const sockaddr_storage& to) noexcept {
    bool loopback = false;
    if (to.ss_family == AF_INET) {
        loopback = (ntohl(reinterpret_cast<const sockaddr_in&>(to).sin_addr.s_addr) >> 24) == 127u;
    } else if (to.ss_family == AF_INET6) {
        loopback = IN6_IS_ADDR_LOOPBACK(&reinterpret_cast<const sockaddr_in6&>(to).sin6_addr) != 0;
    }
    if (!loopback) {
        return;
    }
    TCP_INITIAL_RTO_PARAMETERS parameters{};
    parameters.Rtt = TCP_INITIAL_RTO_UNSPECIFIED_RTT;
    parameters.MaxSynRetransmissions = TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS;
    DWORD bytes = 0;
    WSAIoctl(s, SIO_TCP_INITIAL_RTO, &parameters, sizeof(parameters), nullptr, 0, &bytes, nullptr, nullptr);
}
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
void refuse_fast(Sock, const sockaddr_storage&) noexcept {}  // a refusal is final here already
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

// The address of `host` (text, never a name looked up: layer 1) and port into *out; false when it is not local.
bool local_address(const char* host, std::uint16_t port, sockaddr_storage* out, socklen_t* length) noexcept {
    std::memset(out, 0, sizeof(*out));
    std::string_view text(host);
    if (text == "localhost") {
        text = "127.0.0.1";
    }
    const std::string copy(text);
    sockaddr_in v4{};
    sockaddr_in6 v6{};
    static const unsigned char kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFu, 0xFFu};
    bool is_v4 = inet_pton(AF_INET, copy.c_str(), &v4.sin_addr) == 1;
    if (!is_v4) {
        if (inet_pton(AF_INET6, copy.c_str(), &v6.sin6_addr) != 1) {
            return false;
        }
        if (std::memcmp(v6.sin6_addr.s6_addr, kMapped, 12) == 0) {
            std::memcpy(&v4.sin_addr.s_addr, v6.sin6_addr.s6_addr + 12, 4);  // ::ffff:a.b.c.d is a.b.c.d
            is_v4 = true;
        }
    }
    if (is_v4) {
        v4.sin_family = AF_INET;
        v4.sin_port = htons(port);
        std::memcpy(out, &v4, sizeof(v4));
        *length = static_cast<socklen_t>(sizeof(v4));
    } else {
        v6.sin6_family = AF_INET6;
        v6.sin6_port = htons(port);
        std::memcpy(out, &v6, sizeof(v6));
        *length = static_cast<socklen_t>(sizeof(v6));
    }
    return reach_local(*out);  // the address connected to is the address judged: no name is looked up here
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

#if defined(_WIN32)
// A named pipe (minor 2). Each overlapped call has its own event (a pipe's handle cannot tell a read's end from a
// write's); ReadFile, WriteFile and ConnectNamedPipe reset it as they start.
struct Pipe {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring path;     // \\.\pipe\<name>
    OVERLAPPED read_ov{};
    OVERLAPPED write_ov{};
    OVERLAPPED connect_ov{};
    bool reading = false;  // a ReadFile runs into `in`
    bool writing = false;  // a WriteFile runs from `out`
    bool waiting = false;  // a listener's ConnectNamedPipe runs
    std::vector<char> in;  // the running read's room (64 KiB)
    std::string out;       // the running write's bytes, kept until it ends

    // Ends what runs (cancelled, then waited for: no call writes into freed memory) and closes the handle.
    void close() noexcept {
        if (handle == INVALID_HANDLE_VALUE) {
            return;
        }
        if (reading || writing || waiting) {
            CancelIoEx(handle, nullptr);
            DWORD n = 0;
            if (reading) {
                GetOverlappedResult(handle, &read_ov, &n, TRUE);
            }
            if (writing) {
                GetOverlappedResult(handle, &write_ov, &n, TRUE);
            }
            if (waiting) {
                GetOverlappedResult(handle, &connect_ov, &n, TRUE);
            }
            reading = writing = waiting = false;
        }
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
    ~Pipe() {
        close();
        for (OVERLAPPED* ov : {&read_ov, &write_ov, &connect_ov}) {
            if (ov->hEvent != nullptr) {
                CloseHandle(ov->hEvent);
            }
        }
    }
};

// A Pipe with its events and its read room, or null (no memory or no event).
std::unique_ptr<Pipe> new_pipe() {
    std::unique_ptr<Pipe> pipe(new (std::nothrow) Pipe());
    if (pipe == nullptr) {
        return nullptr;
    }
    try {
        pipe->in.resize(64u << 10u);
    } catch (...) {
        return nullptr;
    }
    for (OVERLAPPED* ov : {&pipe->read_ov, &pipe->write_ov, &pipe->connect_ov}) {
        ov->hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ov->hEvent == nullptr) {
            return nullptr;
        }
    }
    return pipe;
}

// An OVERLAPPED cleared for its next call, its event kept.
void reset(OVERLAPPED& ov) noexcept {
    const HANDLE event = ov.hEvent;
    std::memset(&ov, 0, sizeof(ov));
    ov.hEvent = event;
}
#endif

// ---- TLS and HTTP state (minor 3) ---------------------------------------------------------------------------------

constexpr std::size_t kWireInMost = 256u << 10u;   // encrypted bytes read, not yet given to TLS (reading pauses)
constexpr std::size_t kPlainOutMost = 1u << 20u;   // the mod's bytes TLS holds, not yet encrypted and sent
constexpr std::size_t kRawMost = 256u << 10u;      // an answer's bytes read, not yet parsed (reading pauses)
constexpr std::size_t kBodyOutMost = 1u << 20u;    // an answer's body parsed, not yet the mod's (parsing pauses)
constexpr std::size_t kHeadMost = 64u << 10u;      // an answer's status line and header lines
constexpr std::size_t kOwnFileMost = 64u << 10u;   // a certificate, key or root file of a mod's
constexpr std::size_t kRequestBodyMost = LUMA_NET_SEND_QUEUED_MOST;  // a request's body (the mod's send queue)

#include "net_roots.inc"  // kNetRoots: Mozilla's roots, PEM, built in by third_party/boringssl.cmake

// The roots a mod's luma.json adds for its own TLS (a net permission's "roots"), read at its begin; read only after.
struct Roots {
    std::vector<bssl::UniquePtr<X509>> certificates;
};

// A TLS stream's state: the I/O thread's alone (made by the game thread before the socket joins the polled list).
struct Tls {
    SSL* ssl = nullptr;   // made at the I/O thread's first work on it; owns rbio and wbio
    BIO* rbio = nullptr;  // the other side's bytes, for ssl
    BIO* wbio = nullptr;  // what ssl wrote, for the other side
    bool server = false;  // a tls_listen listener's connection
    std::shared_ptr<SSL_CTX> context;     // a server's (its listener's); a client uses the shared client context
    std::shared_ptr<const Roots> roots;   // a client's: its mod's own roots, or null
    std::string host;                     // a client's: the name or address the certificate must name
    std::string wire_in;                  // read from the socket, not yet given to ssl
    std::string wire_out;                 // ssl wrote it, not yet sent
    std::string plain_in;                 // decrypted, not yet the mod's (a stream's; a request's go to Http::raw)
    std::string plain_out;                // the mod's, not yet given to ssl
    std::uint64_t moved = 0;              // the mod's bytes taken from `outgoing` since the last SENT
    bool eof = false;                     // the socket's other side ended (or broke: `broke`)
    bool broke = false;
    bool handshaken = false;
    bool announced = false;               // what the handshake's end gives (CONNECTED, sends) was given
    bool ended = false;                   // nothing more comes out of ssl
    bool close_notify = false;            // the other side ended TLS properly (CLOSED with LUMA_OK)
    bool closing = false;                 // the mod closed it (copied under the lock for the unlocked work)
    bool shutdown_sent = false;
    std::int32_t failed = LUMA_OK;        // the handshake failed with this status
    std::string refusal;                  // why the certificate was refused (Net.CertificateRefused)

    Tls() = default;
    Tls(const Tls&) = delete;
    Tls& operator=(const Tls&) = delete;
    ~Tls() {
        if (ssl != nullptr) {
            SSL_free(ssl);
        }
    }
};

// A request's state (http): the request kept to send it again at a redirect, and the answer's parsing. The I/O
// thread's alone once the socket joins the polled list.
struct Http {
    std::string method;
    bool https = false;
    std::string host;          // lower case; an IPv6 address without its brackets
    std::uint16_t port = 0;
    std::string target;        // the path and query, as they go on the wire
    std::string lines;         // the mod's header lines, "Name: value\r\n" each
    bool user_agent = false;   // the mod names its own User-Agent
    std::string body;
    std::uint32_t flags = 0;
    std::string mod_id;
    std::vector<std::string> hosts;      // net.internet's: luma.json's hosts (each redirect is checked against them)
    std::shared_ptr<const Roots> roots;  // the mod's own TLS roots
    std::chrono::steady_clock::time_point deadline{};
    std::uint32_t redirects = 0;
    // The answer.
    enum class Stage : std::uint8_t { Head, Length, ToEnd, ChunkSize, ChunkData, ChunkEnd, Trailer, Flush, Done };
    Stage stage = Stage::Head;
    std::string raw;           // the stream's bytes, not parsed yet
    bool eof = false;          // a plain request's socket ended (or broke); an https one's: its Tls's
    bool broke = false;
    std::uint64_t left = 0;    // Length: the body's bytes still to come; ChunkData: this chunk's
    std::uint32_t status = 0;
    std::string head;          // the header lines given at HEADERS
    bool head_ready = false;
    bool head_given = false;
    z_stream* zs = nullptr;    // a gzip body's inflater
    bool z_ended = false;
    std::string body_out;      // the body parsed, not yet the mod's
    std::uint64_t total = 0;   // the body's bytes (after gzip)
    std::int32_t failed = LUMA_OK;
    bool redirect = false;     // a 3xx to follow to `location` (settled under the lock)
    std::string location;

    Http() = default;
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;
    ~Http() { end_inflate(); }
    void end_inflate() {
        if (zs != nullptr) {
            inflateEnd(zs);
            delete zs;
            zs = nullptr;
        }
        z_ended = false;
    }
};

// A WebSocket's state (minor 4): the game thread's alone (made by ws_connect, read and changed only by deliver and the
// calls). Its bytes ride on the stream below it (plain or TLS) as any socket's: the upgrade request first, in
// `outgoing`, then the mod's frames.
struct Ws {
    enum class Stage : std::uint8_t { Head, Open, Ended };
    Stage stage = Stage::Head;
    std::string request;            // the upgrade request (moved to `outgoing` at the socket's making)
    std::string accept;             // the Sec-WebSocket-Accept the server must answer
    std::vector<std::string> protocols;  // asked (empty: none)
    std::string in;                 // the stream's bytes not parsed yet
    std::string message;            // a message's frames so far
    std::uint8_t message_op = 0;    // 1 text, 2 binary; 0: none begun
    std::string chosen;             // the protocol the server chose (CONNECTED's data)
};

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
    // Its ends (I/O thread, g_lock; read once each, ends_of_locked) and what another socket of this process sent it and
    // it has not read: deterministic mode's frame_start waits for that to come in.
    std::uint16_t local_port = 0;  // its own port
    std::uint16_t peer_port = 0;   // the other end's, when that is a loopback address (0: not, or not connected)
    std::uint8_t ends_known = 0;   // 1 local_port read, 2 peer_port read
    std::uint64_t own_due = 0;     // datagrams (UDP) or bytes (a stream) on their way here from this process
    std::uint16_t connect_port = 0;  // a stream's connect: its target's port when that is a loopback address (else 0)
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
    // Endpoints on this device (minor 2).
    bool ipc = false;            // made by ipc_connect or ipc_listen (or accepted by such a listener)
    std::string unix_path;       // a listener's socket file (Apple), removed when it closes
#if defined(_WIN32)
    std::unique_ptr<Pipe> pipe;  // a named pipe's: `sock` stays kNoSock
#endif
    // TLS and requests (minor 3). The Tls and Http are the I/O thread's; the fields below them are g_lock's.
    std::unique_ptr<Tls> tls;       // a TLS stream's (and an https request's); replaced at a redirect
    std::unique_ptr<Http> http;     // a request's (set at its making, never changed)
    std::shared_ptr<SSL_CTX> tls_server;  // a tls_listen listener's: its connections' context
    bool tls_client = false;        // made by tls_connect: no send before its CONNECTED
    bool tls_ready = false;         // its handshake is done
    std::size_t tls_queued = 0;     // the mod's bytes TLS still holds (they count against the send queue)
    const char* where = nullptr;    // the call that made it, for a report given with its ERROR
    std::uint32_t report_code = 0;  // a row to report with its ERROR (251, 252), and its arguments
    std::string report_a;
    std::string report_b;
    bool headers_event = false;     // HEADERS to give
    std::uint32_t http_status = 0;
    std::string http_headers;       // taken inside HEADERS
    bool done_event = false;        // DONE to give, once the body is taken
    std::uint64_t body_total = 0;
    std::unique_ptr<Ws> ws;         // a WebSocket's (minor 4; set at its making, never changed): the game thread's
    // A listener's or udp_open socket's peers outside this machine and the local network (g_lock): refused, counted;
    // the first is said once in the mod log (Net.PeerRefused), at the next delivery.
    std::uint64_t peers_refused = 0;
    bool peer_report = false;
};

bool is_pipe(const Socket* socket) noexcept {
#if defined(_WIN32)
    return socket->pipe != nullptr;
#else
    (void)socket;
    return false;
#endif
}

struct ModState {
    const LumaMod* mod = nullptr;
    std::uint32_t open = 0;  // g_lock: sockets counted against LUMA_NET_SOCKETS_MOST
    std::vector<std::string> hosts;  // game thread: luma.json's net.internet hosts, in lower case
    std::filesystem::path package;   // game thread: its installed folder (empty: none)
    std::shared_ptr<const Roots> roots;  // its luma.json's TLS roots, or null (read at its begin)
};

std::mutex g_lock;  // every Socket's fields but node.event's (the kernel's), g_sockets, ModState::open
std::vector<Socket*> g_sockets;  // g_lock
std::vector<std::unique_ptr<ModState>> g_mods;  // game thread
std::atomic<LumaHandle> g_next_handle{1};
const Socket* g_in_data = nullptr;  // game thread: the socket whose DATA (or HEADERS) call runs
bool g_in_headers = false;          // game thread: that call is a HEADERS: `take` gives the header lines
const Socket* g_delivering = nullptr;  // g_lock: the socket whose deliver runs (reap leaves it: a call inside may
                                       // close it and another, and the I/O thread close its system socket meanwhile)

// The I/O thread: made at the first socket and never freed (a thread still polling at the process's end meets no
// destroyed lock).
struct Io {
    std::condition_variable ready;  // with g_lock: a socket exists
    bool started = false;
    // The wake: a UDP socket on the loopback address connected to itself, in every poll. A byte sent there ends the
    // poll's wait at once: a send queued, a socket made or closed while the thread waits (without it each waited up to
    // kPollMs, every message 5 ms late on average).
    Sock wake = kNoSock;
    std::atomic<bool> wake_sent{false};  // a byte is on its way and not drained: another wake sends none
    // Deterministic mode (frame_start), with g_lock: the game thread's asks, the I/O thread's answers.
    std::uint64_t settle_asked = 0;
    std::uint64_t settle_done = 0;
    std::uint64_t settle_polled = 0;  // the I/O thread's: the ask its last pass polled for (a first pass waits 0 ms)
    std::condition_variable settled;
};
Io* g_io = nullptr;  // game thread makes it

// Any thread, g_io made: the I/O thread's wait ends now (its condition variable when it holds no socket, its poll when
// it does).
void wake_io() noexcept {
    g_io->ready.notify_one();
    if (g_io->wake != kNoSock && !g_io->wake_sent.exchange(true, std::memory_order_acq_rel)) {
        const char byte = 1;
        (void)send_system(g_io->wake, &byte, 1u);
    }
}

// The wake socket (Io::wake): bound to the loopback address, connected to itself, non-blocking. kNoSock when the
// system gives none (the thread then waits kPollMs at most, as before).
Sock make_wake() noexcept {
    if (!system_ready()) {
        return kNoSock;
    }
    const Sock s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNoSock) {
        return kNoSock;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = static_cast<socklen_t>(sizeof(address));
    if (::bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::getsockname(s, reinterpret_cast<sockaddr*>(&address), &length) != 0 ||
        ::connect(s, reinterpret_cast<const sockaddr*>(&address), length) != 0 || !make_nonblocking(s)) {
        close_system(s);
        return kNoSock;
    }
    return s;
}

// A loopback address's port (0: not a loopback address).
std::uint16_t loopback_port(const sockaddr_storage& address) noexcept {
    if (address.ss_family == AF_INET) {
        const auto& v4 = reinterpret_cast<const sockaddr_in&>(address);
        return (ntohl(v4.sin_addr.s_addr) >> 24) == 127u ? ntohs(v4.sin_port) : 0u;
    }
    if (address.ss_family == AF_INET6) {
        const auto& v6 = reinterpret_cast<const sockaddr_in6&>(address);
        return IN6_IS_ADDR_LOOPBACK(&v6.sin6_addr) ? ntohs(v6.sin6_port) : 0u;
    }
    return 0u;
}

// With g_lock, I/O thread: the socket's own port and its loopback peer's, each read from the system once it is there.
void ends_of_locked(Socket* socket) noexcept {
    if (socket->sock == kNoSock || socket->system_closed || socket->ends_known == 3u) {
        return;
    }
    sockaddr_storage address{};
    socklen_t length = static_cast<socklen_t>(sizeof(address));
    if ((socket->ends_known & 1u) == 0u &&
        ::getsockname(socket->sock, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
        socket->local_port = address.ss_family == AF_INET    ? ntohs(reinterpret_cast<const sockaddr_in&>(address).sin_port)
                             : address.ss_family == AF_INET6 ? ntohs(reinterpret_cast<const sockaddr_in6&>(address).sin6_port)
                                                             : 0u;
        socket->ends_known |= socket->local_port != 0u ? 1u : 0u;
    }
    length = static_cast<socklen_t>(sizeof(address));
    if ((socket->ends_known & 2u) == 0u && (socket->kind == Kind::Open || (socket->udp && socket->tied)) &&
        ::getpeername(socket->sock, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
        socket->peer_port = loopback_port(address);
        socket->ends_known |= 2u;  // connected: its peer is known, a loopback one or not
    }
}

// With g_lock, I/O thread: the socket of this process a datagram to the loopback `port` comes to (null: none).
Socket* own_udp_locked(std::uint16_t port) noexcept {
    if (port == 0u) {
        return nullptr;
    }
    for (Socket* other : g_sockets) {
        if (other->udp && other->kind == Kind::Udp && !other->system_closed) {
            ends_of_locked(other);
            if (other->local_port == port) {
                return other;
            }
        }
    }
    return nullptr;
}

// With g_lock: this process's listener on `port` (null: none).
const Socket* own_listener_locked(std::uint16_t port) noexcept {
    if (port == 0u) {
        return nullptr;
    }
    for (const Socket* other : g_sockets) {
        if (other->kind == Kind::Listener && !other->udp && !other->closing && !other->system_closed && other->port == port) {
            return other;
        }
    }
    return nullptr;
}

// With g_lock, I/O thread: the other end of a stream when it is a socket of this process (null: not, or not yet known).
Socket* own_stream_end_locked(Socket* socket) noexcept {
    ends_of_locked(socket);
    if (socket->peer_port == 0u || socket->local_port == 0u) {
        return nullptr;
    }
    for (Socket* other : g_sockets) {
        if (other != socket && !other->udp && other->kind == Kind::Open && !other->system_closed && !is_pipe(other)) {
            ends_of_locked(other);
            if (other->local_port == socket->peer_port && other->peer_port == socket->local_port) {
                return other;
            }
        }
    }
    return nullptr;
}

void io_loop();

bool io_started() {
    if (g_io != nullptr) {
        return g_io->started;
    }
    g_io = new (std::nothrow) Io();
    if (g_io == nullptr) {
        return false;
    }
    g_io->wake = make_wake();
    if (g_io->wake == kNoSock && g_log != nullptr) {
        g_log("LumaCore net: the I/O thread has no wake socket; a send waits up to 10 ms for it");
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

// With g_lock, I/O thread: a peer outside this machine and the local network was refused on `socket`. Counted; the
// first is posted for one mod-log line, so a flood of them grows no log and no queue.
void refuse_peer_locked(Socket* socket) {
    if (socket->peers_refused++ == 0u) {
        socket->peer_report = true;
        post_locked(socket);
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
        if (socket->system_closed && !socket->posted && !socket->resolving && socket != g_delivering &&
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

// A failed connect's status for the ERROR: an endpoint's (nothing there, refused) or a host's.
std::int32_t connect_failure(const Socket* socket, int error) noexcept {
#if !defined(_WIN32)
    if (socket->ipc) {
        if (error == ECONNREFUSED || error == ENOENT) {
            return LUMA_E_NOT_FOUND;
        }
        return error == EACCES || error == EPERM ? LUMA_E_DENIED : LUMA_E_IO;
    }
#else
    (void)socket;
#endif
    return connect_status(error);
}

// With g_lock, I/O thread: ends the system socket (once).
void close_locked(Socket* socket) {
    if (!socket->system_closed) {
        if (socket->sock != kNoSock) {
            close_system(socket->sock);
        }
#if defined(_WIN32)
        if (socket->pipe != nullptr) {
            socket->pipe->close();
        }
#else
        if (!socket->unix_path.empty()) {
            ::unlink(socket->unix_path.c_str());  // a listener's socket file goes with it
        }
#endif
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
        if (!socket->udp) {
            refuse_fast(sock, address);
        }
        if (::connect(sock, reinterpret_cast<const sockaddr*>(&address), length) == 0 || connecting(system_error())) {
            return true;
        }
    }
    return false;
}

#if defined(_WIN32)
// ---- Named pipes (minor 2, Windows), I/O thread, with g_lock ------------------------------------------------------

constexpr DWORD kPipeBuffer = 64u << 10u;

HANDLE pipe_instance(const std::wstring& path, bool first) noexcept {
    return CreateNamedPipeW(path.c_str(),
                            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0u),
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                            PIPE_UNLIMITED_INSTANCES, kPipeBuffer, kPipeBuffer, 0, nullptr);
}

// A connect: CreateFile each turn while every instance is busy (10 s), then the pipe is open.
void pipe_connect_turn_locked(Socket* socket) {
    if (socket->closing) {
        close_locked(socket);
        return;
    }
    Pipe& pipe = *socket->pipe;
    // SECURITY_IDENTIFICATION: the program on the other end may learn who connects, never act as this game.
    const HANDLE handle =
        CreateFileW(pipe.path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_BUSY && std::chrono::steady_clock::now() - socket->started <= kConnectTime) {
            return;  // every instance taken: again at the next turn
        }
        close_locked(socket);
        socket->failed = error == ERROR_FILE_NOT_FOUND ? LUMA_E_NOT_FOUND
                         : error == ERROR_PIPE_BUSY    ? LUMA_E_TIMEOUT
                         : error == ERROR_ACCESS_DENIED ? LUMA_E_DENIED
                                                        : LUMA_E_IO;
        post_locked(socket);
        return;
    }
    pipe.handle = handle;
    socket->kind = Kind::Open;
    socket->connected = true;
    post_locked(socket);  // its reads start at the next turn
}

// A listener's waiting instance became a connection: a new open socket of the mod (ACCEPTED), or closed when full.
void accept_pipe_locked(Socket* listener, HANDLE handle) {
    std::unique_ptr<Pipe> pipe = listener->owner->open < LUMA_NET_SOCKETS_MOST ? new_pipe() : nullptr;
    Socket* fresh = pipe != nullptr ? new (std::nothrow) Socket() : nullptr;
    if (fresh == nullptr) {
        DisconnectNamedPipe(handle);  // full, or no memory: the other side sees its connection end at once
        CloseHandle(handle);
        return;
    }
    pipe->handle = handle;
    fresh->pipe = std::move(pipe);
    fresh->node.event.mod = listener->node.event.mod;
    fresh->node.event.deliver = deliver;
    fresh->node.event.drop = drop;
    fresh->node.socket = fresh;
    fresh->owner = listener->owner;
    fresh->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
    fresh->fn = listener->fn;
    fresh->user = listener->user;
    fresh->kind = Kind::Open;
    fresh->ipc = true;
    try {
        g_sockets.push_back(fresh);
        listener->accepted.push_back(fresh);
    } catch (...) {
        if (!g_sockets.empty() && g_sockets.back() == fresh) {
            g_sockets.pop_back();
        }
        delete fresh;  // its Pipe closes the handle
        return;
    }
    listener->owner->open++;
    post_locked(listener);
}

// A listener: one instance waits for a client at a time; each client's instance becomes a socket, then a new one waits.
void pipe_listener_turn_locked(Socket* socket) {
    Pipe& pipe = *socket->pipe;
    for (int round = 0; round < 8; round++) {
        if (pipe.handle == INVALID_HANDLE_VALUE) {
            pipe.handle = pipe_instance(pipe.path, false);
            if (pipe.handle == INVALID_HANDLE_VALUE) {
                return;  // none to be had now: the next turn
            }
        }
        if (!pipe.waiting) {
            reset(pipe.connect_ov);
            ConnectNamedPipe(pipe.handle, &pipe.connect_ov);  // overlapped: its answer is in GetLastError
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                pipe.waiting = true;
            } else if (error != ERROR_PIPE_CONNECTED) {
                CloseHandle(pipe.handle);
                pipe.handle = INVALID_HANDLE_VALUE;
                return;  // this instance failed: a new one at the next turn
            }
        }
        if (pipe.waiting) {
            DWORD n = 0;
            if (!GetOverlappedResult(pipe.handle, &pipe.connect_ov, &n, FALSE)) {
                if (GetLastError() == ERROR_IO_INCOMPLETE) {
                    return;  // nobody yet
                }
                pipe.waiting = false;
                CloseHandle(pipe.handle);  // that client left before it was taken: a new instance
                pipe.handle = INVALID_HANDLE_VALUE;
                continue;
            }
            pipe.waiting = false;
        }
        const HANDLE connected = pipe.handle;
        pipe.handle = INVALID_HANDLE_VALUE;
        accept_pipe_locked(socket, connected);
    }
}

// An open pipe: reads while there is room, writes what is queued, ends as a TCP socket does.
void pipe_open_turn_locked(Socket* socket) {
    Pipe& pipe = *socket->pipe;
    const auto broke = [socket](DWORD error) {
        socket->peer_closed = true;
        socket->closed_status =
            error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA ? LUMA_OK
                                                                                                       : LUMA_E_IO;
    };
    for (int round = 0; round < 16; round++) {
        if (pipe.reading) {
            DWORD got = 0;
            if (!GetOverlappedResult(pipe.handle, &pipe.read_ov, &got, FALSE)) {
                const DWORD error = GetLastError();
                if (error == ERROR_IO_INCOMPLETE) {
                    break;
                }
                pipe.reading = false;
                broke(error);
                break;
            }
            pipe.reading = false;
            try {
                socket->received.append(pipe.in.data(), got);
            } catch (...) {
                broke(ERROR_NOT_ENOUGH_MEMORY);  // the bytes are gone: the stream cannot go on whole
                break;
            }
        }
        if (socket->peer_closed || socket->closing || socket->received.size() >= LUMA_NET_RECEIVED_MOST) {
            break;
        }
        reset(pipe.read_ov);
        const DWORD room = static_cast<DWORD>(
            std::min<std::size_t>(pipe.in.size(), LUMA_NET_RECEIVED_MOST - socket->received.size()));
        if (!ReadFile(pipe.handle, pipe.in.data(), room, nullptr, &pipe.read_ov) &&
            GetLastError() != ERROR_IO_PENDING) {
            broke(GetLastError());
            break;
        }
        pipe.reading = true;  // done at once or later: collected through the OVERLAPPED either way
    }
    if (!socket->received.empty() || socket->peer_closed) {
        post_locked(socket);
    }
    for (int round = 0; round < 16; round++) {
        if (pipe.writing) {
            DWORD put = 0;
            if (!GetOverlappedResult(pipe.handle, &pipe.write_ov, &put, FALSE)) {
                const DWORD error = GetLastError();
                if (error == ERROR_IO_INCOMPLETE) {
                    break;
                }
                pipe.writing = false;
                broke(error);
                socket->outgoing.clear();
                pipe.out.clear();
                break;
            }
            pipe.writing = false;
            socket->sent_since += put;
            if (put < pipe.out.size()) {
                try {
                    socket->outgoing.insert(0, pipe.out, put, std::string::npos);  // the rest goes first, next
                } catch (...) {
                    broke(ERROR_NOT_ENOUGH_MEMORY);
                    break;
                }
            }
            pipe.out.clear();
        }
        if (socket->outgoing.empty() || socket->peer_closed) {
            break;
        }
        try {
            const std::size_t bytes = std::min<std::size_t>(socket->outgoing.size(), kPipeBuffer);
            pipe.out.assign(socket->outgoing, 0, bytes);
            socket->outgoing.erase(0, bytes);
        } catch (...) {
            break;  // no memory now: the next turn
        }
        reset(pipe.write_ov);
        if (!WriteFile(pipe.handle, pipe.out.data(), static_cast<DWORD>(pipe.out.size()), nullptr, &pipe.write_ov) &&
            GetLastError() != ERROR_IO_PENDING) {
            broke(GetLastError());
            socket->outgoing.clear();
            pipe.out.clear();
            break;
        }
        pipe.writing = true;
    }
    if (!pipe.writing && socket->outgoing.empty() && socket->sent_since > 0u && !socket->closing) {
        socket->sent_event = true;
        post_locked(socket);
    }
    const bool drained = !pipe.writing && socket->outgoing.empty();
    if (socket->closing && (drained || std::chrono::steady_clock::now() - socket->started > kCloseTime)) {
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

void pipe_turn_locked(Socket* socket) {
    switch (socket->kind) {
    case Kind::Connecting:
        pipe_connect_turn_locked(socket);
        return;
    case Kind::Listener:
        if (socket->closing) {
            close_locked(socket);
            return;
        }
        pipe_listener_turn_locked(socket);
        return;
    case Kind::Open:
        pipe_open_turn_locked(socket);
        return;
    default:
        return;
    }
}
#endif

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
            if (!socket->tied && !reach_local(datagram.address)) {
                refuse_peer_locked(socket);  // udp_open hears this machine and the local network only: dropped
                continue;
            }
            const std::uint16_t from_port = loopback_port(datagram.address);
            try {
                datagram.bytes.assign(g_datagram->data(), static_cast<std::size_t>(got));
                socket->datagrams_in.push_back(std::move(datagram));
            } catch (...) {
                break;  // no memory: this one is lost, as the network may lose it
            }
            socket->datagrams_in_bytes += static_cast<std::size_t>(got);
            if (socket->own_due != 0u && own_udp_locked(from_port) != nullptr) {
                socket->own_due--;  // one of those another socket of this process sent here
            }
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
            if (put >= 0) {
                ends_of_locked(socket);
                if (Socket* to = own_udp_locked(socket->tied ? socket->peer_port : loopback_port(datagram.address))) {
                    to->own_due++;  // deterministic mode's frame_start waits for it there
                }
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

// ---- TLS and HTTP on the I/O thread (minor 3) ---------------------------------------------------------------------

std::string host_text(const char* given);
bool host_named(const std::vector<std::string>& hosts, std::string_view host);
void look_up(Socket* socket, std::string host, std::uint16_t port);

// With g_lock, I/O thread: reads the socket into `in` while it holds less than `most` (straight into it, grown by up
// to 64 KiB and cut back after; nothing on the stack, R9). False once the other side ended (*broke: it broke).
bool read_stream_locked(Socket* socket, std::string& in, std::size_t most, bool* broke) {
    while (in.size() < most) {
        const std::size_t before = in.size();
        const std::size_t room = std::min<std::size_t>(64u << 10u, most - before);
        try {
            in.resize(before + room);
        } catch (...) {
            return true;  // no memory now: the bytes wait in the system's buffer
        }
        const int got = receive_system(socket->sock, in.data() + before, room);
        in.resize(before + (got > 0 ? static_cast<std::size_t>(got) : 0u));
        if (got > 0) {
            socket->own_due -= std::min<std::uint64_t>(socket->own_due, static_cast<std::uint64_t>(got));
            continue;
        }
        if (got == 0) {
            *broke = false;
            return false;
        }
        if (!would_block(system_error())) {
            *broke = true;
            return false;
        }
        return true;
    }
    return true;
}

// With g_lock, I/O thread: sends what the socket takes of `out` (the sent part erased). The bytes sent, or -1 when the
// connection broke (`out` cleared).
std::int64_t write_stream_locked(Socket* socket, std::string& out) {
    std::size_t done = 0;
    while (done < out.size()) {
        const int put = send_system(socket->sock, out.data() + done, out.size() - done);
        if (put > 0) {
            done += static_cast<std::size_t>(put);
            if (Socket* to = own_stream_end_locked(socket)) {
                to->own_due += static_cast<std::uint64_t>(put);
            }
            continue;
        }
        if (put < 0 && !would_block(system_error())) {
            out.clear();
            return -1;
        }
        break;
    }
    if (done > 0u) {
        out.erase(0, done);
    }
    return static_cast<std::int64_t>(done);
}

// Whether a connect (its lookup, its TCP connect, its TLS handshake) has run past its 10 s, or a request past its
// own time.
bool connect_late(const Socket* socket, std::chrono::steady_clock::time_point now) {
    return now - socket->started > kConnectTime || (socket->http != nullptr && now > socket->http->deadline);
}

// I/O thread: the client context of every tls_connect and https request (TLS 1.2 and 1.3, the other side's
// certificate checked against Mozilla's roots), and those roots parsed once (a mod with roots of its own gets a store
// of both). Made at the first TLS work, kept to the process's end (as g_io).
SSL_CTX* g_client = nullptr;
std::vector<bssl::UniquePtr<X509>>* g_mozilla = nullptr;
bool g_client_tried = false;

SSL_CTX* client_context() {
    if (g_client_tried) {
        return g_client;
    }
    g_client_tried = true;
    try {
        auto roots = std::make_unique<std::vector<bssl::UniquePtr<X509>>>();
        bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(kNetRoots, sizeof(kNetRoots)));
        while (bio != nullptr) {
            X509* root = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
            if (root == nullptr) {
                break;
            }
            roots->emplace_back(root);
        }
        ERR_clear_error();  // the read past the last root leaves "no start line"
        bssl::UniquePtr<SSL_CTX> context(SSL_CTX_new(TLS_method()));
        if (context != nullptr && !roots->empty() && SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION) &&
            SSL_CTX_set_max_proto_version(context.get(), TLS1_3_VERSION)) {
            X509_STORE* store = SSL_CTX_get_cert_store(context.get());
            for (const bssl::UniquePtr<X509>& root : *roots) {
                X509_STORE_add_cert(store, root.get());
            }
            SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER, nullptr);
            g_mozilla = roots.release();
            g_client = context.release();
        }
    } catch (...) {
    }
    ERR_clear_error();
    if (g_client == nullptr && g_log != nullptr) {
        g_log("LumaCore net: TLS could not start; every TLS connect ends with ERROR LUMA_E_IO");
    }
    return g_client;
}

bool is_address(const std::string& host) {
    in_addr v4{};
    in6_addr v6{};
    return inet_pton(AF_INET, host.c_str(), &v4) == 1 || inet_pton(AF_INET6, host.c_str(), &v6) == 1;
}

// I/O thread: the Tls's SSL with its memory BIOs: a server's from its listener's context; a client's from the client
// context, checking `host` (a name: also sent as SNI) against Mozilla's roots plus the mod's own.
bool tls_start(Tls& tls) {
    SSL_CTX* const context = tls.server ? tls.context.get() : client_context();
    if (context == nullptr) {
        return false;
    }
    bssl::UniquePtr<SSL> ssl(SSL_new(context));
    BIO* const rbio = BIO_new(BIO_s_mem());
    BIO* const wbio = BIO_new(BIO_s_mem());
    if (ssl == nullptr || rbio == nullptr || wbio == nullptr) {
        BIO_free(rbio);
        BIO_free(wbio);
        return false;
    }
    SSL_set_bio(ssl.get(), rbio, wbio);  // ssl owns both from here
    if (tls.server) {
        SSL_set_accept_state(ssl.get());
    } else {
        SSL_set_connect_state(ssl.get());
        X509_VERIFY_PARAM* const param = SSL_get0_param(ssl.get());
        if (is_address(tls.host)) {
            if (!X509_VERIFY_PARAM_set1_ip_asc(param, tls.host.c_str())) {
                return false;
            }
        } else if (!SSL_set_tlsext_host_name(ssl.get(), tls.host.c_str()) ||
                   !X509_VERIFY_PARAM_set1_host(param, tls.host.data(), tls.host.size())) {
            return false;
        }
        if (tls.roots != nullptr && !tls.roots->certificates.empty() && g_mozilla != nullptr) {
            bssl::UniquePtr<X509_STORE> store(X509_STORE_new());
            if (store == nullptr) {
                return false;
            }
            for (const bssl::UniquePtr<X509>& root : *g_mozilla) {
                X509_STORE_add_cert(store.get(), root.get());
            }
            for (const bssl::UniquePtr<X509>& root : tls.roots->certificates) {
                X509_STORE_add_cert(store.get(), root.get());
            }
            if (!SSL_set1_verify_cert_store(ssl.get(), store.get())) {
                return false;
            }
        }
    }
    tls.rbio = rbio;
    tls.wbio = wbio;
    tls.ssl = ssl.release();
    return true;
}

// I/O thread, no lock (the Tls is this thread's alone): the handshake, the mod's bytes encrypted, the other side's
// decrypted into `in` while it holds less than `most`, and what TLS wrote moved to wire_out.
void tls_work(Tls& tls, std::string& in, std::size_t most) {
    if (tls.failed != LUMA_OK) {
        return;
    }
    ERR_clear_error();
    if (tls.ssl == nullptr && !tls_start(tls)) {
        tls.failed = LUMA_E_IO;
        ERR_clear_error();
        return;
    }
    if (!tls.wire_in.empty() && BIO_pending(tls.rbio) < kWireInMost) {
        if (BIO_write(tls.rbio, tls.wire_in.data(), static_cast<int>(tls.wire_in.size())) !=
            static_cast<int>(tls.wire_in.size())) {
            tls.failed = LUMA_E_MEMORY;
            return;
        }
        tls.wire_in.clear();
    }
    if (tls.eof && tls.wire_in.empty()) {
        BIO_set_mem_eof_return(tls.rbio, 0);  // its last bytes are in: a read past them is the end, not "wait"
    }
    if (!tls.handshaken) {
        const int done = SSL_do_handshake(tls.ssl);
        if (done == 1) {
            tls.handshaken = true;
        } else {
            const int error = SSL_get_error(tls.ssl, done);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
                const long verify = SSL_get_verify_result(tls.ssl);
                if (!tls.server && verify != X509_V_OK) {
                    tls.failed = LUMA_E_DENIED;
                    try {
                        tls.refusal = X509_verify_cert_error_string(verify);
                    } catch (...) {
                    }
                } else {
                    tls.failed = tls.server ? LUMA_E_DENIED : LUMA_E_IO;
                }
            }
        }
    }
    if (tls.handshaken) {
        while (!tls.plain_out.empty() && tls.wire_out.size() < kPlainOutMost) {
            const std::size_t piece = std::min<std::size_t>(tls.plain_out.size(), 64u << 10u);
            const int put = SSL_write(tls.ssl, tls.plain_out.data(), static_cast<int>(piece));
            if (put <= 0) {
                tls.eof = true;  // a write TLS refuses: the connection is over
                tls.broke = true;
                tls.plain_out.clear();
                break;
            }
            tls.plain_out.erase(0, static_cast<std::size_t>(put));
        }
        while (!tls.ended && in.size() < most) {
            const std::size_t before = in.size();
            const std::size_t room = std::min<std::size_t>(64u << 10u, most - before);
            try {
                in.resize(before + room);
            } catch (...) {
                break;
            }
            const int got = SSL_read(tls.ssl, in.data() + before, static_cast<int>(room));
            in.resize(before + (got > 0 ? static_cast<std::size_t>(got) : 0u));
            if (got > 0) {
                continue;
            }
            const int error = SSL_get_error(tls.ssl, got);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                break;
            }
            tls.ended = true;  // a close_notify, the end of the socket's bytes, or a fault
            tls.close_notify = error == SSL_ERROR_ZERO_RETURN;
            break;
        }
        if (tls.closing && tls.plain_out.empty() && !tls.shutdown_sent) {
            SSL_shutdown(tls.ssl);  // close_notify, after the mod's last bytes
            tls.shutdown_sent = true;
        }
    }
    for (std::size_t waiting = BIO_pending(tls.wbio); waiting > 0u; waiting = BIO_pending(tls.wbio)) {
        const std::size_t before = tls.wire_out.size();
        try {
            tls.wire_out.resize(before + waiting);
        } catch (...) {
            tls.failed = LUMA_E_MEMORY;
            break;
        }
        const int got = BIO_read(tls.wbio, tls.wire_out.data() + before, static_cast<int>(waiting));
        tls.wire_out.resize(before + (got > 0 ? static_cast<std::size_t>(got) : 0u));
        if (got <= 0) {
            break;
        }
    }
    ERR_clear_error();
}

// With g_lock, I/O thread: wire_out sent as far as the socket takes it; a stream's SENT once the mod's bytes are all
// out.
void tls_flush_locked(Socket* socket) {
    Tls& tls = *socket->tls;
    if (!tls.wire_out.empty() && write_stream_locked(socket, tls.wire_out) < 0) {
        tls.eof = true;
        tls.broke = true;
    }
    socket->tls_queued = tls.plain_out.size() + tls.wire_out.size();
    if (socket->http == nullptr && tls.moved > 0u && socket->outgoing.empty() && socket->tls_queued == 0u &&
        !socket->closing) {
        socket->sent_since += tls.moved;
        tls.moved = 0;
        socket->sent_event = true;
        post_locked(socket);
    }
}

// With g_lock, I/O thread: an open TLS socket's turn after the poll: the encrypted bytes in and out, the mod's bytes
// to TLS once the handshake is done (a server connection's wait queued until then).
void tls_turn_locked(Socket* socket, short revents) {
    Tls& tls = *socket->tls;
    tls.closing = socket->closing;
    if (!tls.eof && tls.wire_in.size() < kWireInMost && (revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        bool broke = false;
        if (!read_stream_locked(socket, tls.wire_in, kWireInMost, &broke)) {
            tls.eof = true;
            tls.broke = broke;
        }
    }
    if (tls.handshaken && !socket->outgoing.empty() && tls.plain_out.size() < kPlainOutMost) {
        try {
            tls.plain_out += socket->outgoing;
            tls.moved += socket->outgoing.size();
            socket->outgoing.clear();
        } catch (...) {
        }
    }
    tls_flush_locked(socket);
}

// With g_lock, I/O thread, after tls_work: a TLS stream's outcome: its handshake's end (CONNECTED, or ERROR with the
// certificate's refusal), decrypted bytes to `received`, its end (CLOSED), the mod's close (close_notify first).
void tls_settle_locked(Socket* socket) {
    Tls& tls = *socket->tls;
    const auto now = std::chrono::steady_clock::now();
    if (socket->closing) {
        tls.closing = true;
        tls_flush_locked(socket);
        if (!tls.handshaken || tls.broke || tls.failed != LUMA_OK || (tls.shutdown_sent && tls.wire_out.empty()) ||
            now - socket->started > kCloseTime) {
            close_locked(socket);
        }
        return;
    }
    if (tls.failed == LUMA_OK && !tls.handshaken && connect_late(socket, now)) {
        tls.failed = LUMA_E_TIMEOUT;
    }
    if (tls.failed != LUMA_OK) {
        close_locked(socket);
        if (tls.server) {
            socket->received.clear();  // a connection whose handshake failed: its CLOSED (contract: LUMA_E_DENIED)
            socket->closed_event = true;
            socket->closed_status = LUMA_E_DENIED;
        } else {
            socket->failed = tls.failed;
            if (!tls.refusal.empty()) {
                socket->report_code = 251u;  // Net.CertificateRefused, given with the ERROR
                socket->report_a = tls.host;
                socket->report_b = tls.refusal;
            }
        }
        post_locked(socket);
        return;
    }
    if (tls.handshaken && !tls.announced) {
        tls.announced = true;
        socket->tls_ready = true;
        if (!tls.server) {
            socket->connected = true;
            post_locked(socket);
        }
    }
    if (!tls.plain_in.empty() && socket->received.size() < LUMA_NET_RECEIVED_MOST) {
        const std::size_t moved = std::min(tls.plain_in.size(), LUMA_NET_RECEIVED_MOST - socket->received.size());
        try {
            socket->received.append(tls.plain_in, 0, moved);
            tls.plain_in.erase(0, moved);
            post_locked(socket);
        } catch (...) {
        }
    }
    tls_flush_locked(socket);
    if (tls.ended) {
        socket->peer_closed = true;  // sends answer LUMA_E_CLOSED from here
        if (tls.plain_in.empty() && socket->received.empty()) {
            close_locked(socket);
            socket->closed_event = true;
            socket->closed_status = tls.close_notify ? LUMA_OK : LUMA_E_IO;
            post_locked(socket);
        }
    }
}

// ---- HTTP answers (I/O thread, no lock: the Http is this thread's alone) ----

bool same_text(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); i++) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (x != y) {
            return false;
        }
    }
    return true;
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

// RFC 9110's token: a header's name.
bool token(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!plain && std::string_view("!#$%&'*+-.^_`|~").find(c) == std::string_view::npos) {
            return false;
        }
    }
    return true;
}

bool redirect_status(std::uint32_t status) {
    return status == 301u || status == 302u || status == 303u || status == 307u || status == 308u;
}

// The body ends: a gzip body still gives what its inflater holds (Flush), then Done.
void end_body(Http& h) {
    h.stage = h.zs != nullptr ? Http::Stage::Flush : Http::Stage::Done;
}

// The answer's status line and header lines (`head` without the empty line that ends it, each line with its CRLF).
// False: not an HTTP answer.
bool read_head(Http& h, std::string_view head) {
    const std::size_t first = head.find("\r\n");
    const std::string_view line = head.substr(0, first);
    if (line.size() < 12u || (line.substr(0, 9) != "HTTP/1.1 " && line.substr(0, 9) != "HTTP/1.0 ") ||
        (line.size() > 12u && line[12] != ' ')) {
        return false;
    }
    std::uint32_t status = 0;
    for (std::size_t i = 9; i < 12u; i++) {
        if (line[i] < '0' || line[i] > '9') {
            return false;
        }
        status = status * 10u + static_cast<std::uint32_t>(line[i] - '0');
    }
    if (status < 100u || status > 599u || status == 101u) {
        return false;  // 101 switches protocols, which no request asks for
    }
    h.status = status;
    bool has_length = false;
    std::uint64_t length = 0;
    bool chunked = false;
    std::string encoding;
    std::string location;
    std::string_view rest = head.substr(first + 2u);
    while (!rest.empty()) {
        const std::size_t end = rest.find("\r\n");
        const std::string_view field = rest.substr(0, end);
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 2u);
        if (field.empty() || field.front() == ' ' || field.front() == '\t') {
            return false;  // an empty line inside, or an obsolete folded line
        }
        const std::size_t colon = field.find(':');
        if (colon == std::string_view::npos || !token(field.substr(0, colon))) {
            return false;
        }
        const std::string_view name = field.substr(0, colon);
        const std::string_view value = trimmed(field.substr(colon + 1u));
        if (same_text(name, "content-length")) {
            if (value.empty() || value.size() > 18u) {
                return false;
            }
            std::uint64_t n = 0;
            for (const char c : value) {
                if (c < '0' || c > '9') {
                    return false;
                }
                n = n * 10u + static_cast<std::uint64_t>(c - '0');
            }
            if (has_length && n != length) {
                return false;  // two lengths that disagree
            }
            has_length = true;
            length = n;
        } else if (same_text(name, "transfer-encoding")) {
            const std::size_t comma = value.rfind(',');
            const std::string_view last = trimmed(comma == std::string_view::npos ? value : value.substr(comma + 1u));
            if (!same_text(last, "chunked")) {
                return false;  // a body framed some other way cannot be read
            }
            chunked = true;
        } else if (same_text(name, "content-encoding")) {
            encoding.assign(value);
        } else if (same_text(name, "location")) {
            location.assign(value);
        }
    }
    if (status < 200u) {
        return true;  // an interim answer (100, 103): the real one follows
    }
    if (redirect_status(status) && (h.flags & LUMA_NET_HTTP_NO_REDIRECT) == 0u && !location.empty()) {
        h.redirect = true;
        h.location = std::move(location);
        return true;
    }
    h.head.assign(head.substr(first + 2u));
    h.head_ready = true;
    if (h.method == "HEAD" || status == 204u || status == 304u) {
        h.stage = Http::Stage::Done;  // no body
        return true;
    }
    const bool gzip = (h.flags & LUMA_NET_HTTP_RAW) == 0u && (same_text(encoding, "gzip") || same_text(encoding, "x-gzip"));
    if (gzip) {
        h.zs = new (std::nothrow) z_stream{};
        if (h.zs == nullptr || inflateInit2(h.zs, 16 + MAX_WBITS) != Z_OK) {
            delete h.zs;
            h.zs = nullptr;
            h.failed = LUMA_E_MEMORY;
            return true;
        }
    }
    if (chunked) {
        h.stage = Http::Stage::ChunkSize;
    } else if (has_length) {
        if (!gzip && length > LUMA_NET_BODY_MOST) {
            h.failed = LUMA_E_TOO_LARGE;  // said at once: no byte of it is read
            return true;
        }
        h.left = length;
        h.stage = Http::Stage::Length;
        if (length == 0u) {
            end_body(h);
        }
    } else {
        h.stage = Http::Stage::ToEnd;  // to the connection's end
    }
    return true;
}

// Up to `n` bytes of body (as sent) into body_out, gzip undone, while body_out has room. The bytes of `data` used.
std::size_t give(Http& h, const char* data, std::size_t n) {
    const std::size_t room = h.body_out.size() < kBodyOutMost ? kBodyOutMost - h.body_out.size() : 0u;
    if (room == 0u) {
        return 0;
    }
    if (h.zs == nullptr) {
        const std::size_t taken = std::min(n, room);
        if (h.total + taken > LUMA_NET_BODY_MOST) {
            h.failed = LUMA_E_TOO_LARGE;
            return 0;
        }
        h.body_out.append(data, taken);
        h.total += taken;
        return taken;
    }
    if (h.z_ended) {
        if (n > 0u) {
            h.failed = LUMA_E_FORMAT;  // bytes after the gzip data's end
        }
        return 0;
    }
    const std::size_t before = h.body_out.size();
    h.body_out.resize(before + room);
    const std::size_t given = std::min<std::size_t>(n, 1u << 30u);
    h.zs->next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    h.zs->avail_in = static_cast<uInt>(given);
    h.zs->next_out = reinterpret_cast<Bytef*>(h.body_out.data() + before);
    h.zs->avail_out = static_cast<uInt>(room);
    const int result = inflate(h.zs, Z_NO_FLUSH);
    const std::size_t produced = room - h.zs->avail_out;
    const std::size_t used = given - h.zs->avail_in;
    h.body_out.resize(before + produced);
    if (result == Z_STREAM_END) {
        h.z_ended = true;
    } else if (result != Z_OK && result != Z_BUF_ERROR) {
        h.failed = LUMA_E_FORMAT;  // not gzip, or broken
        return 0;
    }
    h.total += produced;
    if (h.total > LUMA_NET_BODY_MOST) {
        h.failed = LUMA_E_TOO_LARGE;
        return 0;
    }
    return used;
}

// The answer's bytes in `raw` parsed as far as they go (`ended`: no more will come).
void http_parse(Http& h, bool ended) {
    for (;;) {
        if (h.failed != LUMA_OK || h.redirect) {
            return;
        }
        switch (h.stage) {
        case Http::Stage::Head: {
            const std::size_t end = h.raw.find("\r\n\r\n");
            if (end == std::string::npos || end + 2u > kHeadMost) {
                if (h.raw.size() > kHeadMost) {
                    h.failed = LUMA_E_FORMAT;
                } else if (ended) {
                    h.failed = h.raw.empty() ? LUMA_E_IO : LUMA_E_FORMAT;  // closed with no answer, or half of one
                }
                return;
            }
            if (!read_head(h, std::string_view(h.raw).substr(0, end + 2u))) {
                h.failed = LUMA_E_FORMAT;
                return;
            }
            h.raw.erase(0, end + 4u);
            if (h.status < 200u) {
                h.status = 0;  // an interim answer: the next one
            }
            continue;
        }
        case Http::Stage::Length:
        case Http::Stage::ChunkData: {
            if (h.raw.empty()) {
                if (ended) {
                    h.failed = LUMA_E_IO;  // the body was cut short
                }
                return;
            }
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(h.left, h.raw.size()));
            const std::size_t used = give(h, h.raw.data(), n);
            h.raw.erase(0, used);
            h.left -= used;
            if (h.failed != LUMA_OK) {
                return;
            }
            if (h.left == 0u) {
                if (h.stage == Http::Stage::ChunkData) {
                    h.stage = Http::Stage::ChunkEnd;
                } else {
                    end_body(h);
                }
                continue;
            }
            if (used < n) {
                return;  // the body's room is full: the mod takes first
            }
            continue;
        }
        case Http::Stage::ToEnd: {
            if (!h.raw.empty()) {
                const std::size_t used = give(h, h.raw.data(), h.raw.size());
                h.raw.erase(0, used);
                if (h.failed != LUMA_OK || !h.raw.empty()) {
                    return;
                }
            }
            if (ended) {
                end_body(h);
                continue;
            }
            return;
        }
        case Http::Stage::ChunkSize: {
            const std::size_t end = h.raw.find("\r\n");
            if (end == std::string::npos) {
                if (h.raw.size() > 1024u) {
                    h.failed = LUMA_E_FORMAT;
                } else if (ended) {
                    h.failed = LUMA_E_IO;
                }
                return;
            }
            std::uint64_t size = 0;
            std::size_t digits = 0;
            for (; digits < end; digits++) {
                const char c = h.raw[digits];
                const int value = c >= '0' && c <= '9' ? c - '0'
                                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                         : -1;
                if (value < 0) {
                    break;
                }
                if (digits >= 15u) {
                    h.failed = LUMA_E_FORMAT;  // a chunk past any body this service takes
                    return;
                }
                size = size * 16u + static_cast<std::uint64_t>(value);
            }
            if (digits == 0u || (digits < end && h.raw[digits] != ';' && h.raw[digits] != ' ' && h.raw[digits] != '\t')) {
                h.failed = LUMA_E_FORMAT;
                return;
            }
            h.raw.erase(0, end + 2u);
            if (size == 0u) {
                h.stage = Http::Stage::Trailer;
            } else {
                h.left = size;
                h.stage = Http::Stage::ChunkData;
            }
            continue;
        }
        case Http::Stage::ChunkEnd:
            if (h.raw.size() < 2u) {
                if (ended) {
                    h.failed = LUMA_E_IO;
                }
                return;
            }
            if (h.raw[0] != '\r' || h.raw[1] != '\n') {
                h.failed = LUMA_E_FORMAT;
                return;
            }
            h.raw.erase(0, 2);
            h.stage = Http::Stage::ChunkSize;
            continue;
        case Http::Stage::Trailer: {
            const std::size_t end = h.raw.find("\r\n");
            if (end == std::string::npos) {
                if (h.raw.size() > kHeadMost) {
                    h.failed = LUMA_E_FORMAT;
                } else if (ended) {
                    h.failed = LUMA_E_IO;
                }
                return;
            }
            h.raw.erase(0, end + 2u);  // a trailer field is not kept
            if (end == 0u) {
                end_body(h);
            }
            continue;
        }
        case Http::Stage::Flush: {
            if (h.z_ended) {
                h.stage = Http::Stage::Done;
                continue;
            }
            if (h.body_out.size() >= kBodyOutMost) {
                return;  // the mod takes first
            }
            const std::size_t before = h.body_out.size();
            give(h, nullptr, 0);
            if (h.failed != LUMA_OK) {
                return;
            }
            if (!h.z_ended && h.body_out.size() == before) {
                h.failed = LUMA_E_FORMAT;  // the body ended inside the gzip data
                return;
            }
            continue;
        }
        case Http::Stage::Done:
            return;
        }
    }
}

// I/O thread, no lock: a request's TLS (https) and its answer's parsing.
void http_work(Socket* socket) {
    Http& h = *socket->http;
    if (socket->tls != nullptr) {
        tls_work(*socket->tls, h.raw, kRawMost);
    }
    if (h.failed != LUMA_OK || h.redirect || h.stage == Http::Stage::Done) {
        return;
    }
    try {
        http_parse(h, socket->tls != nullptr ? socket->tls->ended : h.eof);
    } catch (...) {
        h.failed = LUMA_E_MEMORY;
    }
}

// With g_lock, I/O thread: a plain request's turn after the poll: the answer's bytes in, the request out.
void http_plain_turn_locked(Socket* socket, short revents) {
    Http& h = *socket->http;
    if (socket->closing) {
        close_locked(socket);  // cancelled: nothing more is sent or heard
        return;
    }
    if (!h.eof && h.raw.size() < kRawMost && (revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        bool broke = false;
        if (!read_stream_locked(socket, h.raw, kRawMost, &broke)) {
            h.eof = true;
            h.broke = broke;
        }
    }
    if (!socket->outgoing.empty() && write_stream_locked(socket, socket->outgoing) < 0) {
        h.eof = true;
        h.broke = true;
    }
}

// The request's text: its line, the service's headers, the mod's, its body.
std::string request_text(const Http& h) {
    std::string text;
    text.reserve(256u + h.target.size() + h.lines.size() + h.body.size());
    text += h.method;
    text += ' ';
    text += h.target;
    text += " HTTP/1.1\r\nHost: ";
    const bool v6 = h.host.find(':') != std::string::npos;
    text += v6 ? "[" + h.host + "]" : h.host;
    if (h.port != (h.https ? 443u : 80u)) {
        text += ':';
        text += std::to_string(h.port);
    }
    text += "\r\n";
    if (!h.user_agent) {
        text += "User-Agent: Nebula/" NEBULA_VERSION " ";
        text += h.mod_id;
        text += "\r\n";
    }
    if ((h.flags & LUMA_NET_HTTP_RAW) == 0u) {
        text += "Accept-Encoding: gzip\r\n";
    }
    text += "Connection: close\r\n";
    if (!h.body.empty() || h.method == "POST" || h.method == "PUT" || h.method == "PATCH") {
        text += "Content-Length: ";
        text += std::to_string(h.body.size());
        text += "\r\n";
    }
    text += h.lines;
    text += "\r\n";
    text += h.body;
    return text;
}

// Where a request goes: from its url, or a redirect's Location against the request it answers.
struct Target {
    bool https = false;
    std::string host;  // lower case, an IPv6 address without brackets
    std::uint16_t port = 0;
    std::string path;  // starts with '/'
};

// The path and query of a url's rest (fragment left out; "/" when there is none); false: not as on the wire.
bool url_path(std::string_view rest, std::string* out) {
    const std::size_t hash = rest.find('#');
    rest = rest.substr(0, hash);
    for (const char c : rest) {
        if (static_cast<unsigned char>(c) <= 0x20u || static_cast<unsigned char>(c) >= 0x7Fu) {
            return false;
        }
    }
    if (rest.empty() || rest.front() == '?') {
        *out = "/";
        out->append(rest);
    } else if (rest.front() == '/') {
        out->assign(rest);
    } else {
        return false;
    }
    return true;
}

// "http://" or "https://", the host (an IPv6 address in brackets), an optional ":port", the path and query.
bool parse_url(std::string_view url, Target* out) {
    if (url.size() > LUMA_NET_URL_MOST) {
        return false;
    }
    std::string_view rest;
    if (url.size() > 7u && same_text(url.substr(0, 7), "http://")) {
        out->https = false;
        rest = url.substr(7);
    } else if (url.size() > 8u && same_text(url.substr(0, 8), "https://")) {
        out->https = true;
        rest = url.substr(8);
    } else {
        return false;
    }
    const std::size_t end = rest.find_first_of("/?#");
    const std::string_view authority = rest.substr(0, end);
    if (!url_path(end == std::string_view::npos ? std::string_view() : rest.substr(end), &out->path)) {
        return false;
    }
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return false;  // no host, or a user name (never sent)
    }
    std::string_view host = authority;
    std::string_view port;
    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        host = authority.substr(1, close - 1u);
        const std::string_view after = authority.substr(close + 1u);
        if (!after.empty()) {
            if (after.front() != ':') {
                return false;
            }
            port = after.substr(1);
        }
        in6_addr v6{};
        if (inet_pton(AF_INET6, std::string(host).c_str(), &v6) != 1) {
            return false;
        }
    } else if (const std::size_t colon = authority.rfind(':'); colon != std::string_view::npos) {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1u);
    }
    if (host.empty()) {
        return false;
    }
    std::uint32_t number = out->https ? 443u : 80u;
    if (authority.back() == ':' && port.empty()) {
        return false;
    }
    if (!port.empty()) {
        if (port.size() > 5u) {
            return false;
        }
        number = 0;
        for (const char c : port) {
            if (c < '0' || c > '9') {
                return false;
            }
            number = number * 10u + static_cast<std::uint32_t>(c - '0');
        }
        if (number == 0u || number > 65535u) {
            return false;
        }
    }
    out->port = static_cast<std::uint16_t>(number);
    out->host.assign(host);
    for (char& c : out->host) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return true;
}

// A redirect's Location against the request it answers: absolute, "//host...", "/path" or a path beside the current.
bool resolve_location(const Http& h, std::string_view location, Target* out) {
    if ((location.size() > 7u && same_text(location.substr(0, 7), "http://")) ||
        (location.size() > 8u && same_text(location.substr(0, 8), "https://"))) {
        return parse_url(location, out);
    }
    if (location.size() > 2u && location.substr(0, 2) == "//") {
        return parse_url(std::string(h.https ? "https:" : "http:") + std::string(location), out);
    }
    out->https = h.https;
    out->host = h.host;
    out->port = h.port;
    if (location.size() > LUMA_NET_URL_MOST) {
        return false;
    }
    if (!location.empty() && location.front() == '/') {
        return url_path(location, &out->path);
    }
    const std::string_view current = std::string_view(h.target).substr(0, h.target.find('?'));
    const std::string base(current.substr(0, current.rfind('/') + 1u));
    return url_path(base + std::string(location), &out->path);
}

// With g_lock, I/O thread: a request follows its answer's redirect: the host checked (net.internet: luma.json's
// hosts; net.local: this machine or the local network; never from https to http), the old connection ended, a new
// one made (a lookup on its own thread for net.internet), the request sent again (303, and 301/302 after a POST,
// as GET without its body).
void start_redirect_locked(Socket* socket) {
    Http& h = *socket->http;
    h.redirect = false;
    const auto fail = [socket](std::int32_t status) {
        close_locked(socket);
        socket->failed = status;
        post_locked(socket);
    };
    const auto refuse = [socket, &h, &fail]() {
        socket->report_code = 252u;  // Net.RedirectRefused, given with the ERROR
        socket->report_a = h.location.substr(0, LUMA_NET_URL_MOST);
        fail(LUMA_E_DENIED);
    };
    if (h.redirects >= LUMA_NET_REDIRECTS_MOST) {
        fail(LUMA_E_LIMIT);
        return;
    }
    h.redirects++;
    Target target;
    if (!resolve_location(h, h.location, &target)) {
        fail(LUMA_E_FORMAT);
        return;
    }
    if (h.https && !target.https) {
        refuse();
        return;
    }
    sockaddr_storage address{};
    socklen_t length = 0;
    if (socket->internet) {
        if (host_text(target.host.c_str()).empty() || !host_named(h.hosts, target.host)) {
            refuse();
            return;
        }
    } else if (!local_address(target.host.c_str(), target.port, &address, &length)) {
        refuse();
        return;
    }
    if (h.status == 303u || ((h.status == 301u || h.status == 302u) && h.method == "POST")) {
        if (h.method != "HEAD") {
            h.method = "GET";
        }
        h.body.clear();
    }
    h.https = target.https;
    h.host = std::move(target.host);
    h.port = target.port;
    h.target = std::move(target.path);
    if (socket->sock != kNoSock) {
        close_system(socket->sock);  // the old connection (not system_closed: the request goes on)
        socket->sock = kNoSock;
    }
    h.raw.clear();
    h.stage = Http::Stage::Head;
    h.eof = false;
    h.broke = false;
    h.status = 0;
    h.end_inflate();
    socket->tls.reset();
    socket->tls_ready = false;
    socket->tls_queued = 0;
    try {
        if (h.https) {
            socket->tls = std::make_unique<Tls>();
            socket->tls->host = h.host;
            socket->tls->roots = h.roots;
        }
        socket->outgoing = request_text(h);
    } catch (...) {
        fail(LUMA_E_MEMORY);
        return;
    }
    socket->started = std::chrono::steady_clock::now();
    if (socket->internet) {
        socket->kind = Kind::Resolving;
        socket->looked_up = false;
        socket->lookup_status = LUMA_OK;
        socket->addresses.clear();
        socket->next_address = 0;
        socket->resolving = true;
        try {
            std::thread(look_up, socket, h.host, h.port).detach();
        } catch (...) {
            socket->resolving = false;
            socket->looked_up = true;
            socket->lookup_status = LUMA_E_IO;  // no thread: the Resolving turn gives the ERROR
        }
        return;
    }
    const Sock sock = ::socket(address.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == kNoSock) {
        fail(LUMA_E_IO);
        return;
    }
    if (!make_nonblocking(sock)) {
        close_system(sock);
        fail(LUMA_E_IO);
        return;
    }
    int on = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
    socket->sock = sock;
    socket->kind = Kind::Connecting;
    refuse_fast(sock, address);
    if (::connect(sock, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
        const int error = system_error();
        if (!connecting(error)) {
            socket->failed = connect_failure(socket, error);  // the Connecting turn closes it and gives the ERROR
        }
    }
}

// With g_lock, I/O thread, after http_work: a request's outcome: HEADERS, the body to `received`, DONE, a redirect,
// or ERROR (its time, its TLS, its answer).
void http_settle_locked(Socket* socket) {
    Http& h = *socket->http;
    if (socket->closing) {
        close_locked(socket);  // cancelled: no event from here
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (socket->tls != nullptr) {
        Tls& tls = *socket->tls;
        if (tls.failed == LUMA_OK && !tls.handshaken && connect_late(socket, now)) {
            tls.failed = LUMA_E_TIMEOUT;
        }
        if (tls.failed != LUMA_OK && h.failed == LUMA_OK) {
            h.failed = tls.failed;
            if (!tls.refusal.empty()) {
                socket->report_code = 251u;  // Net.CertificateRefused
                socket->report_a = tls.host;
                socket->report_b = tls.refusal;
            }
        }
        if (tls.handshaken) {
            tls.announced = true;
            socket->tls_ready = true;
        }
        tls_flush_locked(socket);
    }
    if (h.failed == LUMA_OK && h.stage != Http::Stage::Done && now > h.deadline) {
        h.failed = LUMA_E_TIMEOUT;
    }
    if (h.failed != LUMA_OK) {
        close_locked(socket);
        socket->received.clear();
        socket->failed = h.failed;
        post_locked(socket);
        return;
    }
    if (h.redirect) {
        start_redirect_locked(socket);
        return;
    }
    if (h.head_ready && !h.head_given) {
        h.head_given = true;
        socket->headers_event = true;
        socket->http_status = h.status;
        socket->http_headers = std::move(h.head);
        post_locked(socket);
    }
    if (!h.body_out.empty() && socket->received.size() < LUMA_NET_RECEIVED_MOST) {
        const std::size_t moved = std::min(h.body_out.size(), LUMA_NET_RECEIVED_MOST - socket->received.size());
        try {
            socket->received.append(h.body_out, 0, moved);
            h.body_out.erase(0, moved);
            post_locked(socket);
        } catch (...) {
        }
    }
    if (h.stage == Http::Stage::Done && h.body_out.empty() && !socket->done_event) {
        socket->done_event = true;
        socket->body_total = h.total;
        close_locked(socket);  // Connection: close; the Socket stays for its last events
        post_locked(socket);
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
#if defined(_WIN32)
    if (socket->pipe != nullptr) {
        pipe_turn_locked(socket);
        return;
    }
#endif
    switch (socket->kind) {
    case Kind::Resolving: {
        if (socket->closing) {
            close_locked(socket);  // no system socket yet: nothing to send; freed once its lookup lets go
            return;
        }
        if (!socket->looked_up) {
            if (connect_late(socket, std::chrono::steady_clock::now())) {
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
            if (connect_late(socket, std::chrono::steady_clock::now())) {
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
                socket->failed = error != 0 ? connect_failure(socket, error)
                                            : (socket->ipc ? LUMA_E_NOT_FOUND : LUMA_E_UNREACHABLE);
                post_locked(socket);
            }
            return;
        }
        socket->kind = socket->udp ? Kind::Udp : Kind::Open;  // a UDP socket's connect only sets its peer
        if (socket->closing) {
            break;  // closed by the mod while connecting: what it queued (nothing) is flushed below
        }
        if (socket->tls == nullptr && socket->http == nullptr) {
            socket->connected = true;  // a TLS stream's CONNECTED comes after its handshake; a request's HEADERS
            post_locked(socket);
        }
        break;
    }
    case Kind::Listener: {
        if (socket->closing) {
            // Closed by the mod: its system socket now, whether or not a connection waits (one waiting is refused by
            // the system; the ones accepted and not yet given were closed with it in close_on). Waiting for a POLLIN
            // here kept the port listening forever, and a later connect there was accepted, never refused (2702).
            close_locked(socket);
            return;
        }
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
            if (!socket->ipc && !socket->internet && !reach_local(from)) {
                close_system(got);  // a peer outside this machine and the local network: never the mod's (no event)
                refuse_peer_locked(socket);
                continue;
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
            fresh->ipc = socket->ipc;
            fresh->sock = got;
            fresh->where = socket->where;
            fresh->started = std::chrono::steady_clock::now();  // a TLS connection's handshake has 10 s from here
            if (socket->tls_server != nullptr) {
                fresh->tls.reset(new (std::nothrow) Tls());
                if (fresh->tls == nullptr) {
                    close_system(got);
                    delete fresh;
                    continue;
                }
                fresh->tls->server = true;
                fresh->tls->context = socket->tls_server;
            }
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
    if (socket->http != nullptr) {
        if (socket->tls == nullptr) {
            http_plain_turn_locked(socket, revents);
        } else if (socket->closing) {
            close_locked(socket);  // cancelled: nothing more is sent or heard
        } else {
            tls_turn_locked(socket, revents);
        }
        return;
    }
    if (socket->tls != nullptr) {
        tls_turn_locked(socket, revents);  // its outcome: tls_settle_locked, after the unlocked work
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
                socket->own_due -= std::min<std::uint64_t>(socket->own_due, static_cast<std::uint64_t>(got));
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
                if (Socket* to = own_stream_end_locked(socket)) {
                    to->own_due += static_cast<std::uint64_t>(put);
                }
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

// With g_lock, I/O thread: what an open TLS stream or request waits for in the poll.
short stream_events(const Socket* socket) {
    bool in = false;
    bool out = false;
    if (socket->tls != nullptr) {
        const Tls& tls = *socket->tls;
        in = !tls.eof && tls.wire_in.size() < kWireInMost;
        out = !tls.wire_out.empty();
    } else {
        const Http& h = *socket->http;
        in = !h.eof && h.raw.size() < kRawMost;
        out = !socket->outgoing.empty();
    }
    return static_cast<short>((in ? POLLIN : 0) | (out ? POLLOUT : 0));
}

// With g_lock, I/O thread, at a pass's end: a settle asked before the pass began is answered when nothing this process
// sent itself is on its way (frame_start).
void settle_check_locked(std::uint64_t asked) {
    if (asked <= g_io->settle_done) {
        return;
    }
    for (Socket* socket : g_sockets) {
        if (socket->system_closed) {
            continue;
        }
        if (socket->own_due != 0u) {
            return;  // sent here by this process, not read yet
        }
        if (!socket->udp && socket->kind == Kind::Connecting && socket->failed == LUMA_OK &&
            own_listener_locked(socket->connect_port) != nullptr) {
            return;  // connecting to a listener of this process
        }
        if (!socket->udp && socket->kind == Kind::Open && !socket->peer_closed && !is_pipe(socket) &&
            socket->connect_port != 0u && own_listener_locked(socket->connect_port) != nullptr &&
            own_stream_end_locked(socket) == nullptr) {
            return;  // connected there, and the listener has not accepted it yet
        }
        if (!socket->outgoing.empty() && own_stream_end_locked(socket) != nullptr) {
            return;  // queued for a socket of this process, not sent yet
        }
        for (const Datagram& datagram : socket->datagrams_out) {
            if (own_udp_locked(socket->tied ? socket->peer_port : loopback_port(datagram.address)) != nullptr) {
                return;
            }
        }
    }
    g_io->settle_done = asked;
    g_io->settled.notify_all();
}

void io_loop() {
    std::vector<Socket*> polled;
    std::vector<PollFd> fds;
    std::vector<Socket*> working;  // open TLS streams and requests: their work outside the lock
    for (;;) {
        polled.clear();
        fds.clear();
        std::uint64_t asked = 0;  // the settle asked when this pass began
        bool settling = false;    // and not answered yet
        {
            std::unique_lock<std::mutex> lock(g_lock);
            g_io->ready.wait(lock, [] {
                return std::any_of(g_sockets.begin(), g_sockets.end(), [](const Socket* s) { return !s->system_closed; });
            });
            asked = g_io->settle_asked;
            settling = asked > g_io->settle_done;
            try {
                for (Socket* socket : g_sockets) {
                    if (socket->system_closed || is_pipe(socket)) {
                        continue;  // a pipe is not a socket: its turn comes below
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
                        fd.events = socket->tls != nullptr || socket->http != nullptr
                                        ? stream_events(socket)
                                        : static_cast<short>(
                                              (!socket->peer_closed && socket->received.size() < LUMA_NET_RECEIVED_MOST
                                                   ? POLLIN
                                                   : 0) |
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
                if (g_io->wake != kNoSock) {
                    PollFd fd{};
                    fd.fd = g_io->wake;
                    fd.events = POLLIN;
                    fds.push_back(fd);  // last: no socket's, so `polled` has no entry for it
                }
            } catch (...) {
                polled.clear();
                fds.clear();
            }
        }
        // Outside the lock: the descriptors stay open (only this thread closes them). While a settle waits, what this
        // process sent itself may still be in the system's loopback: looked at again within a millisecond.
        const int wait_ms = !settling ? kPollMs : asked != g_io->settle_polled ? 0 : 1;
        g_io->settle_polled = asked;
        if (!fds.empty()) {
            poll_system(fds.data(), fds.size(), wait_ms);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        }
        if (fds.size() > polled.size() && (fds.back().revents & POLLIN) != 0) {
            // Drained, then open to the next wake: a wake between the two found the flag set and sent nothing, and
            // this pass reads the state it woke for (changed under g_lock before it woke).
            char drained[64];
            while (receive_system(g_io->wake, drained, sizeof(drained)) > 0) {
            }
            g_io->wake_sent.store(false, std::memory_order_release);
        }
        working.clear();
        {
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
            try {
                for (Socket* socket : g_sockets) {
                    if (!socket->system_closed && socket->kind == Kind::Open &&
                        (socket->tls != nullptr || socket->http != nullptr)) {
                        working.push_back(socket);
                    }
                }
            } catch (...) {
                working.clear();  // no memory now: the next round
            }
            if (working.empty()) {
                settle_check_locked(asked);
            }
        }
        if (working.empty()) {
            continue;
        }
        // Outside the lock: the handshakes, the encryption, the answers' parsing and gzip. Only a socket this thread
        // closed is ever freed, so each stays; its Tls and Http are this thread's alone.
        for (Socket* socket : working) {
            if (socket->http != nullptr) {
                http_work(socket);
            } else {
                tls_work(*socket->tls, socket->tls->plain_in, LUMA_NET_RECEIVED_MOST);
            }
        }
        const std::lock_guard<std::mutex> lock(g_lock);
        for (Socket* socket : working) {
            if (socket->system_closed) {
                continue;
            }
            if (socket->http != nullptr) {
                http_settle_locked(socket);
            } else {
                tls_settle_locked(socket);
            }
        }
        settle_check_locked(asked);
    }
}

// Deterministic mode (luma_service_provider.h frame_start), game thread, at each frame's start: what this process's
// sockets sent each other comes in and is posted before the frame's events are taken, so it is given on the same frame
// every run (the I/O thread runs in real time, the frames as fast as the game makes them).
constexpr auto kSettleMost = std::chrono::seconds(1);
void frame_start(void*) {
    if (g_io == nullptr || !g_io->started) {
        return;
    }
    std::unique_lock<std::mutex> lock(g_lock);
    if (std::none_of(g_sockets.begin(), g_sockets.end(), [](const Socket* s) { return !s->system_closed; })) {
        return;  // the I/O thread waits for a socket: nothing on its way
    }
    const std::uint64_t ask = ++g_io->settle_asked;
    wake_io();
    if (g_io->settled.wait_for(lock, kSettleMost, [ask] { return g_io->settle_done >= ask; })) {
        return;
    }
    // Lost on the way (a full loopback buffer drops a datagram): not waited for again.
    std::uint64_t due = 0;
    for (Socket* socket : g_sockets) {
        due += socket->own_due;
        socket->own_due = 0;
    }
    g_io->settle_done = std::max(g_io->settle_done, ask);
    lock.unlock();
    static bool said = false;
    if (!said && g_log != nullptr) {
        said = true;
        char line[200];
        std::snprintf(line, sizeof(line),
                      "LumaCore net: %llu datagrams or bytes this process sent itself had not come after 1 s; the frame went on",
                      static_cast<unsigned long long>(due));
        g_log(line);
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

// A report with up to two arguments after {where} (b empty: one).
void report_with(const LumaMod* mod, const char* where, std::uint32_t code, const std::string& a,
                 const std::string& b) noexcept {
    const char* args[2] = {a.c_str(), b.c_str()};
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, code, args, b.empty() ? 1u : 2u);
    }
}

void deliver_ws(Socket* socket, const LumaMod* mod);

void deliver(LumaEventNode* node) {
    Socket* const socket = reinterpret_cast<Node*>(node)->socket;
    const LumaMod* const mod = node->mod;
    if (socket->ws != nullptr) {  // set at its making, never changed
        deliver_ws(socket, mod);
        return;
    }
    bool connected = false;
    std::int32_t failed = LUMA_OK;
    std::vector<LumaHandle> accepted;
    bool data = false;
    std::uint64_t datagrams = 0;
    bool sent_given = false;
    std::uint64_t sent = 0;
    bool closed = false;
    std::int32_t closed_status = LUMA_OK;
    bool headers = false;
    std::uint32_t http_status = 0;
    bool done = false;
    std::uint64_t body_total = 0;
    std::uint32_t report_code = 0;
    std::string report_a;
    std::string report_b;
    bool peer_report = false;
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
        g_delivering = socket;
        fn = socket->fn;
        user = socket->user;
        handle = socket->handle;
        peer_report = std::exchange(socket->peer_report, false);
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
        headers = std::exchange(socket->headers_event, false) && failed == LUMA_OK;
        http_status = socket->http_status;
        if (socket->done_event && !data && failed == LUMA_OK) {
            done = true;  // the body all taken first (header): DONE once `received` is empty
            socket->done_event = false;
            body_total = socket->body_total;
        }
        if (failed != LUMA_OK) {
            report_code = std::exchange(socket->report_code, 0u);
            report_a = std::move(socket->report_a);
            report_b = std::move(socket->report_b);
        }
        if (failed != LUMA_OK || (closed && !data) || done) {
            uncount_locked(socket);
            socket->finished = true;  // the handle is closed after this event (header)
        }
    }
    const auto open = [socket] {
        const std::lock_guard<std::mutex> lock(g_lock);
        return !socket->closing;  // the mod may close it inside a call
    };
    if (peer_report) {
        // No address of the peer: the line says only that one came from outside and was refused.
        if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
            kernel->report(mod, socket->where != nullptr ? socket->where : LUMA_NET_LOCAL_NAME, 254u, nullptr, 0u);
        }
    }
    if (connected) {
        call(mod, service, fn, user, LUMA_NET_EVENT_CONNECTED, handle, LUMA_OK, 0u);
    }
    for (const LumaHandle fresh : accepted) {
        if (!open()) {
            break;
        }
        call(mod, service, fn, user, LUMA_NET_EVENT_ACCEPTED, handle, LUMA_OK, fresh);
    }
    if (headers && open()) {
        g_in_data = socket;
        g_in_headers = true;
        call(mod, service, fn, user, LUMA_NET_EVENT_HEADERS, handle, LUMA_OK, http_status);
        g_in_headers = false;
        g_in_data = nullptr;
        const std::lock_guard<std::mutex> lock(g_lock);
        std::string().swap(socket->http_headers);  // lines not taken are gone (header)
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
        if (report_code != 0u && socket->where != nullptr) {
            report_with(mod, socket->where, report_code, report_a, report_b);  // why, before the ERROR
        }
        call(mod, service, fn, user, LUMA_NET_EVENT_ERROR, handle, failed, 0u);
    } else if (done) {
        call(mod, service, fn, user, LUMA_NET_EVENT_DONE, handle, LUMA_OK, body_total);
    } else if (closed && !data) {
        call(mod, service, fn, user, LUMA_NET_EVENT_CLOSED, handle, closed_status, 0u);
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    if (!socket->closing && !socket->finished) {
        if (closed && data) {
            socket->closed_event = true;  // the bytes first (header): CLOSED once they are taken
        }
        if (!socket->received.empty() || !socket->datagrams_in.empty() || socket->closed_event ||
            socket->done_event) {
            post_locked(socket);  // bytes or datagrams left, or the CLOSED or DONE after them: a later frame
        }
    }
    g_delivering = nullptr;
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
    const Sock sock =
        ::socket(family, udp ? SOCK_DGRAM : SOCK_STREAM, family == AF_UNIX ? 0 : (udp ? IPPROTO_UDP : IPPROTO_TCP));
    if (sock == kNoSock) {
        return LUMA_E_IO;
    }
    if (!make_nonblocking(sock)) {
        close_system(sock);
        return LUMA_E_IO;
    }
    if (!udp && family != AF_UNIX) {
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
    wake_io();
    *out = socket->handle;
    return LUMA_OK;
}

// What tls_connect and http add to a connect: TLS (checking the connect's host), a request.
struct Extra {
    bool tls = false;
    std::unique_ptr<Http> http;
    std::unique_ptr<Ws> ws;
};

// The connect's TLS and request on its new Socket, not joined yet (the call that made it kept for its reports).
// False: no memory.
bool attach(Socket* socket, const char* where, Extra* extra, const char* host, const ModState* state) noexcept {
    socket->where = where;
    if (extra == nullptr) {
        return true;
    }
    try {
        if (extra->tls) {
            socket->tls = std::make_unique<Tls>();
            socket->tls->host = host;
            socket->tls->roots = state->roots;
            socket->tls_client = extra->http == nullptr;
        }
        if (extra->http != nullptr) {
            socket->outgoing = request_text(*extra->http);
            socket->http = std::move(extra->http);
        }
        if (extra->ws != nullptr) {
            socket->outgoing = extra->ws->request;  // sent once connected (TLS: after the handshake)
            socket->ws = std::move(extra->ws);
        }
    } catch (...) {
        socket->tls.reset();
        socket->outgoing.clear();
        return false;
    }
    return true;
}

// tcp_connect and udp_connect of net.local: a stream, or a datagram socket tied to one peer (`extra`: tls_connect's
// TLS, http's request).
std::int32_t local_connect(const LumaMod* mod, const char* where, bool udp, const char* host, std::uint16_t port,
                           LumaEventFn fn, void* user, LumaHandle* out, Extra* extra = nullptr) noexcept {
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
        if (!attach(socket, where, extra, host, state)) {
            close_system(socket->sock);
            delete socket;
            return LUMA_E_MEMORY;
        }
        socket->tied = udp;
        socket->connect_port = udp ? 0u : loopback_port(address);
        socket->kind = Kind::Connecting;  // a UDP connect only sets the peer: CONNECTED at the I/O thread's next turn
        socket->started = std::chrono::steady_clock::now();
        if (!udp) {
            refuse_fast(socket->sock, address);
        }
        if (::connect(socket->sock, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
            const int error = system_error();
            if (!connecting(error)) {
                // Refused at once (loopback with nobody listening, on some systems): an ERROR at the next frame.
                socket->failed = connect_failure(socket, error);
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
std::int32_t net_tls_connect(const LumaMod* mod, const char* host, std::uint16_t port, LumaEventFn fn, void* user,
                             LumaHandle* out) noexcept {
    Extra extra;
    extra.tls = true;
    return local_connect(mod, "net.local.tls_connect", false, host, port, fn, user, out, &extra);
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

// A file of the mod's own into *out (`given`: a path inside its folder, '/' between parts). LUMA_OK; a part empty,
// "." or "..", a drive or a separator (a root, a way out): LUMA_E_ARGUMENT; missing, or no folder: LUMA_E_NOT_FOUND;
// past kOwnFileMost: LUMA_E_FORMAT.
std::int32_t read_own_file(const std::filesystem::path& package, std::string_view given, std::string* out) {
    if (given.empty() || given.size() > 1024u) {
        return LUMA_E_ARGUMENT;
    }
    std::filesystem::path path = package;
    for (std::size_t start = 0;;) {
        const std::size_t slash = given.find('/', start);
        const std::string_view part =
            given.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (part.empty() || !galaxy::mods::safe_path_part(part)) {
            return LUMA_E_ARGUMENT;
        }
        path /= galaxy::text_path(std::string(part));
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1u;
    }
    if (package.empty()) {
        return LUMA_E_NOT_FOUND;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return LUMA_E_NOT_FOUND;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return LUMA_E_NOT_FOUND;
    }
    if (size > kOwnFileMost) {
        return LUMA_E_FORMAT;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return LUMA_E_NOT_FOUND;
    }
    out->resize(static_cast<std::size_t>(size));
    file.read(out->data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(file.gcount()) != size) {
        return LUMA_E_NOT_FOUND;
    }
    return LUMA_OK;
}

// tls_listen's context: the certificate chain (the server's first) and its private key, from the mod's own files.
std::int32_t server_context(const LumaMod* mod, const char* where, const ModState* state, const char* certificate,
                            const char* key, std::shared_ptr<SSL_CTX>* out) noexcept {
    if (certificate == nullptr) {
        return argument(mod, where, "certificate");
    }
    if (key == nullptr) {
        return argument(mod, where, "key");
    }
    try {
        const auto refuse = [mod, where](const char* name, const char* path, std::int32_t status) {
            if (status == LUMA_E_ARGUMENT) {
                return argument(mod, where, name);
            }
            report(mod, where, 253u, path);  // Net.FileRefused
            return status;
        };
        std::string chain_text;
        std::string key_text;
        if (const std::int32_t status = read_own_file(state->package, certificate, &chain_text); status != LUMA_OK) {
            return refuse("certificate", certificate, status);
        }
        if (const std::int32_t status = read_own_file(state->package, key, &key_text); status != LUMA_OK) {
            return refuse("key", key, status);
        }
        ERR_clear_error();
        bssl::UniquePtr<SSL_CTX> context(SSL_CTX_new(TLS_method()));
        if (context == nullptr || !SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION) ||
            !SSL_CTX_set_max_proto_version(context.get(), TLS1_3_VERSION)) {
            ERR_clear_error();
            return LUMA_E_MEMORY;
        }
        bool good = false;
        {
            bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(chain_text.data(), static_cast<ossl_ssize_t>(chain_text.size())));
            bssl::UniquePtr<X509> leaf(bio != nullptr ? PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr) : nullptr);
            good = leaf != nullptr && SSL_CTX_use_certificate(context.get(), leaf.get()) == 1;
            while (good) {
                bssl::UniquePtr<X509> next(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
                if (next == nullptr) {
                    break;  // the chain's end
                }
                good = SSL_CTX_add1_chain_cert(context.get(), next.get()) == 1;
            }
        }
        ERR_clear_error();
        if (!good) {
            return refuse("certificate", certificate, LUMA_E_FORMAT);
        }
        {
            bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(key_text.data(), static_cast<ossl_ssize_t>(key_text.size())));
            bssl::UniquePtr<EVP_PKEY> pkey(
                bio != nullptr ? PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr) : nullptr);
            good = pkey != nullptr && SSL_CTX_use_PrivateKey(context.get(), pkey.get()) == 1 &&
                   SSL_CTX_check_private_key(context.get()) == 1;
        }
        ERR_clear_error();
        if (!good) {
            return refuse("key", key, LUMA_E_FORMAT);  // not PEM, or not the certificate's key
        }
        SSL_CTX* const raw = context.release();
        *out = std::shared_ptr<SSL_CTX>(raw, SSL_CTX_free);  // a throw here frees raw
        return LUMA_OK;
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

// tcp_listen and tls_listen (`tls`: each connection's TLS with the mod's certificate and key).
std::int32_t listen_on(const LumaMod* mod, const char* where, std::uint16_t port, std::uint32_t local_network,
                       LumaEventFn fn, void* user, LumaHandle* out, bool tls, const char* certificate,
                       const char* key) noexcept {
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
    std::shared_ptr<SSL_CTX> server;
    if (tls) {
        if (const std::int32_t status = server_context(mod, where, state, certificate, key, &server);
            status != LUMA_OK) {
            return status;
        }
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
    socket->where = where;
    socket->tls_server = std::move(server);
    return join(socket, out);
}

std::int32_t net_tcp_listen(const LumaMod* mod, std::uint16_t port, std::uint32_t local_network, LumaEventFn fn,
                            void* user, LumaHandle* out) noexcept {
    return listen_on(mod, "net.local.tcp_listen", port, local_network, fn, user, out, false, nullptr, nullptr);
}
std::int32_t net_tls_listen(const LumaMod* mod, std::uint16_t port, std::uint32_t local_network,
                            const char* certificate, const char* key, LumaEventFn fn, void* user,
                            LumaHandle* out) noexcept {
    return listen_on(mod, "net.local.tls_listen", port, local_network, fn, user, out, true, certificate, key);
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
    if (socket == nullptr || socket->ipc || (socket->kind != Kind::Listener && !(socket->udp && !socket->tied))) {
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
            queued += other->outgoing.size() + other->datagrams_out_bytes + other->tls_queued;
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
        if (socket->http != nullptr || socket->ws != nullptr) {
            return bad_handle(mod, where);  // a request is sent whole by http; a WebSocket's messages by ws_send
        }
        if (socket->kind == Kind::Resolving || socket->kind == Kind::Connecting || socket->peer_closed ||
            socket->system_closed || (socket->tls_client && !socket->tls_ready)) {
            return LUMA_E_CLOSED;  // an answer (header): a TLS stream sends from its CONNECTED
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
    wake_io();
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
    if (socket == nullptr || socket != g_in_data || socket->udp || socket->ws != nullptr) {
        return bad_handle(mod, where);  // a UDP socket's datagrams: take_datagram; a WebSocket's: MESSAGE
    }
    std::string& from = g_in_headers ? socket->http_headers : socket->received;  // HEADERS: its header lines
    if (buf == nullptr) {
        if (size != 0u) {
            report(mod, where, 101u, "buf");
            return LUMA_E_ARGUMENT;
        }
        return static_cast<std::int64_t>(from.size());
    }
    const std::size_t taken = static_cast<std::size_t>(std::min<std::uint64_t>(size, from.size()));
    std::memcpy(buf, from.data(), taken);
    from.erase(0, taken);
    return static_cast<std::int64_t>(taken);
}

// ---- WebSockets (minor 4, game thread) ------------------------------------------------------------------------------

constexpr std::string_view kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";  // RFC 6455 1.3

std::string base64(const unsigned char* bytes, std::size_t n) {
    std::string out(((n + 2u) / 3u) * 4u + 1u, '\0');
    const std::size_t written = EVP_EncodeBlock(reinterpret_cast<std::uint8_t*>(out.data()), bytes, n);
    out.resize(written);
    return out;
}

// A client frame (RFC 6455 5.2): FIN, the opcode, the length, a mask of 4 random bytes, the payload masked.
std::string ws_frame(std::uint8_t op, std::string_view payload) {
    std::string frame;
    frame.reserve(payload.size() + 14u);
    frame.push_back(static_cast<char>(0x80u | op));
    const std::uint64_t n = payload.size();
    if (n < 126u) {
        frame.push_back(static_cast<char>(0x80u | n));
    } else if (n <= 0xFFFFu) {
        frame.push_back(static_cast<char>(0x80u | 126u));
        frame.push_back(static_cast<char>((n >> 8u) & 0xFFu));
        frame.push_back(static_cast<char>(n & 0xFFu));
    } else {
        frame.push_back(static_cast<char>(0x80u | 127u));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<char>((n >> shift) & 0xFFu));
        }
    }
    unsigned char mask[4] = {};
    RAND_bytes(mask, sizeof(mask));
    frame.append(reinterpret_cast<const char*>(mask), sizeof(mask));
    const std::size_t at = frame.size();
    frame.append(payload);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        frame[at + i] = static_cast<char>(static_cast<unsigned char>(frame[at + i]) ^ mask[i & 3u]);
    }
    return frame;
}

// With g_lock: a close frame queued (once: the stage ends), code 0 with no reason: an empty one.
void ws_close_frame_locked(Socket* socket, std::uint32_t code, std::string_view reason) {
    Ws& ws = *socket->ws;
    if (ws.stage == Ws::Stage::Ended || socket->peer_closed || socket->system_closed) {
        ws.stage = Ws::Stage::Ended;
        return;
    }
    ws.stage = Ws::Stage::Ended;
    try {
        std::string payload;
        if (code != 0u) {
            payload.push_back(static_cast<char>((code >> 8u) & 0xFFu));
            payload.push_back(static_cast<char>(code & 0xFFu));
            payload.append(reason.substr(0, 123u));
        }
        socket->outgoing += ws_frame(8u, payload);
    } catch (...) {
    }
}

// UTF-8 as RFC 3629 allows it (no overlongs, no surrogates, nothing past U+10FFFF).
bool utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        std::uint32_t point = 0;
        if (c < 0x80u) {
            ++i;
            continue;
        } else if (c >= 0xC2u && c <= 0xDFu) {
            extra = 1;
            point = c & 0x1Fu;
        } else if (c >= 0xE0u && c <= 0xEFu) {
            extra = 2;
            point = c & 0x0Fu;
        } else if (c >= 0xF0u && c <= 0xF4u) {
            extra = 3;
            point = c & 0x07u;
        } else {
            return false;
        }
        if (i + extra >= text.size()) {
            return false;  // cut short
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u) {
                return false;
            }
            point = (point << 6u) | (next & 0x3Fu);
        }
        if ((extra == 2u && (point < 0x800u || (point >= 0xD800u && point <= 0xDFFFu))) ||
            (extra == 3u && (point < 0x10000u || point > 0x10FFFFu))) {
            return false;
        }
        i += extra + 1u;
    }
    return true;
}

// The server's answer to the upgrade (its status line and header lines, without the empty line): 101 with Upgrade:
// websocket, Connection: Upgrade, the right Accept, no extension, a protocol only if one was asked.
bool ws_head_ok(Ws& ws, std::string_view head, std::uint32_t* status) {
    *status = 0;
    const std::size_t first = head.find("\r\n");
    const std::string_view line = head.substr(0, first);
    if (line.size() < 12u || line.substr(0, 5) != "HTTP/" || line[8] != ' ') {
        return false;
    }
    std::uint32_t code = 0;
    for (std::size_t i = 9; i < 12u; ++i) {
        if (line[i] < '0' || line[i] > '9') {
            return false;
        }
        code = code * 10u + static_cast<std::uint32_t>(line[i] - '0');
    }
    *status = code;
    if (code != 101u) {
        return false;
    }
    bool upgrade = false;
    bool connection = false;
    bool accepted = false;
    std::string_view rest = first == std::string_view::npos ? std::string_view() : head.substr(first + 2u);
    while (!rest.empty()) {
        const std::size_t end = rest.find("\r\n");
        const std::string_view header = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 2u);
        const std::size_t colon = header.find(':');
        if (colon == std::string_view::npos) {
            return false;
        }
        const std::string_view name = trimmed(header.substr(0, colon));
        const std::string_view value = trimmed(header.substr(colon + 1u));
        if (same_text(name, "upgrade")) {
            upgrade = same_text(value, "websocket");
        } else if (same_text(name, "connection")) {
            std::string_view tokens = value;
            while (!tokens.empty()) {
                const std::size_t comma = tokens.find(',');
                connection |= same_text(trimmed(tokens.substr(0, comma)), "upgrade");
                tokens = comma == std::string_view::npos ? std::string_view() : tokens.substr(comma + 1u);
            }
        } else if (same_text(name, "sec-websocket-accept")) {
            accepted = value == ws.accept;
        } else if (same_text(name, "sec-websocket-extensions")) {
            return false;  // none was asked
        } else if (same_text(name, "sec-websocket-protocol")) {
            if (std::find(ws.protocols.begin(), ws.protocols.end(), value) == ws.protocols.end()) {
                return false;  // not one asked (or none was)
            }
            ws.chosen.assign(value);
        }
    }
    return upgrade && connection && accepted;
}

void call_data(const LumaMod* mod, const char* service, LumaEventFn fn, void* user, std::uint32_t kind,
               LumaHandle handle, std::int32_t status, std::uint64_t value, std::string_view data,
               std::uint32_t flags) {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel == nullptr) {
        return;
    }
    LumaEvent event{};
    event.size = sizeof(event);
    event.kind = kind;
    event.status = status;
    event.flags = flags;
    event.handle = handle;
    event.data = data.empty() ? nullptr : data.data();
    event.data_size = data.size();
    event.value = value;
    kernel->call(mod, service, fn, user, &event);
}

// A WebSocket's delivery: the stream's bytes taken whole into Ws::in and parsed here; the upgrade's answer
// (CONNECTED), frames (MESSAGE; pings answered; a close echoed: CLOSED), the stream's end (CLOSED 1006), SENT.
void deliver_ws(Socket* socket, const LumaMod* mod) {
    Ws& ws = *socket->ws;
    const char* const service = socket->internet ? LUMA_NET_INTERNET_NAME : LUMA_NET_LOCAL_NAME;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    LumaHandle handle = 0;
    std::int32_t failed = LUMA_OK;
    std::uint32_t report_code = 0;
    std::string report_a;
    std::string report_b;
    bool sent_given = false;
    std::uint64_t sent = 0;
    bool closed = false;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        socket->posted = false;
        if (socket->closing || socket->finished || socket->owner == nullptr) {
            reap_locked();
            return;
        }
        g_delivering = socket;
        fn = socket->fn;
        user = socket->user;
        handle = socket->handle;
        socket->connected = false;  // the stream's: a WebSocket's CONNECTED is the upgrade's
        failed = std::exchange(socket->failed, LUMA_OK);
        if (failed != LUMA_OK) {
            report_code = std::exchange(socket->report_code, 0u);
            report_a = std::move(socket->report_a);
            report_b = std::move(socket->report_b);
        }
        try {
            ws.in += socket->received;
        } catch (...) {
            failed = LUMA_E_MEMORY;
        }
        socket->received.clear();
        if (std::exchange(socket->sent_event, false)) {
            sent_given = true;
            sent = std::exchange(socket->sent_since, 0u);
        }
        closed = std::exchange(socket->closed_event, false);
    }
    const auto open = [socket] {
        const std::lock_guard<std::mutex> lock(g_lock);
        return !socket->closing;  // the mod may close it inside a call
    };
    // The end, when it comes: an ERROR (before CONNECTED) or a CLOSED, then the handle closed.
    bool end = false;
    bool end_error = false;
    std::int32_t end_status = LUMA_OK;
    std::uint64_t end_value = 0;
    std::string end_reason;
    std::uint32_t close_code = 0;  // a close of ours to queue (0: none)
    if (failed != LUMA_OK) {
        end = true;
        end_error = true;
        end_status = failed;
    }
    if (!end && ws.stage == Ws::Stage::Head) {
        const std::size_t at = ws.in.find("\r\n\r\n");
        if (at == std::string::npos) {
            if (ws.in.size() > kHeadMost) {
                end = true;
                end_error = true;
                end_status = LUMA_E_FORMAT;
            }
        } else {
            std::uint32_t http_status = 0;
            if (!ws_head_ok(ws, std::string_view(ws.in).substr(0, at), &http_status)) {
                end = true;
                end_error = true;
                end_status = LUMA_E_FORMAT;
                end_value = http_status;
            } else {
                ws.in.erase(0, at + 4u);
                ws.stage = Ws::Stage::Open;
                call_data(mod, service, fn, user, LUMA_NET_EVENT_CONNECTED, handle, LUMA_OK, 0u, ws.chosen, 0u);
            }
        }
    }
    // Whole frames only (a frame's bytes wait in `in`; the message limit bounds them).
    while (!end && ws.stage == Ws::Stage::Open && open()) {
        if (ws.in.size() < 2u) {
            break;
        }
        const auto b0 = static_cast<std::uint8_t>(ws.in[0]);
        const auto b1 = static_cast<std::uint8_t>(ws.in[1]);
        const bool fin = (b0 & 0x80u) != 0u;
        const std::uint8_t op = b0 & 0x0Fu;
        std::uint64_t length = b1 & 0x7Fu;
        std::size_t head = 2u;
        if (length == 126u) {
            if (ws.in.size() < 4u) {
                break;
            }
            length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(ws.in[2])) << 8u) |
                     static_cast<std::uint8_t>(ws.in[3]);
            head = 4u;
        } else if (length == 127u) {
            if (ws.in.size() < 10u) {
                break;
            }
            length = 0;
            for (std::size_t i = 2; i < 10u; ++i) {
                length = (length << 8u) | static_cast<std::uint8_t>(ws.in[i]);
            }
            head = 10u;
        }
        std::uint32_t bad = 0;
        if ((b0 & 0x70u) != 0u || (b1 & 0x80u) != 0u) {
            bad = 1002u;  // an extension's bits, or a masked frame from a server
        } else if (op >= 8u) {
            if (!fin || length > 125u || (op != 8u && op != 9u && op != 10u)) {
                bad = 1002u;
            }
        } else if (op > 2u || (op == 0u && ws.message_op == 0u) || (op != 0u && ws.message_op != 0u)) {
            bad = 1002u;
        } else if (length > LUMA_NET_MESSAGE_MOST || ws.message.size() + length > LUMA_NET_MESSAGE_MOST) {
            bad = 1009u;
        }
        if (bad == 0u && ws.in.size() - head < length) {
            break;  // the frame's rest has not come
        }
        if (bad != 0u) {
            end = true;
            end_status = bad == 1009u ? LUMA_E_TOO_LARGE : LUMA_E_FORMAT;
            end_value = bad;
            close_code = bad;
            break;
        }
        const std::string_view payload(ws.in.data() + head, static_cast<std::size_t>(length));
        bool message_done = false;
        if (op <= 2u) {
            try {
                if (op != 0u) {
                    ws.message_op = op;
                }
                ws.message.append(payload);
            } catch (...) {
                end = true;
                end_status = LUMA_E_TOO_LARGE;
                end_value = 1009u;
                close_code = 1009u;
                break;
            }
            message_done = fin;
        } else if (op == 9u) {
            const std::lock_guard<std::mutex> lock(g_lock);
            try {
                socket->outgoing += ws_frame(10u, payload);  // a pong with the ping's data (RFC 6455 5.5.3)
            } catch (...) {
            }
        } else if (op == 8u) {
            if (length == 1u) {
                end = true;
                end_status = LUMA_E_FORMAT;
                end_value = 1002u;
                close_code = 1002u;
                break;
            }
            end = true;
            end_status = LUMA_OK;
            end_value = length >= 2u ? ((static_cast<std::uint32_t>(static_cast<std::uint8_t>(payload[0])) << 8u) |
                                        static_cast<std::uint8_t>(payload[1]))
                                     : 1005u;
            end_reason.assign(length > 2u ? payload.substr(2) : std::string_view());
            close_code = length >= 2u ? static_cast<std::uint32_t>(end_value) : 0u;  // echoed (5.5.1)
            if (close_code == 0u) {
                const std::lock_guard<std::mutex> lock(g_lock);
                ws_close_frame_locked(socket, 0u, {});
            }
        }
        ws.in.erase(0, head + static_cast<std::size_t>(length));
        if (message_done) {
            const std::uint32_t kind = ws.message_op == 1u ? LUMA_NET_WS_TEXT : LUMA_NET_WS_BINARY;
            if (kind == LUMA_NET_WS_TEXT && !utf8(ws.message)) {
                end = true;
                end_status = LUMA_E_FORMAT;
                end_value = 1007u;
                close_code = 1007u;
                break;
            }
            std::string message;
            message.swap(ws.message);
            ws.message_op = 0;
            call_data(mod, service, fn, user, LUMA_NET_EVENT_MESSAGE, handle, LUMA_OK, message.size(), message, kind);
        }
    }
    if (!end && closed) {
        end = true;
        end_error = ws.stage == Ws::Stage::Head;
        end_status = LUMA_E_IO;
        end_value = end_error ? 0u : 1006u;
    }
    if (!end && sent_given && sent > 0u && ws.stage == Ws::Stage::Open && open()) {
        call(mod, service, fn, user, LUMA_NET_EVENT_SENT, handle, LUMA_OK, sent);
    }
    if (end && open()) {
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            if (close_code != 0u) {
                ws_close_frame_locked(socket, close_code, {});
            }
            ws.stage = Ws::Stage::Ended;
            uncount_locked(socket);
            socket->finished = true;  // its last event (header): the handle is closed after it
            socket->closing = true;   // the close queued is still sent (I/O thread), then the connection ends
            socket->started = std::chrono::steady_clock::now();
        }
        wake_io();
        if (end_error) {
            if (report_code != 0u && socket->where != nullptr) {
                report_with(mod, socket->where, report_code, report_a, report_b);  // why, before the ERROR
            }
            call(mod, service, fn, user, LUMA_NET_EVENT_ERROR, handle, end_status, end_value);
        } else {
            call_data(mod, service, fn, user, LUMA_NET_EVENT_CLOSED, handle, end_status, end_value, end_reason, 0u);
        }
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    if (!socket->closing && !socket->finished && (!socket->received.empty() || socket->closed_event)) {
        post_locked(socket);
    }
    g_delivering = nullptr;
    reap_locked();
    wake_io();  // a pong queued: the I/O thread sends it
}

// The protocols asked, as tokens separated by ","; false: not that form (or past LUMA_NET_PROTOCOL_MOST).
bool ws_protocols(const char* given, std::vector<std::string>* out) {
    const std::string_view text(given, ::strnlen(given, LUMA_NET_PROTOCOL_MOST + 1u));
    if (text.empty() || text.size() > LUMA_NET_PROTOCOL_MOST) {
        return false;
    }
    std::string_view rest = text;
    while (true) {
        const std::size_t comma = rest.find(',');
        const std::string_view one = trimmed(rest.substr(0, comma));
        if (one.empty() || !token(one)) {
            return false;
        }
        out->emplace_back(one);
        if (comma == std::string_view::npos) {
            return true;
        }
        rest = rest.substr(comma + 1u);
    }
}

std::int32_t internet_connect(const LumaMod* mod, const char* where, bool udp, const char* host, std::uint16_t port,
                              LumaEventFn fn, void* user, LumaHandle* out, Extra* extra) noexcept;

std::int32_t ws_connect_on(const LumaMod* mod, const char* where, bool internet, const char* url,
                           const char* protocol, LumaEventFn fn, void* user, LumaHandle* out) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (url == nullptr) {
        return argument(mod, where, "url");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        const std::string_view given(url, ::strnlen(url, LUMA_NET_URL_MOST + 1u));
        std::string as_http;
        if (given.size() > 5u && same_text(given.substr(0, 5), "ws://")) {
            as_http = "http://";
            as_http.append(given.substr(5));
        } else if (given.size() > 6u && same_text(given.substr(0, 6), "wss://")) {
            as_http = "https://";
            as_http.append(given.substr(6));
        } else {
            return argument(mod, where, "url");
        }
        Target target;
        if (given.size() > LUMA_NET_URL_MOST || !parse_url(as_http, &target)) {
            return argument(mod, where, "url");
        }
        auto ws = std::make_unique<Ws>();
        if (protocol != nullptr && !ws_protocols(protocol, &ws->protocols)) {
            return argument(mod, where, "protocol");
        }
        ModState* state = state_of(mod);
        if (state == nullptr) {
            return LUMA_E_CANCELLED;
        }
        if (internet) {
            if (host_text(target.host.c_str()).empty()) {
                return argument(mod, where, "url");  // luma.json's hosts: internet_connect checks them (reported)
            }
        } else {
            sockaddr_storage address{};
            socklen_t length = 0;
            if (!local_address(target.host.c_str(), target.port, &address, &length)) {
                return argument(mod, where, "url");  // not this machine's or the local network's
            }
        }
        unsigned char nonce[16] = {};
        RAND_bytes(nonce, sizeof(nonce));
        const std::string key = base64(nonce, sizeof(nonce));
        std::string keyed = key;
        keyed.append(kWsGuid);
        unsigned char digest[SHA_DIGEST_LENGTH] = {};
        SHA1(reinterpret_cast<const std::uint8_t*>(keyed.data()), keyed.size(), digest);
        ws->accept = base64(digest, sizeof(digest));
        const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
        const char* const id = kernel != nullptr ? kernel->mod_id(mod) : nullptr;
        std::string& r = ws->request;
        r = "GET ";
        r += target.path;
        r += " HTTP/1.1\r\nHost: ";
        const bool v6 = target.host.find(':') != std::string::npos;
        r += v6 ? "[" + target.host + "]" : target.host;
        if (target.port != (target.https ? 443u : 80u)) {
            r += ':';
            r += std::to_string(target.port);
        }
        r += "\r\nUser-Agent: Nebula/" NEBULA_VERSION " ";
        r += id != nullptr ? id : "";
        r += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ";
        r += key;
        r += "\r\nSec-WebSocket-Version: 13\r\n";
        if (!ws->protocols.empty()) {
            r += "Sec-WebSocket-Protocol: ";
            for (std::size_t i = 0; i < ws->protocols.size(); ++i) {
                r += i == 0u ? "" : ", ";
                r += ws->protocols[i];
            }
            r += "\r\n";
        }
        r += "\r\n";
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            if (const std::int32_t status = room_locked(mod, where, state, r.size()); status != LUMA_OK) {
                return status;
            }
        }
        Extra extra;
        extra.tls = target.https;
        extra.ws = std::move(ws);
        return internet ? internet_connect(mod, where, false, target.host.c_str(), target.port, fn, user, out, &extra)
                        : local_connect(mod, where, false, target.host.c_str(), target.port, fn, user, out, &extra);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t ws_send_on(const LumaMod* mod, const char* where, bool internet, LumaHandle handle, const void* data,
                        std::uint64_t bytes, std::uint32_t binary) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (data == nullptr && bytes != 0u) {
        return argument(mod, where, "data");
    }
    if (binary > 1u) {
        return argument(mod, where, "binary");
    }
    const ModState* state = state_of(mod);
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        Socket* socket = state != nullptr ? find_locked(state, handle, internet) : nullptr;
        if (socket == nullptr || socket->ws == nullptr) {
            return bad_handle(mod, where);
        }
        if (socket->ws->stage != Ws::Stage::Open || socket->peer_closed || socket->system_closed) {
            return LUMA_E_CLOSED;  // an answer (header)
        }
        if (bytes > LUMA_NET_MESSAGE_MOST) {
            return LUMA_E_TOO_LARGE;  // an answer (header)
        }
        const std::string_view payload(static_cast<const char*>(data), static_cast<std::size_t>(bytes));
        if (binary == 0u && !utf8(payload)) {
            return argument(mod, where, "data");  // text must be UTF-8 (RFC 6455 5.6)
        }
        if (const std::int32_t status = room_locked(mod, where, state, bytes + 14u); status != LUMA_OK) {
            return status;
        }
        try {
            socket->outgoing += ws_frame(binary != 0u ? 2u : 1u, payload);
        } catch (...) {
            return LUMA_E_MEMORY;
        }
    }
    wake_io();
    return LUMA_OK;
}

std::int32_t net_ws_connect(const LumaMod* mod, const char* url, const char* protocol, LumaEventFn fn, void* user,
                            LumaHandle* out) noexcept {
    return ws_connect_on(mod, "net.local.ws_connect", false, url, protocol, fn, user, out);
}
std::int32_t net_ws_send(const LumaMod* mod, LumaHandle handle, const void* data, std::uint64_t bytes,
                         std::uint32_t binary) noexcept {
    return ws_send_on(mod, "net.local.ws_send", false, handle, data, bytes, binary);
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
        if (socket->ws != nullptr && socket->ws->stage == Ws::Stage::Open) {
            ws_close_frame_locked(socket, 1000u, {});  // a close first (RFC 6455 7.1.2): then the connection ends
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
    wake_io();
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
        wake_io();
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

// ---- Endpoints on this device (minor 2) ----------------------------------------------------------------------------

// An endpoint's name: 1 to `most` bytes of letters, digits, '.', '_', '-'.
bool endpoint_ok(const char* name, std::size_t most) noexcept {
    if (name == nullptr) {
        return false;
    }
    const std::size_t length = ::strnlen(name, most + 1u);
    if (length == 0u || length > most) {
        return false;
    }
    for (std::size_t i = 0; i < length; i++) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-')) {
            return false;
        }
    }
    return true;
}

#if defined(_WIN32)
std::wstring pipe_path(const std::string& name) {
    std::wstring path = L"\\\\.\\pipe\\";
    for (const char c : name) {
        path += static_cast<wchar_t>(c);  // ASCII only (endpoint_ok)
    }
    return path;
}

// A pipe socket of the mod, counted at join: no system socket, its Pipe made. LUMA_OK or the status to return.
std::int32_t make_pipe(const LumaMod* mod, const char* where, ModState* state, LumaEventFn fn, void* user,
                       Socket** out) {
    if (!io_started()) {
        return LUMA_E_IO;
    }
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        if (state->open >= LUMA_NET_SOCKETS_MOST) {
            report(mod, where, 104u, "16");  // Service.Limit
            return LUMA_E_LIMIT;
        }
    }
    std::unique_ptr<Pipe> pipe = new_pipe();
    Socket* socket = pipe != nullptr ? new (std::nothrow) Socket() : nullptr;
    if (socket == nullptr) {
        return LUMA_E_MEMORY;
    }
    socket->pipe = std::move(pipe);
    socket->node.event.mod = mod;
    socket->node.event.deliver = deliver;
    socket->node.event.drop = drop;
    socket->node.socket = socket;
    socket->owner = state;
    socket->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
    socket->fn = fn;
    socket->user = user;
    socket->ipc = true;
    *out = socket;
    return LUMA_OK;
}
#else
// An endpoint's Unix socket address: Linux and Android's abstract name (no file; gone with its last socket), elsewhere
// a socket file in the temporary folder (*path). False: too long for the system's address.
bool unix_address(const std::string& name, sockaddr_un* out, socklen_t* length, std::string* path) {
    std::memset(out, 0, sizeof(*out));
    out->sun_family = AF_UNIX;
    path->clear();
#if defined(__linux__)
    if (name.size() + 1u > sizeof(out->sun_path)) {
        return false;
    }
    std::memcpy(out->sun_path + 1, name.data(), name.size());  // sun_path[0] 0: the abstract namespace
    *length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1u + name.size());
#else
    const char* folder = std::getenv("TMPDIR");
    std::string file = folder != nullptr && *folder != '\0' ? folder : "/tmp";
    if (file.back() != '/') {
        file += '/';
    }
    file += name;
    file += ".sock";
    if (file.size() + 1u > sizeof(out->sun_path)) {
        return false;
    }
    std::memcpy(out->sun_path, file.c_str(), file.size() + 1u);
    *length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + file.size() + 1u);
    *path = std::move(file);
#endif
    return true;
}
#endif

std::int32_t net_ipc_connect(const LumaMod* mod, const char* name, LumaEventFn fn, void* user,
                             LumaHandle* out) noexcept {
    static constexpr const char* where = "net.local.ipc_connect";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (!endpoint_ok(name, LUMA_NET_ENDPOINT_MOST)) {
        return argument(mod, where, "name");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        ModState* state = state_of(mod);
        if (state == nullptr) {
            return LUMA_E_CANCELLED;
        }
        Socket* socket = nullptr;
#if defined(_WIN32)
        if (const std::int32_t status = make_pipe(mod, where, state, fn, user, &socket); status != LUMA_OK) {
            return status;
        }
        socket->pipe->path = pipe_path(name);
        socket->kind = Kind::Connecting;  // the I/O thread opens it (and waits on a busy pipe, 10 s)
        socket->started = std::chrono::steady_clock::now();
#else
        sockaddr_un address{};
        socklen_t length = 0;
        std::string path;
        if (!unix_address(name, &address, &length, &path)) {
            return LUMA_E_IO;  // the temporary folder's path is too long for a socket's address
        }
        if (const std::int32_t status = make(mod, where, state, AF_UNIX, fn, user, &socket); status != LUMA_OK) {
            return status;
        }
        socket->ipc = true;
        socket->kind = Kind::Connecting;
        socket->started = std::chrono::steady_clock::now();
        if (::connect(socket->sock, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
            const int error = system_error();
            if (!connecting(error)) {
                socket->failed = connect_failure(socket, error);  // an ERROR at the next frame
            }
        }
#endif
        const std::int32_t status = join(socket, out);
        return status != LUMA_OK ? status : LUMA_PENDING;
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t net_ipc_listen(const LumaMod* mod, const char* name, LumaEventFn fn, void* user,
                            LumaHandle* out) noexcept {
    static constexpr const char* where = "net.local.ipc_listen";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (!endpoint_ok(name, LUMA_NET_ENDPOINT_MOST)) {
        return argument(mod, where, "name");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
        const char* id = kernel != nullptr ? kernel->mod_id(mod) : nullptr;
        const std::string endpoint = std::string("nebula.") + (id != nullptr ? id : "") + "." + name;
        if (id == nullptr || !endpoint_ok(endpoint.c_str(), 100u)) {
            return argument(mod, where, "name");  // with the mod's id: too long, or a letter no endpoint takes
        }
        ModState* state = state_of(mod);
        if (state == nullptr) {
            return LUMA_E_CANCELLED;
        }
        Socket* socket = nullptr;
#if defined(_WIN32)
        if (const std::int32_t status = make_pipe(mod, where, state, fn, user, &socket); status != LUMA_OK) {
            return status;
        }
        socket->pipe->path = pipe_path(endpoint);
        socket->pipe->handle = pipe_instance(socket->pipe->path, true);
        if (socket->pipe->handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            delete socket;
            // The first instance is refused when any other process (or this game) holds the name.
            return error == ERROR_ACCESS_DENIED || error == ERROR_PIPE_BUSY ? LUMA_E_EXISTS : LUMA_E_IO;
        }
#else
        sockaddr_un address{};
        socklen_t length = 0;
        std::string path;
        if (!unix_address(endpoint, &address, &length, &path)) {
            return LUMA_E_IO;
        }
        if (!path.empty()) {
            // A socket file somebody answers on is taken (EXISTS); one nobody answers on is a crashed run's: removed.
            const Sock probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (probe != kNoSock) {
                const bool answered = ::connect(probe, reinterpret_cast<const sockaddr*>(&address), length) == 0;
                close_system(probe);
                if (answered) {
                    return LUMA_E_EXISTS;
                }
                ::unlink(path.c_str());
            }
        }
        if (const std::int32_t status = make(mod, where, state, AF_UNIX, fn, user, &socket); status != LUMA_OK) {
            return status;
        }
        if (::bind(socket->sock, reinterpret_cast<const sockaddr*>(&address), length) != 0 ||
            ::listen(socket->sock, 16) != 0) {
            const int error = system_error();
            close_system(socket->sock);
            delete socket;
            return address_in_use(error) ? LUMA_E_EXISTS : LUMA_E_IO;
        }
        socket->unix_path = std::move(path);
        socket->ipc = true;
#endif
        socket->kind = Kind::Listener;
        return join(socket, out);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
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
    wake_io();
}

// tcp_connect and udp_connect of net.internet: the host's name looked up, then a stream or a datagram socket tied to
// its first public address that takes the connect.
std::int32_t internet_connect(const LumaMod* mod, const char* where, bool udp, const char* host, std::uint16_t port,
                              LumaEventFn fn, void* user, LumaHandle* out, Extra* extra = nullptr) noexcept {
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
        if (!attach(socket, where, extra, name.c_str(), state)) {
            delete socket;
            return LUMA_E_MEMORY;
        }
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
        wake_io();
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
std::int32_t internet_tls_connect(const LumaMod* mod, const char* host, std::uint16_t port, LumaEventFn fn, void* user,
                                  LumaHandle* out) noexcept {
    Extra extra;
    extra.tls = true;
    return internet_connect(mod, "net.internet.tls_connect", false, host, port, fn, user, out, &extra);
}
std::int32_t internet_tls_listen(const LumaMod* mod, std::uint16_t, std::uint32_t, const char*, const char*,
                                 LumaEventFn, void*, LumaHandle*) noexcept {
    if (const std::int32_t status = enter(mod, "net.internet.tls_listen"); status != LUMA_OK) {
        return status;
    }
    return LUMA_E_DENIED;  // a server is net.local's (an answer)
}

// ---- Requests (minor 3) --------------------------------------------------------------------------------------------

constexpr std::string_view kMethods[] = {"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
// A request of 1.0 or of a later minor (a larger struct whose first fields are these): only a smaller one is refused.
constexpr std::uint32_t kHttpRequestLeast = static_cast<std::uint32_t>(sizeof(LumaNetHttpRequest));  // 1.0's whole struct
static_assert(sizeof(void*) != 8 || sizeof(LumaNetHttpRequest) == 56u, "LumaNetHttpRequest 1.0 is 56 bytes (luma_net.h)");
constexpr std::string_view kServiceHeaders[] = {"host", "content-length", "transfer-encoding", "connection",
                                                "accept-encoding"};

// http of both services: the request checked and copied, then a connect (net.internet's: luma.json's hosts) that
// sends it.
std::int32_t http_on(const LumaMod* mod, const char* where, bool internet, const LumaNetHttpRequest* request,
                     LumaEventFn fn, void* user, LumaHandle* out) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (request == nullptr || request->size < kHttpRequestLeast) {
        return argument(mod, where, "request");
    }
    if (request->url == nullptr) {
        return argument(mod, where, "url");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    if (request->timeout_ms > LUMA_NET_TIMEOUT_MOST_MS) {
        return argument(mod, where, "timeout_ms");
    }
    if ((request->flags & ~(LUMA_NET_HTTP_NO_REDIRECT | LUMA_NET_HTTP_RAW)) != 0u) {
        return argument(mod, where, "flags");
    }
    if ((request->headers == nullptr && request->header_count != 0u) ||
        request->header_count > LUMA_NET_REQUEST_HEADERS_MOST) {
        return argument(mod, where, "headers");
    }
    if (request->body == nullptr && request->body_bytes != 0u) {
        return argument(mod, where, "body");
    }
    try {
        auto h = std::make_unique<Http>();
        h->method = "GET";
        if (request->method != nullptr) {
            const std::string_view method(request->method, ::strnlen(request->method, 8u));
            if (std::find(std::begin(kMethods), std::end(kMethods), method) == std::end(kMethods)) {
                return argument(mod, where, "method");
            }
            h->method.assign(method);
        }
        Target target;
        if (!parse_url(std::string_view(request->url, ::strnlen(request->url, LUMA_NET_URL_MOST + 1u)), &target)) {
            return argument(mod, where, "url");
        }
        ModState* state = state_of(mod);
        if (state == nullptr) {
            return LUMA_E_CANCELLED;
        }
        if (internet) {
            if (host_text(target.host.c_str()).empty()) {
                return argument(mod, where, "url");  // luma.json's hosts: internet_connect checks them (reported)
            }
        } else {
            sockaddr_storage address{};
            socklen_t length = 0;
            if (!local_address(target.host.c_str(), target.port, &address, &length)) {
                return argument(mod, where, "url");  // not this machine's or the local network's
            }
        }
        for (std::uint32_t i = 0; i < request->header_count; i++) {
            const LumaNetHeader& header = request->headers[i];
            if (header.name == nullptr || header.value == nullptr) {
                return argument(mod, where, "headers");
            }
            const std::string_view name(header.name, ::strnlen(header.name, 65u));
            const std::string_view value(header.value, ::strnlen(header.value, 4097u));
            if (name.size() > 64u || !token(name) || value.size() > 4096u ||
                std::any_of(std::begin(kServiceHeaders), std::end(kServiceHeaders),
                            [name](std::string_view own) { return same_text(name, own); })) {
                return argument(mod, where, "headers");
            }
            for (const char c : value) {
                const auto byte = static_cast<unsigned char>(c);
                if ((byte < 0x20u && c != '\t') || byte == 0x7Fu) {
                    return argument(mod, where, "headers");  // no line ends, no control bytes
                }
            }
            if (same_text(name, "user-agent")) {
                h->user_agent = true;
            }
            h->lines.append(name);
            h->lines += ": ";
            h->lines.append(value);
            h->lines += "\r\n";
        }
        if (request->body_bytes > kRequestBodyMost) {
            report(mod, where, 104u, "4194304");  // Service.Limit: the send queue
            return LUMA_E_LIMIT;
        }
        const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
        const char* const id = kernel != nullptr ? kernel->mod_id(mod) : nullptr;
        h->body.assign(static_cast<const char*>(request->body), static_cast<std::size_t>(request->body_bytes));
        h->https = target.https;
        h->host = target.host;
        h->port = target.port;
        h->target = std::move(target.path);
        h->flags = request->flags;
        h->mod_id = id != nullptr ? id : "";
        if (internet) {
            h->hosts = state->hosts;
        }
        h->roots = state->roots;
        const std::uint32_t ms = request->timeout_ms != 0u ? request->timeout_ms : LUMA_NET_TIMEOUT_DEFAULT_MS;
        h->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        const std::size_t text_bytes = request_text(*h).size();
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            std::uint32_t requests = 0;
            for (const Socket* socket : g_sockets) {
                if (socket->owner == state && socket->http != nullptr && !socket->closing && !socket->finished) {
                    requests++;
                }
            }
            if (requests >= LUMA_NET_REQUESTS_MOST) {
                report(mod, where, 104u, "8");  // Service.Limit
                return LUMA_E_LIMIT;
            }
            if (const std::int32_t status = room_locked(mod, where, state, text_bytes); status != LUMA_OK) {
                return status;
            }
        }
        Extra extra;
        extra.tls = target.https;
        extra.http = std::move(h);
        return internet ? internet_connect(mod, where, false, target.host.c_str(), target.port, fn, user, out, &extra)
                        : local_connect(mod, where, false, target.host.c_str(), target.port, fn, user, out, &extra);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t net_http(const LumaMod* mod, const LumaNetHttpRequest* request, LumaEventFn fn, void* user,
                      LumaHandle* out) noexcept {
    return http_on(mod, "net.local.http", false, request, fn, user, out);
}
std::int32_t internet_http(const LumaMod* mod, const LumaNetHttpRequest* request, LumaEventFn fn, void* user,
                           LumaHandle* out) noexcept {
    return http_on(mod, "net.internet.http", true, request, fn, user, out);
}
std::int32_t internet_ws_connect(const LumaMod* mod, const char* url, const char* protocol, LumaEventFn fn, void* user,
                                 LumaHandle* out) noexcept {
    return ws_connect_on(mod, "net.internet.ws_connect", true, url, protocol, fn, user, out);
}
std::int32_t internet_ws_send(const LumaMod* mod, LumaHandle handle, const void* data, std::uint64_t bytes,
                              std::uint32_t binary) noexcept {
    return ws_send_on(mod, "net.internet.ws_send", true, handle, data, bytes, binary);
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
std::int32_t internet_ipc_connect(const LumaMod* mod, const char*, LumaEventFn, void*, LumaHandle*) noexcept {
    if (const std::int32_t status = enter(mod, "net.internet.ipc_connect"); status != LUMA_OK) {
        return status;
    }
    return LUMA_E_DENIED;  // endpoints on this device are net.local's (an answer)
}
std::int32_t internet_ipc_listen(const LumaMod* mod, const char*, LumaEventFn, void*, LumaHandle*) noexcept {
    if (const std::int32_t status = enter(mod, "net.internet.ipc_listen"); status != LUMA_OK) {
        return status;
    }
    return LUMA_E_DENIED;
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

// From the manifest the session started with, for the mod `id`: its folder, luma.json's net.internet hosts (lower
// case, no final dot), and the roots its net permissions add for its TLS (their PEM files read now; a file the
// validator would refuse is left out, logged).
void read_manifest(ModState* state, const char* id) {
    if (id == nullptr) {
        return;
    }
    for (const galaxy::mods::ModInfo& info : galaxy::mods::session_mods().started_with) {
        if (info.id != id || !info.luma) {
            continue;
        }
        state->package = info.path;
        std::shared_ptr<Roots> roots;
        for (const galaxy::luma::Permission& permission : info.luma->permissions) {
            if (permission.name != "net.internet" && permission.name != "net.local") {
                continue;
            }
            if (permission.name == "net.internet") {
                for (std::string host : permission.hosts) {
                    for (char& c : host) {
                        if (c >= 'A' && c <= 'Z') {
                            c = static_cast<char>(c - 'A' + 'a');
                        }
                    }
                    if (!host.empty() && host.back() == '.') {
                        host.pop_back();
                    }
                    state->hosts.push_back(std::move(host));
                }
            }
            for (const std::string& file : permission.roots) {
                std::string text;
                std::size_t found = 0;
                if (read_own_file(state->package, file, &text) == LUMA_OK) {
                    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(text.data(), static_cast<ossl_ssize_t>(text.size())));
                    while (bio != nullptr) {
                        bssl::UniquePtr<X509> root(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
                        if (root == nullptr) {
                            break;
                        }
                        if (roots == nullptr) {
                            roots = std::make_shared<Roots>();
                        }
                        roots->certificates.push_back(std::move(root));
                        found++;
                    }
                    ERR_clear_error();
                }
                if (found == 0u && g_log != nullptr) {
                    const std::string line = "LumaCore net: " + info.id + ": the root file " + file +
                                             " is missing or holds no PEM certificate; its TLS does not trust it";
                    g_log(line.c_str());
                }
            }
        }
        state->roots = std::move(roots);
        return;
    }
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
        read_manifest(state.get(), kernel != nullptr ? kernel->mod_id(mod) : nullptr);
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
        wake_io();
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

// The local-network address a UDP socket connected toward `target` takes (connect on a datagram socket only picks the
// route and the source address; nothing is sent). True with the address in text when it is a private or link-local
// IPv4 address other than loopback.
bool routed_address(const char* target, char (&text)[INET_ADDRSTRLEN]) noexcept {
    const Sock s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNoSock) {
        return false;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(9u);
    bool found = false;
    if (inet_pton(AF_INET, target, &to.sin_addr) == 1 &&
        ::connect(s, reinterpret_cast<const sockaddr*>(&to), static_cast<socklen_t>(sizeof(to))) == 0) {
        sockaddr_in self{};
        socklen_t length = static_cast<socklen_t>(sizeof(self));
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&self), &length) == 0 && self.sin_family == AF_INET &&
            (ntohl(self.sin_addr.s_addr) >> 24u) != 127u && local_v4(self.sin_addr) &&
            inet_ntop(AF_INET, &self.sin_addr, text, INET_ADDRSTRLEN) != nullptr) {
            found = true;
        }
    }
    close_system(s);
    return found;
}

std::int64_t net_address(const LumaMod* mod, char* buf, std::uint64_t size) noexcept {
    static constexpr const char* where = "net.local.address";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (buf == nullptr && size != 0u) {
        return argument(mod, where, "buf");
    }
    if (!system_ready()) {
        return LUMA_E_NOT_FOUND;
    }
    // The default route's interface first (the usual home network), then a route into each private range, for a
    // network with no way out.
    static constexpr const char* kTargets[] = {"8.8.8.8", "192.168.0.1", "192.168.1.1", "10.0.0.1", "172.16.0.1",
                                               "169.254.0.1"};
    char text[INET_ADDRSTRLEN] = {};
    for (const char* target : kTargets) {
        if (routed_address(target, text)) {
            const std::uint64_t length = std::strlen(text);
            if (buf == nullptr) {
                return static_cast<std::int64_t>(length);
            }
            if (size < length + 1u) {
                return LUMA_E_TOO_LARGE;
            }
            std::memcpy(buf, text, length + 1u);
            return static_cast<std::int64_t>(length);
        }
    }
    return LUMA_E_NOT_FOUND;
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
    net_ipc_connect,
    net_ipc_listen,
    net_tls_connect,
    net_tls_listen,
    net_http,
    net_ws_connect,
    net_ws_send,
    net_address,
};

// net.local's own rows (the net range is 250-299), beside the registry's (100-104).
const LumaServiceRow kLocalRows[] = {
    {251u, "Net.CertificateRefused", {"where", "host", "reason", nullptr}},
    {252u, "Net.RedirectRefused", {"where", "url", nullptr, nullptr}},
    {253u, "Net.FileRefused", {"where", "path", nullptr, nullptr}},
    {254u, "Net.PeerRefused", {"where", nullptr, nullptr, nullptr}},
};
const LumaServiceProvider kLocalProvider = {
    sizeof(LumaServiceProvider), LUMA_NET_LOCAL_NAME, &kLocal.base, nullptr, kLocalRows,
    sizeof(kLocalRows) / sizeof(kLocalRows[0]), nullptr, mod_begin, mod_gone, mod_end, registered, frame_start,
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
    internet_ipc_connect,
    internet_ipc_listen,
    internet_tls_connect,
    internet_tls_listen,
    internet_http,
    internet_ws_connect,
    internet_ws_send,
};

// net.internet's own rows (the net range is 250-299).
const LumaServiceRow kInternetRows[] = {
    {250u, "Net.HostNotNamed", {"where", "host", nullptr, nullptr}},
    {251u, "Net.CertificateRefused", {"where", "host", "reason", nullptr}},
    {252u, "Net.RedirectRefused", {"where", "url", nullptr, nullptr}},
};
const LumaServiceProvider kInternetProvider = {
    sizeof(LumaServiceProvider), LUMA_NET_INTERNET_NAME, &kInternet.base, nullptr, kInternetRows,
    sizeof(kInternetRows) / sizeof(kInternetRows[0]), nullptr, mod_begin, mod_gone, mod_end, registered,
    nullptr,  // net.local's frame_start settles the sockets of both
};
const LumaServiceProvider* const kProviders[] = {&kLocalProvider, &kInternetProvider};

}  // namespace

std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept {
    g_log = log;
    return kProviders;
}

}  // namespace galaxy::luma_net
