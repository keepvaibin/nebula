# LumaKit: talk

Gives: lines and choices from an actor in the game's balloon, dialogue trees, voice for lines.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub fn say(who: ref<LiveActor>, line: text) -> pending
pub fn ask(who: ref<LiveActor>, line: text, choices: list<text>) -> pending<int>
pub fn run(who: ref<LiveActor>, tree: text) -> pending<text?>  // a tree of kit/talk; its end node's name
pub fn voice(line: text, s: sound)          // the sound played when the line is shown
```

Data `kit/talk/<tree>.json`: nodes of a line, choices, each choice's next node and a per-file flag it sets (through
`kit.progress`). Built on: `K.call` (`MR::createTalkCtrl`, `tryTalkNearPlayer`, `TalkMessageCtrl`), `C.text`,
`C.sound`, `kit.progress`. Conformance: say, ask with two and four choices, a tree with a loop and a flag, a voiced
line, the speaker killed mid-line.
