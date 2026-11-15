---
title: Changing Nebula's text and graphics
shortTitle: Change Nebula's text
intro: Translate or reword the text Nebula adds to the game's screens, and change how those screens look.
contentType: how-tos
---

## About Nebula's text

Nebula adds a few things to the game's screens: the **Quit** button, the quit and cutscene questions, the achievement pop-up, the achievement board, the Observatory's Achievement Luma, the HOME Menu and the settings menu. Nebula builds them from the game's own layouts, fonts and textures, and their text comes from one table with every language the game was released in.

A mod can change any of this text, and a translation into a language the game never had can bring its own.

## Where each element comes from

| Element | Text IDs | Layout and art (game files) |
|---|---|---|
| **Quit** button on file select | `Quit` | `LayoutData/FileSelect.arc`, its **Start** button |
| Quit question | `QuitQuestion` | The game's question window, `LayoutData/SysInfoWindow.arc`, with its own **Yes** and **No** |
| Cutscene question | `SkipCutsceneQuestion` | The game's question window, `LayoutData/SysInfoWindow.arc` |
| Achievement pop-up | `AchievementGet`; `Achievement.ID`, or `Achievement.ID.Luigi` on a Luigi file when it exists; `AchievementsUnlocked` for three or more at once, with `{n}` as their number | `LayoutData/InformationWindow.arc`: window `InfoWinC`, text `ConfirmC` and picture-font icons `ConfirmU`. For the icons, see [Achievement icons](#achievement-icons). |
| Achievement board | `BoardTitle`, `TabAll`, `TabStory`, `TabChallenges`, `TabExtras`, `TabStats`, `FilterAll`, `FilterUnlocked`, `FilterLocked`, `BoardCount`, `BoardPercent`, `BoardEmptyUnlocked`, `BoardEmptyLocked`, `Achievement.ID`, `AchievementHint.ID` (shown once unlocked), `AchievementLocked`, `StatsFile`, `StatsAllTime`, the `Stat` IDs, `PlayTime`, `DateFormat`, `DigitGroup`, `HintTabs`, `HintMove`, `BoardBack` | `LayoutData/InformationWindow.arc` (text), `SysInfoWindowMini.arc` (windows), `BackButton.arc` (buttons), `MapGalaxyBg.arc` (the galaxy map's background, without its frame) |
| Achievement Luma in the Observatory | `LumaGreeting`, then `LumaBoard` on the next page | The game's talk balloon, `LayoutData/TalkBalloonEvent.arc` |
| HOME Menu | `HomeMenuTitle`, `HomeMenuReturn`, `HomeMenuReset`, `Quit`. Its questions: `ResetQuestion` or `QuitQuestion`, then `UnsavedLost`, answered with `HomeMenuNo` and `HomeMenuYes` (the game's own **No** and **Yes**). | The settings menu's question window and buttons |
| Settings menu | `SettingsTitle`, `SettingsApply`, `SettingsBack`, the `SettingsHint` IDs, the `Key` IDs, the `Settings` IDs (the line under the page, the questions and their answers), the `SettingsJoin` IDs, and `S.TEXT` for each page and row | `LayoutData/SysInfoWindow.arc` (the dimmer and window), `InformationWindow.arc` (text), `SysInfoWindowMini.arc` (the highlight), `BackButton.arc` (buttons), `PauseMenu.arc` (its **Settings** button) |

The IDs and every built-in translation are in `localization/NebulaText.tsv`.

### Settings menu text

The settings menu's pages and rows are written in English capitals. Each one's ID is `S.` followed by the text, with each space written as `_` and each number as `#`. Its translation names the numbers `{0}`, `{1}` and so on, in order.

```text
S.DISPLAY_MODE = Anzeigemodus
S.#_FPS = {0} FPS
S.#X_#X# = {0}× · {1}×{2}
```

A text with no entry of its own is translated in pieces:

* The words before a closing `(...)`, and the words inside it.
* Each sentence.
* The words before and after `: `.
* Each item of a list. For example, `8X (OFF WITH MSAA)` is `S.#X` and `S.OFF_WITH_MSAA`.

The pieces are joined with `SettingsJoinAside`, `SettingsJoinSentence`, `SettingsJoinLabel` and `SettingsJoinList`, for example `{0} ({1})`, or `{0}。{1}` in Japanese.

Names, such as a monitor, a graphics card, a controller or a mod, show as they are unless the table has the whole name, for example `S.Keyboard_and_mouse`.

## Languages

Each language is one of the game's language folders. The Wii releases have the first twelve. Portuguese and Traditional Chinese come from the Nintendo Switch release and have no Wii folder.

| Folder | Language |
|---|---|
| `UsEnglish` | English (United States) |
| `EuEnglish` | English (United Kingdom) |
| `UsFrench` | French (Canada) |
| `EuFrench` | French (Europe) |
| `UsSpanish` | Spanish (Latin America) |
| `EuSpanish` | Spanish (Europe) |
| `EuGerman` | German |
| `EuItalian` | Italian |
| `EuDutch` | Dutch |
| `JpJapanese` | Japanese |
| `KrKorean` | Korean |
| `CnSimpChinese` | Chinese (Simplified) |
| `UsPortuguese` | Portuguese (Brazil) |
| `TwTradChinese` | Chinese (Traditional) |

A text missing in one language falls back to the same language in the other region (for example `UsFrench` and `EuFrench`), then to American English.

## Changing the text

1. Create a UTF-8 file named `NebulaText.txt` in the played language's `MessageData` folder, next to the game's `Message.arc`. For example, `/UsEnglish/MessageData/NebulaText.txt`.
1. Add one text on each line, as `ID = TEXT`.

   ```text
   # Lines that start with # are comments.
   AchievementGet = Achievement unlocked!
   Quit = Leave the game
   SkipCutsceneQuestion = Skip this scene?
   Achievement.first_star = Your First Power Star
   ```

1. Start the game. Nebula reads the file once, when it first shows its text. The log line `game: NebulaText: ...` names the folder it used and how many texts it read.

IDs that are not in the file keep Nebula's own text.

### Text rules

* `\n` is a line break, and `\\` is a backslash.
* The questions and the pop-up keep their lines as written, so break long questions yourself.
* The board's descriptions and the Luma's talk break to fit their box, measured with the box's own font. Lines are as even as possible. Nebula never breaks before a French space that comes before `!`, `?`, `:`, `;` or `»`, or after `«`. Japanese and Chinese break between characters, but never before closing punctuation or a small kana.

### Tokens

| Token | Result |
|---|---|
| `{galaxy:INTERNAL-NAME}` | That galaxy's name from the game's own text. For example, `{galaxy:HeavensDoorGalaxy}` is Gateway Galaxy. The internal names are the folder names in `StageData`. |
| `{message:ID}` | One of the game's own messages, without its tags. `{message:Layout_MapGrandGalaxyOne}` to `{message:Layout_MapGrandGalaxySix}` are the domes, from the Terrace to the Garden. `{message:CometName_Red}` is the Speedy Comet. |
| `{player}` | The player's name as the game writes it: Mario, or Luigi on a Luigi file. |

### Using another built-in language

Add `language = FOLDER`, with any folder from the [Languages](#languages) table, to use Nebula's built-in text in that language for every ID the file does not list. For example, a German translation mod that replaces the files in `UsEnglish` needs only this line:

```text
language = EuGerman
```

## Shipping the text in a mod

Put `NebulaText.txt` in the mod's `files` folder, under the language folder:

```text
My Translation/
  files/
    UsEnglish/
      MessageData/
        NebulaText.txt
```

Import the folder, or a ZIP file of it, from the **MODS** page of the settings menu.

A mod whose text covers languages other than the one being played can stand in for it: choose it with the mod's **Text** option on the **MODS** page. Nebula's text then follows the mod, and the mod's own `NebulaText.txt`, if it has one, comes with it.

### Shipping the text in a Riivolution patch

Use `create="true"`, because the disc has no `NebulaText.txt`.

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
    <file disc="/UsEnglish/MessageData/NebulaText.txt"
          external="/MyTranslation/NebulaText.txt" create="true" />
  </patch>
</wiidisc>
```

## Translating into a new language

A translation into a language the game never had replaces the game's text in a folder the disc has, usually `UsEnglish/MessageData/Message.arc`. For letters that the game's font lacks, it also replaces `LayoutData/Font.arc`.

1. Translate the game's text in `Message.arc`, and replace `Font.arc` if you need more letters.
1. Add a `NebulaText.txt` beside `Message.arc` with every ID translated, so nothing falls back to English. The list of IDs is the first column of `localization/NebulaText.tsv`.

## Changing the graphics

Nebula's elements use the game's layouts and textures, so a texture pack that replaces those textures changes Nebula's elements too.

1. On the **GRAPHICS** page of the settings menu, set **DUMP TEXTURES** to **ON**.
1. Open the screen with the element: file select for the **Quit** button, or any stage for the pop-up. Nebula saves every texture the game shows under its texture pack name.
1. On the **GRAPHICS** page, select **OPEN** in the **TEXTURE DUMPS FOLDER** row. The pop-up's window is the information window's frame texture, and the icons are the picture font's sheets.
1. Edit the PNG file, keep its name, and put it in a mod's `textures` folder.

A mod can also replace the layout archives themselves, for example `files/LayoutData/InformationWindow.arc`. The pop-up finds its panes by name (`InfoWindowCenter`, `InfoWinC`, `ConfirmC` and `ConfirmU`), so a replacement layout must keep them.

### Achievement icons

Each achievement shows a picture from the game's own files: a texture, or a character of the picture font. A texture pack that replaces one of these textures, or the picture font's sheet, changes the icon too. When a mod's files lack the texture, the Power Star shows instead.

| Icon | Source | Achievements |
|---|---|---|
| The Terrace, Fountain, Kitchen, Bedroom, Engine Room and Garden domes | `LayoutData/MapDomeIcon.arc`, `mymapdome1.tpl` to `mymapdome6.tpl` | Welcome Aboard, Mega Takedown, Reactor Overload, Sink the Fleet, Anti-Matter? I Barely Know Her! and Too Hot to Handle, each with the dome its Grand Star opens |
| Luigi's face | `LayoutData/MiiIcon.arc`, `mymiiluigi.tpl` | Found You, Luigi! (Another... Me?), Super Luigi Galaxy and Green Means Go |
| Mario's face | `LayoutData/MiiIcon.arc`, `mymiimario.tpl` | Light-Years Ahead |
| Peach's face | `LayoutData/MiiIcon.arc`, `mymiipeach.tpl` | The Universe Reborn |
| The storybook's front cover | `ObjectData/PictureBookTexture.arc`, the right half of `PictureBookCoverFront.bti` | The Mother of Stars |
| Power Star | Picture font `7` | A Star Is Born and Halfway There |
| Crown | Picture font `P` | Super Mario Galaxy, and the pop-up for several at once |
| Rainbow star | Picture font `W` | Grand Finale |
| Shooting star | Picture font `e` | Comet Chaser, One-Hit Wonder, Beat the Clock and Wish Upon A Star |
| Purple coin | Picture font `N` | Purple Reign |
| Question mark | Picture font `Z` | Hidden Treasures |
| Green Star | Picture font `O` | Going Green |
| Silver Star | Picture font `J` | Trial Run |
| Hungry Luma | Picture font `c` | All You Can Eat and Snack Time |
| Star Bit | Picture font `;` | Star Bit Tycoon and Pocketful of Star Bits |
| 1-Up Mushroom | Picture font `a` | 1UP Hoarder |
| Mario's head | Picture font `B` | Life of the Party |
| Coin | Picture font `A` | Coin-noisseur |
| Grand Star | Picture font `K` | One-Life Wonder |
| Life Mushroom | Picture font `b` | Cosmic Immortal |
| Red Star | Picture font `S` | All Aboard Air Mario! |

## Adding a translation to Nebula itself

1. Edit `localization/NebulaText.tsv`. It is UTF-8 and tab-separated: an ID column, then one column for each language folder. An empty cell falls back as described in [Languages](#languages).
1. Use each release's own words for the game's things, such as its names for Power Stars, Lumas, comets and Rosalina. Leave galaxy names as `{galaxy:...}` tokens.
1. Generate the table that the game compiles, `native/petari/src/Game/System/NebulaTextTable.inc`:

   ```shell
   python tools/nebula_text.py
   ```

1. To check that the table matches the TSV file, run the script with `--check`. It fails when the two differ.

A test, `galaxy_native_settings_overlay_tests`, steps every row of every settings page through its values, and fails on any text the table cannot name.
