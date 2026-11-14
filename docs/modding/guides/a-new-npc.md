# A new character to talk to

The Keeper stands in front of the start of Good Egg. Walk up to him and the game's A prompt shows; press A and he
greets Mario and asks a question with two answers. He says one line for Yes and another for No.

The whole mod is in the folder `a-new-npc` beside this page. Its model is a plain
shape made by a script, with its own materials, a cloth texture and a `Wait` clip; put your own `.glb` in its place
under the same name.

## The folder

```text
a-new-npc/
  luma.json
  source/main.lum
  layers/base/assets/Keeper.glb                     the model: meshes, materials, textures, the Wait clip
  layers/base/merge/MessageData/messages.json       his lines in every language
  layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json
  tests/talk/test.json
```

## 1. Start the folder

```sh
lumalink new a-new-npc --id luma-keeper --name "The Keeper" --summary "A new character to talk to, with its own model, materials and lines." --author you
lumalink add need lumakit a-new-npc
```

```json luma.json
{
  "format": "1.0",
  "id": "luma-keeper",
  "version": "1.0.0",
  "name": "The Keeper",
  "summary": "A new character to talk to, with its own model, materials and lines.",
  "text": {
    "UsEnglish": {"name": "The Keeper", "summary": "A new character to talk to, with its own model, materials and lines."},
    "EuEnglish": {"name": "The Keeper", "summary": "A new character to talk to, with its own model, materials and lines."},
    "UsFrench": {"name": "Le Gardien", "summary": "Un nouveau personnage à qui parler, avec son modèle, ses matériaux et ses répliques."},
    "EuFrench": {"name": "Le Gardien", "summary": "Un nouveau personnage à qui parler, avec son modèle, ses matériaux et ses répliques."},
    "UsSpanish": {"name": "El Guardián", "summary": "Un personaje nuevo con quien hablar, con su propio modelo, materiales y frases."},
    "EuSpanish": {"name": "El Guardián", "summary": "Un personaje nuevo con quien hablar, con su propio modelo, materiales y frases."},
    "EuGerman": {"name": "Der Hüter", "summary": "Eine neue Figur zum Reden, mit eigenem Modell, eigenen Materialien und Sätzen."},
    "EuItalian": {"name": "Il Custode", "summary": "Un nuovo personaggio con cui parlare, con modello, materiali e battute propri."},
    "EuDutch": {"name": "De Hoeder", "summary": "Een nieuw personage om mee te praten, met een eigen model, materialen en zinnen."},
    "UsPortuguese": {"name": "O Guardião", "summary": "Um novo personagem para conversar, com modelo, materiais e falas próprios."},
    "JpJapanese": {"name": "キーパー", "summary": "話しかけられる新しいキャラクター。モデル、マテリアル、セリフはすべて独自です。"},
    "KrKorean": {"name": "키퍼", "summary": "말을 걸 수 있는 새 캐릭터. 모델, 재질, 대사가 모두 새것입니다."},
    "CnSimpChinese": {"name": "守护者", "summary": "可以交谈的新角色，拥有自己的模型、材质和台词。"},
    "TwTradChinese": {"name": "守護者", "summary": "可以交談的新角色，擁有自己的模型、材質和台詞。"}
  },
  "authors": ["you"],
  "game": { "id": "RMG" },
  "needs": [{ "id": "lumakit", "version": ">=1.0.0" }],
  "layers": [{ "name": "base" }],
  "code": { "platforms": ["windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-arm64", "macos-x64", "android-arm64", "android-x64", "ios-arm64"]}
}
```

## 2. The model

Copy your model to `layers/base/assets/Keeper.glb`. LumaLink cooks its meshes, materials, textures and clips into
the game's formats when it packs the mod; the code names it `model "Keeper"` and its clip `"Wait"`.

## 3. His lines

`merge/MessageData/messages.json` adds lines to the game's text. Each has a `default` and one line per language
(`fr`, `es`, `de`, `it`, `nl`, `pt`, `ja`, `ko`, `zh-Hans`, `zh-Hant`), which covers the game's 14:

```json
{
  "lines": {
    "Keeper_Hello": { "default": "Welcome, [player]. Shall I keep your star bits safe?", "fr": "Bienvenue, [player]. Je garde tes fragments d'étoile ?", "es": "Bienvenido, [player]. ¿Te guardo los trozos de estrella?", "de": "Willkommen, [player]. Soll ich deine Sternenteile verwahren?", "it": "Benvenuto, [player]. Custodisco i tuoi astrini?", "nl": "Welkom, [player]. Zal ik je sterrenstukjes bewaren?", "pt": "Bem-vindo, [player]. Guardo os teus pedaços de estrela?", "ja": "ようこそ、[player]。スターピースを預かろうか？", "ko": "어서 와, [player]. 스타칩을 맡아 줄까?", "zh-Hans": "欢迎，[player]。要我替你保管星星碎片吗？", "zh-Hant": "歡迎，[player]。要我替你保管星星碎片嗎？" },
    "Keeper_Yes": { "default": "Yes", "fr": "Oui", "es": "Sí", "de": "Ja", "it": "Sì", "nl": "Ja", "pt": "Sim", "ja": "はい", "ko": "예", "zh-Hans": "好", "zh-Hant": "好" },
    "Keeper_No": { "default": "No", "fr": "Non", "es": "No", "de": "Nein", "it": "No", "nl": "Nee", "pt": "Não", "ja": "いいえ", "ko": "아니요", "zh-Hans": "不用", "zh-Hant": "不用" },
    "Keeper_Thanks": { "default": "They are safe with me.", "fr": "Ils sont en sécurité avec moi.", "es": "Conmigo están a salvo.", "de": "Bei mir sind sie sicher.", "it": "Con me sono al sicuro.", "nl": "Bij mij zijn ze veilig.", "pt": "Comigo estão seguros.", "ja": "確かに預かったよ。", "ko": "내가 잘 맡아 둘게.", "zh-Hans": "放在我这里很安全。", "zh-Hant": "放在我這裡很安全。" },
    "Keeper_Bye": { "default": "Come back any time.", "fr": "Reviens quand tu veux.", "es": "Vuelve cuando quieras.", "de": "Komm jederzeit wieder.", "it": "Torna quando vuoi.", "nl": "Kom gerust terug.", "pt": "Volta quando quiseres.", "ja": "いつでもおいで。", "ko": "언제든 다시 와.", "zh-Hans": "随时再来。", "zh-Hant": "隨時再來。" }
  }
}
```

## 4. Where he stands

A patch adds one row to Good Egg's object table: the Keeper, under his mod's name, a few steps in front of the start.

```json
{
  "patch": 1,
  "key": ["l_id"],
  "add": [
    {
      "name": "luma-keeper/LumaKeeper",
      "l_id": 9412,
      "pos_x": -3439.8, "pos_y": -13081.3, "pos_z": -15575.5,
      "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0,
      "scale_x": 1.0, "scale_y": 1.0, "scale_z": 1.0,
      "Obj_arg0": -1, "Obj_arg1": -1, "Obj_arg2": -1, "Obj_arg3": -1,
      "Obj_arg4": -1, "Obj_arg5": -1, "Obj_arg6": -1, "Obj_arg7": -1
    }
  ]
}
```

## 5. The code

`source/main.lum`:

```lumascript
use kit.actor
use kit.talk

fn chat(keeper: LiveActor) {
    start chat_task(ref(keeper))
}

task chat_task(k: ref<LiveActor>) {
    let answer = wait talk.ask(k, text "Keeper_Hello", [text "Keeper_Yes", text "Keeper_No"])
    log fact "answer" answer
    match answer {
        0 => wait talk.say(k, text "Keeper_Thanks")
        else => wait talk.say(k, text "Keeper_Bye")
    }
    log fact "talked" true
}

on start {
    actor.define(actor.Kind {
        name: "LumaKeeper"                     // placed in a stage as "luma-keeper/LumaKeeper"
        look: model "Keeper"                   // its materials and textures are cooked from the glTF
        idle_action: "Wait"                    // the model's own clip, looped
        sensors: [actor.Sensor.Body(70.0)]
        on_talk: chat
    })
}
```

What each part does:

- `actor.define` makes the kind of object. A kind stands on the ground by default: it falls onto the ground under it
  and stays upright to the planet's gravity. `on_talk` gives him the game's A prompt; A by him calls `chat`.
- `chat` starts a task, since a talk lasts many frames. `talk.ask` shows the question and waits for the answer
  (0 for the first choice); `talk.say` shows a line and waits until the player closes it.
- `log fact` lines are what the test reads.

## 6. Check, build, test, pack

```sh
lumalink check a-new-npc
lumalink build a-new-npc
lumalink test a-new-npc
lumalink pack a-new-npc
```

`test` starts the game hidden with the mod and LumaKit, walks Mario up to the Keeper, presses A through the talk and
prints `pass` or what failed.

## See also

`lumakit/actor.md`, `lumakit/talk.md`, `content/models.md`, `language/text-and-services.md`, `lumalink/cmd-test.md`.
