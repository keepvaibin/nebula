# LumaKit: screen

Gives: whole screens: a message, a choice, a list, a loading screen, a layout of the mod's as a screen.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub fn message(line: text) -> pending                  // waitable; closes with A or B
pub fn choose(title: text, rows: list<text>) -> pending<int?>  // none: the player backed out
pub struct List { title: text, rows: list<text>, on_choose: fn(int)? = none, closes: bool = false }
pub struct ListScreen { fn set_rows(self, rows: list<text>); fn close(self); fn alive(self) -> bool }
pub fn list(l: List) -> ListScreen
pub fn layout(l: layout, pauses: bool = true) -> ui.Open   // a mod's layout as a screen
pub fn loading(tips: list<text>, picture: draw.Texture? = none)   // shown while stages load
```

Built on: `ui`, `draw` (the loading point: "one on every presented frame, loading included"), `input`, `time`
(the game held while a screen pauses), `C.layout`, `C.text`. Conformance: each kind opened, answered and backed out of
with pad, keyboard and touch; a list updated while open; a screen open when its mod stops (closed, the game resumed).
