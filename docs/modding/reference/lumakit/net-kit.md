---
title: kit.net.kit
shortTitle: net.kit
intro: "Reliable ordered messages and rooms over the `net` service's datagrams; a clock shared between peers; a small HTTP and WebSocket server for a page (a phone as a controller)."
contentType: reference
---

## About kit.net.kit

The `kit.net.kit` module of LumaKit gives you reliable ordered messages and rooms over the `net` service's datagrams; a clock shared between peers; a small HTTP and WebSocket server for a page (a phone as a controller).

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.net.kit
```

### How the module works

Reliable ordered messages between up to 32 players over the net.local service's datagrams (a star: the host relays), a clock shared with the host, and a small HTTP + WebSocket server for a page (a phone as a controller), all built on svc.net.local and svc.time. A link is made by `reliable(port, local_network)` and joined to a host with `connect`. What arrives comes to the callbacks given to `on_received`, `on_peer` and `on_page_message`, kept with the mod that gave them. A page's files are `PageFile { path, body }`. Wire form (all little-endian; one datagram each): [0] 0x4C [1] type [2] from id [3] to id [4..8] seq [8..12] ack [12..] payload types: 1 HELLO (to the host), 2 WELCOME (the id given, in `to`), 3 DATA (reliable, by seq), 4 ACK, 5 PING, 6 BYE, 7 CLOCK_Q (payload: token u32, sent f64), 8 CLOCK_A (token u32, the question's sent f64, the question came f64, the answer sent f64; the last two the host's) The clock's four times (NTP's): a send is stamped as it goes, a receive as the previous frame's start (datagrams reach a mod at a frame's start, so one handled now came after the previous one began: the earliest it can have). The host answers from its frame, as the question went, so both ways wait alike and frame lengths drop out. DATA's payload: [0] kind (0 the mod's bytes, 1 a peer joined, 2 a peer left) [1] origin [2] target [3..] bytes The host is id 0, players 1 to 254, 255 is "everyone". Reliability is per pair (host and each player): a DATA is sent again every 12 frames until the cumulative ack passes it, received out of order is held until the gap fills, and a pair silent for 300 frames (5 s at 60) is a peer that left. A PING goes out every 30 frames of quiet.

The module's source is `lumakit/source/net/kit.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Link`](#link) | Struct | A link: its slot, 0 when it was not made. A page likewise. Their functions are also module functions (`kit.send(l, to, data)`); the clock is the task `clock(l)`. |
| [`Page`](#page) | Struct |  |
| [`PageFile`](#pagefile) | Struct | One file the page server gives: `path` as the browser asks it ("index.html", "pad.js"; "/" is index.html). |
| [`reliable`](#reliable) | Function | A link on `port` (0: one the system picks) of this machine (local_network false) or the local network too. It hosts until `connect` joins it to a host. An empty link (slot 0) when the socket could not be made (the port in use, 16 sockets held, or the calling mod does not have the permission net.local). |
| [`connect`](#connect) | Function | Joins the link to the host at `host`:`port` (a local-network address): it stops hosting, says HELLO until the host gives it an id (`id(l)` is 255 until then) and hears the players there through `on_peer`. False on an empty or closed link, or one with players already. |
| [`id`](#id) | Function | This side's id: 0 for a host, the host's number for a player, 255 while a player waits for it. |
| [`port`](#port) | Function | The port the link's socket listens on (the one the system picked when 0 was asked). |
| [`send`](#send) | Function | Sends `data` (at most 1200 bytes) to the player `to` (0: the host), in order with everything this side sent before. False when the link is empty or closed, `to` is no player of it, the data is too long, or 256 messages to that side are still unanswered. |
| [`send_all`](#send_all) | Function | Sends `data` to every other player of the link (a player's goes through the host). |
| [`peers`](#peers) | Function | The ids this side knows are there: a host's players, or a player's host (0) once it has an id. |
| [`on_received`](#on_received) | Function | The mod's function for each message: (link, the sender's id, its bytes), in the order sent. |
| [`on_peer`](#on_peer) | Function | The mod's function for each player that joins (true) or leaves (false). |
| [`close`](#close) | Function | Ends the link: every other side is told, nothing more is sent or heard. Any number of times. |
| [`alive`](#alive) | Function |  |
| [`clock`](#clock) | Task | The host's time minus ours, in seconds: four questions 2 frames apart, the answer of the quickest. A host gives 0. The task stops (its `wait` gives nothing) when no answer came in 2 seconds. |
| [`serve_page`](#serve_page) | Function | Serves `files` on `port` of the local network (0: one the system picks) to a browser: GET of a file's path, and a WebSocket at any path whose messages go to `on_page_message`. An empty page (slot 0) when the port is in use or 16 sockets are held, or the calling mod does not have the permission net.local. |
| [`url`](#url) | Function | The address a phone on the same network opens: http://<this machine's local-network address>:<port>/. |
| [`page_port`](#page_port) | Function | The port the page is served on (the one the system picked when 0 was asked); 0 for an empty or closed page. |
| [`on_page_message`](#on_page_message) | Function | The mod's function for each whole message the page sends over its WebSocket. |
| [`page_send`](#page_send) | Function | Sends `data` to every page connected over WebSocket (binary when `binary`, else UTF-8 text). |
| [`page_alive`](#page_alive) | Function |  |
| [`close_page`](#close_page) | Function | Stops serving: the listener and every connection close. Any number of times. |

### `Link`

```lumascript
pub struct Link {
    slot: u32
    fn connect(self, host: text, port: u16) -> bool { return connect(self, host, port) }
    fn id(self) -> u8 { return id(self) }
    fn port(self) -> u16 { return port(self) }
    fn send(self, to: u8, data: bytes) -> bool { return send(self, to, data) }
    fn send_all(self, data: bytes) -> bool { return send_all(self, data) }
    fn peers(self) -> list<u8> { return peers(self) }
    fn on_received(self, f: fn(Link, u8, bytes)) { on_received(self, f) }
    fn on_peer(self, f: fn(Link, u8, bool)) { on_peer(self, f) }
    fn close(self) { close(self) }
    fn alive(self) -> bool { return alive(self) }
}
```

A link: its slot, 0 when it was not made. A page likewise. Their functions are also module functions (`kit.send(l, to, data)`); the clock is the task `clock(l)`.

### `Page`

```lumascript
pub struct Page {
    slot: u32
    fn url(self) -> text { return url(self) }
    fn port(self) -> u16 { return page_port(self) }
    fn on_message(self, f: fn(Page, bytes)) { on_page_message(self, f) }
    fn send(self, data: bytes, binary: bool = false) { page_send(self, data, binary) }
    fn close(self) { close_page(self) }
    fn alive(self) -> bool { return page_alive(self) }
}
```

### `PageFile`

```lumascript
pub struct PageFile { path: text, body: bytes }
```

One file the page server gives: `path` as the browser asks it ("index.html", "pad.js"; "/" is index.html).

### `reliable`

```lumascript
pub fn reliable(port: u16, local_network: bool) -> Link
```

A link on `port` (0: one the system picks) of this machine (local_network false) or the local network too. It hosts until `connect` joins it to a host. An empty link (slot 0) when the socket could not be made (the port in use, 16 sockets held, or the calling mod does not have the permission net.local).

### `connect`

```lumascript
pub fn connect(l: Link, host: text, port: u16) -> bool
```

Joins the link to the host at `host`:`port` (a local-network address): it stops hosting, says HELLO until the host gives it an id (`id(l)` is 255 until then) and hears the players there through `on_peer`. False on an empty or closed link, or one with players already.

### `id`

```lumascript
pub fn id(l: Link) -> u8
```

This side's id: 0 for a host, the host's number for a player, 255 while a player waits for it.

### `port`

```lumascript
pub fn port(l: Link) -> u16
```

The port the link's socket listens on (the one the system picked when 0 was asked).

### `send`

```lumascript
pub fn send(l: Link, to: u8, data: bytes) -> bool
```

Sends `data` (at most 1200 bytes) to the player `to` (0: the host), in order with everything this side sent before. False when the link is empty or closed, `to` is no player of it, the data is too long, or 256 messages to that side are still unanswered.

### `send_all`

```lumascript
pub fn send_all(l: Link, data: bytes) -> bool
```

Sends `data` to every other player of the link (a player's goes through the host).

### `peers`

```lumascript
pub fn peers(l: Link) -> list<u8>
```

The ids this side knows are there: a host's players, or a player's host (0) once it has an id.

### `on_received`

```lumascript
pub fn on_received(l: Link, f: fn(Link, u8, bytes))
```

The mod's function for each message: (link, the sender's id, its bytes), in the order sent.

### `on_peer`

```lumascript
pub fn on_peer(l: Link, f: fn(Link, u8, bool))
```

The mod's function for each player that joins (true) or leaves (false).

### `close`

```lumascript
pub fn close(l: Link)
```

Ends the link: every other side is told, nothing more is sent or heard. Any number of times.

### `alive`

```lumascript
pub fn alive(l: Link) -> bool
```

### `clock`

```lumascript
pub task clock(l: Link) -> f64
```

The host's time minus ours, in seconds: four questions 2 frames apart, the answer of the quickest. A host gives 0. The task stops (its `wait` gives nothing) when no answer came in 2 seconds.

### `serve_page`

```lumascript
pub fn serve_page(port: u16, files: list<PageFile>) -> Page
```

Serves `files` on `port` of the local network (0: one the system picks) to a browser: GET of a file's path, and a WebSocket at any path whose messages go to `on_page_message`. An empty page (slot 0) when the port is in use or 16 sockets are held, or the calling mod does not have the permission net.local.

### `url`

```lumascript
pub fn url(p: Page) -> text
```

The address a phone on the same network opens: http://<this machine's local-network address>:<port>/.

### `page_port`

```lumascript
pub fn page_port(p: Page) -> u16
```

The port the page is served on (the one the system picked when 0 was asked); 0 for an empty or closed page.

### `on_page_message`

```lumascript
pub fn on_page_message(p: Page, f: fn(Page, bytes))
```

The mod's function for each whole message the page sends over its WebSocket.

### `page_send`

```lumascript
pub fn page_send(p: Page, data: bytes, binary: bool = false)
```

Sends `data` to every page connected over WebSocket (binary when `binary`, else UTF-8 text).

### `page_alive`

```lumascript
pub fn page_alive(p: Page) -> bool
```

### `close_page`

```lumascript
pub fn close_page(p: Page)
```

Stops serving: the listener and every connection close. Any number of times.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
