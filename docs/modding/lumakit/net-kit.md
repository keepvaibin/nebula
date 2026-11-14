# LumaKit: net.kit

Gives: reliable ordered messages and rooms over the `net` service's datagrams; a clock shared between peers; a small
HTTP and WebSocket server for a page (a phone as a controller).

```lumascript skip a listing from the part's contract, not a module that builds alone
pub struct Link { fn send(self, to: u8, data: bytes); fn send_all(self, data: bytes); fn peers(self) -> list<u8>;
                  fn close(self) }                // events: net_kit.received(link, from, data), net_kit.peer(link, id, joined)
pub fn reliable(over: net.Datagrams) -> Link   // needs: net
pub fn clock(l: Link) -> pending<f64>          // the host's time minus ours, in seconds (idea 141)
pub struct Page { fn url(self) -> text; fn close(self) }   // events: net_kit.page_message(page, data)
pub fn serve_page(port: u16, files: list<data>) -> Page   // needs: net (listen)
```

Built on: `net` (datagrams, STUN, listening), `time`. Conformance: messages in order with 20% of
datagrams dropped, a peer leaving, the clock within one frame on a local link, a page served and its messages
received. Corpus: 65, 81, 116, 138, 140, 141, 142, 147. B: F. A: G.

As built (11-12): `reliable(port: u16, local_network: bool) -> Link` and `connect(l, host,
port) -> bool` (no net.Datagrams type); module functions `send/send_all -> bool`, `peers`, `id`, `port`, `close`,
`alive` (struct functions do not cross mods yet); callbacks `on_received(l, fn(Link, u8, bytes))`, `on_peer(l, fn(Link,
u8, bool))`, `on_page_message(p, fn(Page, bytes))` in place of events; `pub task clock(l) -> f64`; `PageFile { path,
body }` for `files`; `page_send(p, data, binary)`, `page_port`, `page_alive`, `close_page`; `url` from net.local 1.4
`address`. A star of up to 32 over UDP, the page server's WebSocket on tcp_listen.
Source k/mods/lumakit/source/net/kit.lum; conformance h/mods/kit-net.kit, h/tests/kit-net.kit.
