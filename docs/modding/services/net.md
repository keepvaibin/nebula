# Service: network

## More rules

### Services-net (written with its code, R1)

Code: Header `native/include/luma_net.h` (this page says what it decides and why). Provider
`runtime/src/luma_net.cpp`, host call `runtime/include/galaxy/luma_net_service.h`.

Layer 1 is `net.local`; layer 2 is `net.internet` (section 4); UDP is minor 1 of both
(section 5); endpoints on this device are minor 2 (section 6, branch
`h/2.7-ipc`). Then TLS + HTTP and WebSocket; none changes 1.0.

### 1. What is decided here, in one list

1. One service, `net.local` 1.0, permission `net.local` (consent asks the player). Catalog row after `fs.exchange`;
   harness capability `services.net.local`. The validator already knows the name.
2. Reach: TCP to this machine and the local network only, by address in text: `localhost`, 127.0.0.0/8, ::1, 10/8,
   172.16/12, 192.168/16, 169.254/16, fc00::/7, fe80::/10. No name is looked up (a name, or any other address:
   LUMA_E_ARGUMENT, reported). The internet is layer 2's, behind its own permission.
3. Listen: on this machine (`local_network` 0, 127.0.0.1) or the local network too (1, every IPv4 interface; a v6
   listener is a later minor, connect already reaches v6 local addresses). Port 0: the system picks; `port()` gives
   it. A port in use: LUMA_E_EXISTS, an answer. On Windows SO_EXCLUSIVEADDRUSE (no other program can take the port
   beside the mod); elsewhere SO_REUSEADDR (a restart can listen again at once).
4. No call blocks (registry 6): connect, listen and close return at once; what waits comes as an event at a frame's
   start, through the kernel's call (the mod's guard, its log name). Events: CONNECTED, ACCEPTED, DATA, SENT, CLOSED,
   ERROR (header). A socket's events come in the order they happened.
5. Reading: one DATA event per frame a socket has bytes; `take` only inside it (else LUMA_E_HANDLE, reported). Bytes
   not taken stay for the next frame. At most 1 MiB waits a socket (`LUMA_NET_RECEIVED_MOST`); past it the socket is
   not read (TCP slows the sender) until the mod takes.
6. Sending: `send` copies; bytes go in order; SENT once all queued bytes are out (value: bytes since the last). At
   most 4 MiB queued over all the mod's sockets (LUMA_E_LIMIT, nothing queued, reported). Sending before CONNECTED or
   after the other side closed: LUMA_E_CLOSED, an answer.
7. Ending: the other side's close is CLOSED (status 0) after the bytes it sent came as DATA; a broken connection is
   CLOSED with LUMA_E_IO. A connect that fails is ERROR (UNREACHABLE, TIMEOUT after 10 s, IO). The handle is closed
   after either; no event follows.
8. close(): no event after it returns; queued bytes are still sent for up to 10 s, then the connection ends (a peer
   that never reads cannot hold a descriptor). Sockets a listener accepted that the mod never heard of close with the
   listener. Closing twice: LUMA_E_HANDLE, reported.
9. Limits: 16 sockets and listeners a mod (17th: LUMA_E_LIMIT, Service.Limit row, reported once a call).
10. A mod gone (its stop): every socket of it ends at once without the rest of its bytes; nothing is called for it
    again.
11. No thread runs while no mod uses net. TCP_NODELAY on every socket (a game's messages are small). SIGPIPE never
    fires (MSG_NOSIGNAL / SO_NOSIGPIPE).
12. Rows: net.local reports only the registry's (100-104: wrong argument, wrong handle, limit). No new player text.

### 2. Why

- Local first: a LAN co-op or a tool on the same machine (LiveSplit, a level editor, a ghost viewer) needs nothing
  else, and it is the part every platform has the same way. Names and the internet bring TLS, proxies and a
  bigger consent, so they are a layer of their own.
- Events at a frame's start keep a mod's code on the game thread: no locks in mods, no call that can stall a frame.
- Fixed caps (16, 4 MiB, 1 MiB) keep one mod from taking the machine's descriptors or memory; each is a number a
  mod can plan for.

### 3. Tests

| id | what it proves |
|---|---|
| 2701-tcp-echo | listen on a port the system picks, connect over loopback, ping and pong:ping both ways, SENT on each side, the client's close is the server's CLOSED (status 0), no ERROR |
| 2702-tcp-mistakes | a far host, a name, port 0, null fn, local_network 2: ARGUMENT; 16 listeners then LIMIT; send on a listener, take outside DATA, close twice: HANDLE; a port nobody listens on: ERROR UNREACHABLE; the three log lines |
| 2703-internet-rules | net.internet without a public host: a host luma.json does not name and a "*." entry's parent: DENIED with Net.HostNotNamed; wrong letter, port 0, null host: ARGUMENT; one count of 16 for both services; a net.local handle given to net.internet's send, close, port: HANDLE; net.internet's tcp_listen: DENIED; LOCALHOST and 127.0.0.1 (named): ERROR DENIED; nothing.invalid and a.named.invalid. (named through "*.", final dot): ERROR NOT_FOUND; no other event |
| 2704-udp | UDP 1.1 over loopback: udp_open and udp_connect sockets, "ping" and an empty datagram one way, "pong:ping" back by send_to to the sender, sender address and port, SENT on each; send on udp_open, send_to on udp_connect, take on UDP, take_datagram outside a call: HANDLE; a far or v6 host: ARGUMENT; 65508 bytes: TOO_LARGE; net.internet udp_open and send_to: DENIED, its udp_connect to a loopback name: ERROR DENIED |
| 2705-ipc | 1.2: ipc_listen on the mod's endpoint and ipc_connect to it, ping and pong:ping both ways, the client's close is the server's CLOSED 0; the same endpoint again: EXISTS; port on it: HANDLE; a wrong or 65-byte name: ARGUMENT; net.internet's ipc calls: DENIED; an endpoint nobody listens on: ERROR NOT_FOUND |

State: WRITTEN (Windows MSVC /Zs clean; the POSIX branch is checked with the NDK's clang for aarch64-linux-android,
-Wall -Wextra -Wshadow, clean). PROVEN waits on a run on luma-int. A connect to a public host succeeding is not in
the suite: a test run reaches no public host; that path is net.local's connect (the same Connecting and Open code)
reached through the lookup's addresses.

### 4. Layer 2: net.internet

1. One service, `net.internet` 1.0, permission `net.internet` with `hosts` (consent asks the player and shows the
   hosts; their digest is in the consent record, consent 1.2). Catalog row after `net.local`; capability
   `services.net.internet`. Its table has net.local's layout (services-registry 2 item 4; LumaScript resolves both
   alike): `tcp_connect(host, port)`, `tcp_listen` (always LUMA_E_DENIED: no server on the open internet), `port` (no listener,
   so LUMA_E_HANDLE), `send`, `take`, `close`;
   events, caps, send and read rules are section 1's (items 4-8).
2. Hosts: luma.json's `hosts` of the manifest the session started with, lower case (A-Z), final dot dropped. A host
   matches an entry that is the same text, `*.<name>` for any name under <name> (not <name> itself), or `*` (any host;
   consent shows Permission.net.internet.Any). A host not named: LUMA_E_DENIED at once, reported with row 250
   Net.HostNotNamed `{mod}: {where} was given the host {host}, which luma.json does not name for net.internet; it did
   nothing.` (14 languages). A host that is neither a name (letters, digits, `-`, `.`, labels 1-63 bytes) nor an
   address in text, or over 253 bytes: LUMA_E_ARGUMENT, reported.
3. The connect's 10 s include it; a lookup still running at 10 s is ERROR TIMEOUT (its thread finishes and lets go;
   the socket is freed after). An address in text goes through the same call (no network asked).
4. Public only: every address found is kept only if no host on this machine or the local network has it: net.local's
   ranges (section 1 item 2) and 0/8, 100.64/10, 224/4, 240/4, ::, ::1, ff00::/8, v4-mapped addresses by their v4
   rule, the old v4-compatible ::a.b.c.d. None left: ERROR DENIED (a name pointing at the player's network is not a
   way around net.local's consent); no address at all: ERROR NOT_FOUND.
5. Several addresses: tried in the resolver's order, the next one when a connect fails, all inside the same 10 s; the
   last one's failure is the ERROR (UNREACHABLE, IO, TIMEOUT).
6. One limit for both: a mod's 16 sockets count net.local's and net.internet's together; the 4 MiB queue too.
7. Handles belong to their service: net.local's calls refuse net.internet's handles and back (LUMA_E_HANDLE,
   reported); the kernel's call names the service that made the socket.
8. A mod gone during a lookup: the socket is closed at once; the lookup's thread finishes into the closed socket and
   nothing is called.
9. Not here: TLS (layer 4's HTTPS), proxies (the system's TCP is used as it is), UDP (layer 3).

### 5. UDP: minor 1 of net.local and net.internet

1. Same services, minor 1. UDP is a kind of socket, not a reach: net.local and net.internet each get four calls at the
   end of their table (one layout, registry 2 item 4) and the event DATAGRAM (7). No new permission, no new row.
   Capability `services.net.udp`.
2. Two kinds of UDP socket. `udp_open(port, local_network)` (net.local only): bound on this machine (0) or the local
   network too (1), IPv4, port 0 picks one (`port` gives it); anyone there may send to it; it sends with
   `send_to(host, port)` to this machine's or the local network's IPv4 addresses. `udp_connect(host, port)` (both
   services, the hosts of their tcp_connect): tied to one peer, CONNECTED (or ERROR) first, sends with `send`, hears
   only from that peer (the system filters). net.internet's `udp_open` and `send_to` answer LUMA_E_DENIED: a socket
   anyone on the internet may send to is a server. net.internet's udp_connect takes the first public address the
   lookup gives (UDP cannot tell which one answers).
3. Datagrams whole. One `send` or `send_to` is one datagram, at most LUMA_NET_DATAGRAM_MOST (65507) bytes (more:
   LUMA_E_TOO_LARGE, an answer); 0 bytes is a datagram. `take_datagram(buf, size, from)` inside a DATAGRAM call takes
   the oldest: up to `size` bytes (the rest of a longer one is lost), the sender's address in text and port, the whole
   length returned; none left: LUMA_E_NOT_FOUND. DATAGRAM's value: the datagrams waiting; ones not taken stay for a
   later frame. `take` on a UDP socket and `send` on a udp_open one: LUMA_E_HANDLE, reported.
4. Caps. The 16 sockets and the 4 MiB send queue are shared with TCP. UDP).
5. What UDP never tells. A datagram lost, refused ("port unreachable") or too large for the path leaves the queue as
   sent; SENT fires when the queue is empty (value: the bytes handed to the system, 0 for empty datagrams). Neither
   side's socket closes because of the other: no CLOSED event on UDP; `close` sends what is queued (10 s at most) and
   ends.
6. Threads.

### 6. Endpoints on this device: minor 2 of net.local

1. Why not a service of its own. A pipe reaches what a loopback TCP connect reaches; a second consent for it would ask
   the player the same question twice. So `ipc_connect` and `ipc_listen` end net.local's table (1.2), and
   net.internet's same slots answer LUMA_E_DENIED. Capability `services.net.ipc`.
2. Names. An endpoint is 1 to 64 bytes of letters, digits, `.`, `_`, `-`. Windows: the named pipe `\\.\pipe\<name>`
   (LiveSplit's is `LiveSplit`); Linux and Android: the abstract Unix socket name `<name>` (no file); Apple: the
   socket file `<TMPDIR>/<name>.sock` (the app's own folder on iOS). `ipc_connect` reaches any endpoint (other
   programs', other mods'); `ipc_listen` only the mod's own, `nebula.<mod id>.<name>`, so no mod takes a name another
   program expects.
3. The same stream as TCP. CONNECTED, ACCEPTED, DATA, SENT, CLOSED, ERROR, `send`, `take`, `close` and the caps of
   section 1 apply unchanged; `port` on an endpoint listener: LUMA_E_HANDLE. A connect's ERROR: NOT_FOUND (nothing
   listens), TIMEOUT (a pipe whose every instance stays busy 10 s), DENIED (the system refused), IO. Listening where
   something listens already: LUMA_E_EXISTS (Windows: the first instance is refused; Linux: the abstract name is
   bound; Apple: a socket file somebody answers on; one nobody answers on is a crashed run's and is removed).
4. Windows pipes are not sockets. A listener keeps one instance waiting (FILE_FLAG_FIRST_PIPE_INSTANCE at listen, so a
   name another process holds is EXISTS; PIPE_REJECT_REMOTE_CLIENTS); each client's instance becomes a socket and a
   new instance waits. A connect opens with SECURITY_IDENTIFICATION: the program on the other end may learn who
   connects, never act as the game. Closing cancels what runs and waits for it before the handle closes: no overlapped
   call ever writes into freed memory.
5. Elsewhere endpoints are AF_UNIX stream sockets, polled with the rest; a listener's socket file (Apple) is removed
   when it closes.
6. Checked: Windows (MSVC, the host's flags) and Android (NDK clang, -Wall -Wextra -Wshadow) compile clean; the Apple
   branch is not compiled here (no Apple SDK on this machine): Apple builds owe it.

### 7. TLS and HTTP: minor 3 of net.local and net.internet

Director 11-12: BoringSSL vendored and pinned; NASM when present, else OPENSSL_NO_ASM (logged at configure);
Mozilla's roots ship with the game; a mod's own roots only through its manifest, shown on the Mods page; the same
library serves the loader's mod signatures (part 5).

1. The library. `third_party/boringssl` (0.20261020.0, archive sha256 in NEBULA-PIN.txt), built by
   `third_party/boringssl.cmake` as nebula_bssl_crypto and nebula_bssl_ssl on every platform (Windows: NASM if found,
   else C only; elsewhere the .S files). Roots: `third_party/mozilla-roots/cacert.pem` built into the program as
   net_roots.inc. gzip: zlib's inflate alone with Z_PREFIX (`third_party/zlib_inflate.cmake`), so it never meets the
   importer's full zlib. Trials: MSVC (C only), clang's GNU driver on Windows, NDK arm64 with the .S files: each
   builds the libraries and loads the 118 roots.
2. tls_connect (both services; net.internet by its tcp_connect rules): TLS 1.2 or 1.3, SNI and the name check for a
   name, the address check for an address. CONNECTED after the handshake; before it `send` answers LUMA_E_CLOSED. A
   certificate refused: ERROR LUMA_E_DENIED with Net.CertificateRefused {where, host, reason} (reason: BoringSSL's
   verify text). Then the stream of section 1: DATA, SENT (once the mod's bytes are encrypted and out), CLOSED (status
   0 after the other side's close_notify, LUMA_E_IO when the connection just ended), close (close_notify after the
   queued bytes). Bytes TLS holds count in the mod's 4 MiB.
3. Roots. Mozilla's list, plus the PEM files a mod's `net.local` or `net.internet` permission names in `roots` (1 to 8 paths under `certs/`, each at most 64 KiB, PEM certificates; L211). Read
   once at the mod's begin; a file that cannot be read is logged and left out.
4. tls_listen (net.local only; net.internet answers DENIED): tcp_listen's listener; each connection is TLS with the
   mod's chain and key (PEM files in its folder, '/' paths, no empty, "." or ".." part). ACCEPTED at once; the new
   socket may send at once (the bytes go after the handshake). A failed handshake is that socket's CLOSED with
   LUMA_E_DENIED. A missing file: NOT_FOUND; not PEM or a key that is not the certificate's: FORMAT; both report
   Net.FileRefused {where, path}. A bad path: ARGUMENT (Service.Argument).
5. http (both services): an HTTP/1.1 request, one connection each (Connection: close). Its handle gives HEADERS once
   (value: the status; `take` inside it gives the header lines), DATA with the body (chunked undone; gzip undone
   unless LUMA_NET_HTTP_RAW), then DONE (value: the body's bytes). The service writes Host, Content-Length,
   Accept-Encoding: gzip, Connection: close and User-Agent "Nebula/<version> <mod id>" (the mod may name its own
   User-Agent). Redirects 301/302/303/307/308 are followed up to 5 (6th: ERROR LIMIT); 303, and 301/302 after a POST,
   become a GET without the body; each redirect's host is checked again (net.internet: luma.json's hosts; net.local:
   this machine or the local network), and https never goes to http: ERROR DENIED with Net.RedirectRefused {where,
   url}. NO_REDIRECT gives the 3xx as the answer. 1xx answers are skipped; 101 is FORMAT. A body past 64 MiB: ERROR
   TOO_LARGE (at once when Content-Length says so). The whole request has its timeout (default 30 s, at most 600 s):
   ERROR TIMEOUT. `close` cancels: no event after. 8 requests a mod at once (9th: LIMIT, reported); each also counts
   as one of its 16 sockets.
6. Threads. The game thread never waits on cryptography. A deliver's socket is never freed while its calls run
   (g_delivering), even when a call closes it and another.
7. Rows. 251 Net.CertificateRefused, 252 Net.RedirectRefused (both services), 253 Net.FileRefused (net.local); 14
   languages in NebulaText.tsv.
8. Tests. 2706-net-tls, 2707-net-http (section 3's table grows with them).

State: WRITTEN when h/2.7-tls is READY (compiles: MSVC /Zs with the host's flags, clang with the native host's,
NDK clang aarch64 -Wall -Wextra -Wshadow). PROVEN waits on its run on luma-int.

### 8. WebSocket clients: minor 4 of net.local and net.internet (layer 5)

- `ws_connect(url, protocol, fn)`: "ws://" or "wss://" (wss: section 7's TLS, the same roots), the host by each
  service's tcp_connect rules. The upgrade request (GET, Host, User-Agent: Nebula/<version> <mod id>, Upgrade:
  websocket, Connection: Upgrade, a random 16-byte Sec-WebSocket-Key, Version 13, the protocols asked) goes first on
  the stream. CONNECTED once the answer is 101 with Upgrade: websocket, Connection: Upgrade, the Accept of the key
  (base64), no extension and no protocol that was not asked; its data is the protocol
  chosen. Any other answer: ERROR LUMA_E_FORMAT, value the HTTP status (0: not HTTP); the stream ending first: ERROR
  LUMA_E_IO; the connect's own errors as tcp_connect's and tls_connect's.
- Messages: MESSAGE (kind 10), one a whole message (fragments joined), flags 1 text (UTF-8 checked, RFC 3629) or 2
  binary, data the bytes during the call. At most LUMA_NET_MESSAGE_MOST (16 MiB) a message either way.
- `ws_send(socket, data, bytes, binary)`: one frame, masked with 4 random bytes (RFC 6455 5.3); SENT when all queued
  is sent (its value counts the frames' bytes). Before CONNECTED or after a close: LUMA_E_CLOSED.
- Pings are answered with a pong of the same data; pongs are ignored. No ping is sent by the service.
- The end: the server's close is echoed (its code), then CLOSED status LUMA_OK, value its code (1005: none), data its
  reason. A frame against RFC 6455 (reserved bits, a masked frame, an unknown opcode, a control frame fragmented or
  past 125 bytes, a continuation with nothing begun): the service closes with 1002, CLOSED LUMA_E_FORMAT 1002; text
  not UTF-8: 1007, LUMA_E_FORMAT; a message past 16 MiB: 1009, LUMA_E_TOO_LARGE. The stream ending without a close:
  CLOSED LUMA_E_IO 1006. `close` by the mod sends close 1000 first; no event after.
- Frames are parsed on the game thread at delivery, whole frames only; the stream's bytes are taken from `received`
  at every delivery (so its 1 MiB cap pauses nothing).
- `send` and `take` on a WebSocket: LUMA_E_HANDLE, reported.

Tests: h/tests/net 2708 (ws-local: the mod's own WebSocket server on tcp_listen, the client's messages both ways,
ping, close codes).

### 9. This machine's address: `address`, net.local 1.4 (h/net-address; net.kit's page url)

- `address(mod, buf, size) -> int64`: this machine's IPv4 address on the local network in text, so a page server can
  say where a phone finds it. The service opens a UDP socket and `connect`s it toward 8.8.8.8, then 192.168.0.1,
  192.168.1.1, 10.0.0.1, 172.16.0.1, 169.254.0.1 (a datagram connect only chooses the route; nothing is sent) and
  takes `getsockname`'s address from the first that is private or link-local and not loopback. Game thread; nothing
  blocks.
- Returns the length without the NUL; a null buf with size 0 asks the length. None: LUMA_E_NOT_FOUND (an answer); a
  short buf: LUMA_E_TOO_LARGE (an answer, nothing written); a null buf with size: LUMA_E_ARGUMENT, reported.
- net.internet has no `address`: a public address is not this machine's to tell (a router's NAT decides it).

Tests: h/tests/net 2709 (net-address).
