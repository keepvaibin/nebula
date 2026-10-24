# Changing Nebula's text and graphics

Nebula adds a few things to the game's screens: the Quit to desktop button, the quit and cutscene questions, the
achievement pop-up, the achievement board, the Observatory's Achievement Luma and the settings menu. They are built from the game's own
layouts, fonts and textures, and their text comes from one table in every language the game was released in. A mod can change any of it, and a translation into a language the game
never had can bring its own.

## What is on screen and where it comes from

| Element | Text ids | Layout and art (from the game files) |
|---|---|---|
| Quit to desktop button (file select) | `QuitToDesktop` | `LayoutData/FileSelect.arc`, its Start button |
| Quit question | `QuitQuestion` | the game's question window (`LayoutData/SysInfoWindow.arc`), its own Yes and No |
| Cutscene question | `SkipCutsceneQuestion` | the same window |
| Achievement pop-up | `AchievementGet`, `Achievement.<id>` (`Achievement.<id>.Luigi` instead on a Luigi file, when there is one), `AchievementsUnlocked` (three or more at once, `{n}` their number) | `LayoutData/InformationWindow.arc` (window `InfoWinC`, text `ConfirmC`, glyph icons `ConfirmU`); icons below |
| Achievement board | `BoardTitle`, `TabAll`, `TabStory`, `TabChallenges`, `TabExtras`, `TabStats`, `FilterAll`, `FilterUnlocked`, `FilterLocked`, `BoardCount`, `BoardPercent`, `BoardEmptyUnlocked`, `BoardEmptyLocked`, `Achievement.<id>`, `AchievementHint.<id>` (once unlocked), `AchievementLocked`, `StatsFile`, `StatsAllTime`, `Stat...`, `PlayTime`, `DateFormat`, `DigitGroup`, `HintTabs`, `HintMove`, `BoardBack` | `LayoutData/InformationWindow.arc` (text), `SysInfoWindowMini.arc` (windows), `BackButton.arc` (buttons), `MapGalaxyBg.arc` (the galaxy map's backdrop, its frame hidden) |
| Achievement Luma's talk (the Observatory) | `LumaGreeting`, then `LumaBoard` on the next page | the game's talk balloon (`LayoutData/TalkBalloonEvent.arc`) |
| HOME Menu | `HomeMenuTitle`, `HomeMenuReturn`, `HomeMenuReset`, `QuitToDesktop`; asking: `ResetQuestion` or `QuitQuestion`, then `UnsavedLost`, answered `HomeMenuNo` and `HomeMenuYes` (the game's own No and Yes) | the settings menu's question window and buttons (below) |
| Settings menu (`F1`, the pause menu's Settings) | `SettingsTitle`, `SettingsApply`, `SettingsBack`, `SettingsHint...`, `Key...`, `Settings...` (the line under the page, the questions and their answers), `SettingsJoin...`, and `S.<text>` for each of the host's texts (below) | `LayoutData/SysInfoWindow.arc` (the dimmer and window), `InformationWindow.arc` (text), `SysInfoWindowMini.arc` (the highlight), `BackButton.arc` (buttons), `PauseMenu.arc` (its Settings button) |

The ids and every built-in translation are in [`localization/NebulaText.tsv`](../localization/NebulaText.tsv).

The settings menu's pages and rows come from Nebula's host, in English capitals; each has the id `S.` and that text with
its spaces as `_` and each number as `#`, and its translation names the numbers `{0}`, `{1}`... in their order:

```
S.DISPLAY_MODE = Anzeigemodus
S.#_FPS = {0} FPS
S.#X_#X# = {0}× · {1}×{2}
```

A text with no entry of its own is translated piece by piece: the words before a closing `(...)` and those inside it,
then its sentences, the words before `: ` and after it, a list's items (`8X (OFF WITH MSAA)` is `S.#X` and
`S.OFF_WITH_MSAA`), joined by `SettingsJoinAside`, `SettingsJoinSentence`, `SettingsJoinLabel` and `SettingsJoinList`
(`{0} ({1})`, `{0}。{1}` in Japanese...). Names (a monitor, a graphics card, a controller, a mod) are shown as they
are unless the table has them whole (`S.Keyboard_and_mouse`). A test (`galaxy_native_settings_overlay_tests`) steps
every row of every page through its values and fails on a text the table cannot name.

## Languages

Each language is the game's language folder. The Wii versions have the first twelve; Portuguese and Traditional
Chinese come from the Switch release and have no Wii folder.

| Folder | Language | Folder | Language |
|---|---|---|---|
| `UsEnglish` | English (US) | `EuEnglish` | English (UK) |
| `UsFrench` | French (Canada) | `EuFrench` | French (Europe) |
| `UsSpanish` | Spanish (Latin America) | `EuSpanish` | Spanish (Europe) |
| `EuGerman` | German | `EuItalian` | Italian |
| `EuDutch` | Dutch | `UsPortuguese` | Portuguese (Brazil) |
| `JpJapanese` | Japanese | `KrKorean` | Korean |
| `CnSimpChinese` | Chinese (Simplified) | `TwTradChinese` | Chinese (Traditional) |

A text missing in one language falls back to the same language of the other region (UsFrench and EuFrench, ...), then
to American English.

## Changing the text: NebulaText.txt

Put a file named `NebulaText.txt` in the played language's `MessageData` folder, next to the game's `Message.arc`:

```
/UsEnglish/MessageData/NebulaText.txt
```

UTF-8, one text per line:

```
# Lines starting with # are comments.
AchievementGet = Achievement unlocked!
QuitToDesktop = Leave the game
SkipCutsceneQuestion = Skip this scene?
Achievement.first_star = Your First Power Star
```

- `\n` is a line break, `\\` a backslash. The questions and the pop-up keep their lines as written (break long
  questions yourself). The board's descriptions and the Luma's talk are broken to fit their box, measured with the
  box's own font: lines as even as they can be; never at a French space before `!`, `?`, `:`, `;` or `»`, or after
  `«`; Japanese and Chinese between characters, never before closing punctuation or a small kana.
- `{galaxy:InternalName}` is that galaxy's name from the game's own text, for example `{galaxy:HeavensDoorGalaxy}`
  (Gateway Galaxy). The internal names are the folder names in `StageData`.
- `{message:Id}` is one of the game's own messages without its tags: `{message:Layout_MapGrandGalaxyOne}` to
  `...Six` are the domes (the Terrace to the Garden), `{message:CometName_Red}` the Speedy Comet.
- `{player}` is the player's name as the game writes it: Mario, or Luigi on a Luigi file.
- Ids not in the file keep Nebula's own text.
- `language = EuGerman` (any folder from the table) uses Nebula's built-in German for every id the file does not
  list. A German translation mod that replaces the files in `UsEnglish` needs only this line.

The game reads the file once, when Nebula's first text is shown. Its log (`game: NebulaText: ...`) names the folder it
used and how many texts it read.

### As a Nebula mod

```
mods/
  My Translation/
    files/
      UsEnglish/
        MessageData/
          NebulaText.txt
```

Import the folder (or a zip of it) from the launcher's Mods tab or Settings > Mods. A mod whose text covers other
languages than the one played stands in for it (Settings > Mods, its Text choice); Nebula's text follows it, and the
mod's own `NebulaText.txt`, if it has one, comes along.

### As a Riivolution patch

```xml
<wiidisc version="1">
  <id game="RMG" />
  <options>
    <section name="Translation">
      <option name="Nebula text">
        <choice name="On"><patch id="nebulatext" /></choice>
      </option>
    </section>
  </options>
  <patch id="nebulatext">
    <file disc="/UsEnglish/MessageData/NebulaText.txt" external="/MyTranslation/NebulaText.txt" create="true" />
  </patch>
</wiidisc>
```

`create="true"` adds the file; the disc has none.

### A language the game never had

A translation replaces the game's own text in a folder the disc has (usually `UsEnglish/MessageData/Message.arc`) and,
for letters the game's font lacks, `LayoutData/Font.arc`. Add a `NebulaText.txt` beside it with every id translated,
so nothing falls back to English. The full list of ids is the first column of `localization/NebulaText.tsv`.

## Changing the graphics

Nebula draws nothing of its own: the button, the questions and the pop-up use the game's layouts and textures, so a
texture pack that replaces those textures changes Nebula's elements too.

1. Turn on Settings > Mods > Dump textures and open the screen (the file select for the button, any stage for the
   pop-up). Every texture the game shows is saved under its texture-pack name.
2. Settings > Mods > Open texture dumps folder. The pop-up's window is the information window's frame texture; the
   icons are the picture font's sheets.
3. Edit the PNG, keep its name, and put it in a mod's `textures/` folder.

A mod can also replace the layout archives themselves (`files/LayoutData/InformationWindow.arc`, ...). The pop-up
finds its panes by name (`InfoWindowCenter`, `InfoWinC`, `ConfirmC`, `ConfirmU`); a replacement layout must keep them.

### Achievement icons

Each achievement shows a picture from the game's own files, chosen in
`native/petari/src/Game/System/NebulaAchievements.cpp`: a texture, or a glyph of the picture font.

| Icon | From | Used for |
|---|---|---|
| The Terrace, Fountain, Kitchen, Bedroom, Engine Room and Garden domes | `LayoutData/MapDomeIcon.arc`, `mymapdome1.tpl` to `mymapdome6.tpl` | Welcome Aboard, Mega Takedown, Reactor Overload, Sink the Fleet, Anti-Matter? I Barely Know Her!, Too Hot to Handle: each the dome its Grand Star opens |
| Luigi's, Mario's and Peach's faces | `LayoutData/MiiIcon.arc`, `mymiiluigi.tpl`, `mymiimario.tpl`, `mymiipeach.tpl` | Found You, Luigi! (Another... Me?), Super Luigi Galaxy, Green Means Go; Light-Years Ahead; The Universe Reborn |
| The storybook's front cover | `ObjectData/PictureBookTexture.arc`, `PictureBookCoverFront.bti` (its right half) | The Mother of Stars |
| Power Star | picture font `7` | A Star Is Born, Halfway There |
| crown | `P` | Super Mario Galaxy; the pop-up for several at once |
| rainbow star | `W` | Grand Finale |
| shooting star | `e` | Comet Chaser, One-Hit Wonder, Beat the Clock, Wish Upon A Star |
| purple coin | `N` | Purple Reign |
| question mark | `Z` | Hidden Treasures |
| Green Star | `O` | Going Green |
| Silver Star | `J` | Trial Run |
| Hungry Luma | `c` | All You Can Eat, Snack Time |
| Star Bit | `;` | Star Bit Tycoon, Pocketful of Star Bits |
| 1-Up Mushroom | `a` | 1UP Hoarder |
| Mario's head | `B` | Life of the Party |
| coin | `A` | Coin-noisseur |
| Grand Star | `K` | One-Life Wonder |
| Life Mushroom | `b` | Cosmic Immortal |
| Red Star | `S` | All Aboard Air Mario! |

A texture pack that replaces one of these textures, or the picture font's sheet, changes the icon with it. A texture a
mod's files lack shows the Power Star instead.

## Adding a translation to Nebula itself

Edit `localization/NebulaText.tsv` (UTF-8, tab-separated: an id column, then one column per language folder; an empty
cell falls back as above), then run:

```
python tools/nebula_text.py
```

It writes `native/petari/src/Game/System/NebulaTextTable.inc`, which the game compiles; `--check` fails when the two
differ. Use each version's own words for the game's things (its name for Power Stars, Lumas, comets, Rosalina) and
leave galaxy names as `{galaxy:...}` tokens.
