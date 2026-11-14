# A video on a screen inside a galaxy

A screen stands in front of the start of Good Egg. When Mario walks up to it, the mod's movie plays full screen with
its sound. + skips it, and the game goes on where it was.

The whole mod is in the folder `a-video-in-a-galaxy` beside this page. Its
screen model and its movie are made by a script (a test picture and a tone); put your own in their place under the
same names.

## The folder

```text
a-video-in-a-galaxy/
  luma.json
  source/main.lum
  layers/base/assets/Screen.glb
  layers/base/assets/movies/Trailer.webm         the picture: AV1 in WebM
  layers/base/assets/movies/Trailer.ogg          the sound
  layers/base/assets/movies/Trailer.movie.json
  layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json
  tests/movie/test.json
```

## 1. Start the folder

```sh
lumalink new a-video-in-a-galaxy --id galaxy-screen --name "Galaxy Screen" --summary "A screen in a galaxy that plays the mod's movie when Mario walks up to it." --author you
lumalink add need lumakit a-video-in-a-galaxy
```

```json luma.json
{
  "format": "1.0",
  "id": "galaxy-screen",
  "version": "1.0.0",
  "name": "Galaxy Screen",
  "summary": "A screen in a galaxy that plays the mod's movie when Mario walks up to it.",
  "text": {
    "UsEnglish": {"name": "Galaxy Screen", "summary": "A screen in a galaxy that plays the mod's movie when Mario walks up to it."},
    "EuEnglish": {"name": "Galaxy Screen", "summary": "A screen in a galaxy that plays the mod's movie when Mario walks up to it."},
    "UsFrench": {"name": "Écran galactique", "summary": "Un écran dans une galaxie qui passe la vidéo du mod quand Mario s'en approche."},
    "EuFrench": {"name": "Écran galactique", "summary": "Un écran dans une galaxie qui passe la vidéo du mod quand Mario s'en approche."},
    "UsSpanish": {"name": "Pantalla galáctica", "summary": "Una pantalla en una galaxia que reproduce el vídeo del mod cuando Mario se acerca."},
    "EuSpanish": {"name": "Pantalla galáctica", "summary": "Una pantalla en una galaxia que reproduce el vídeo del mod cuando Mario se acerca."},
    "EuGerman": {"name": "Galaxie-Bildschirm", "summary": "Ein Bildschirm in einer Galaxie, der das Video der Mod zeigt, wenn Mario hingeht."},
    "EuItalian": {"name": "Schermo galattico", "summary": "Uno schermo in una galassia che riproduce il video della mod quando Mario si avvicina."},
    "EuDutch": {"name": "Melkwegscherm", "summary": "Een scherm in een sterrenstelsel dat de video van de mod speelt als Mario erheen loopt."},
    "UsPortuguese": {"name": "Tela galáctica", "summary": "Uma tela numa galáxia que passa o vídeo do mod quando Mario se aproxima."},
    "JpJapanese": {"name": "ギャラクシースクリーン", "summary": "マリオが近づくと、MODのムービーを流すギャラクシーのスクリーン。"},
    "KrKorean": {"name": "갤럭시 스크린", "summary": "마리오가 다가가면 모드의 영상을 틀어 주는 갤럭시 안의 스크린."},
    "CnSimpChinese": {"name": "银河屏幕", "summary": "马力欧走近时播放模组影片的银河屏幕。"},
    "TwTradChinese": {"name": "銀河螢幕", "summary": "瑪利歐走近時播放模組影片的銀河螢幕。"}
  },
  "authors": ["you"],
  "game": { "id": "RMG" },
  "needs": [{ "id": "lumakit", "version": ">=1.0.0" }],
  "layers": [{ "name": "base" }],
  "code": { "platforms": ["windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-arm64", "macos-x64", "android-arm64", "android-x64", "ios-arm64"]}
}
```

## 2. The movie

A movie is an AV1 video in a WebM file, its sound beside it, and a `.movie.json` that names both. `skip: true` lets
the player skip it with +. To make one from any video:

```sh
ffmpeg -i trailer.mp4 -c:v libsvtav1 -an Trailer.webm
ffmpeg -i trailer.mp4 -vn -c:a libvorbis Trailer.ogg
```

```json
{ "file": "Trailer.webm", "audio": "Trailer.ogg", "skip": true }
```

## 3. Where the screen stands

```json
{
  "patch": 1,
  "key": ["l_id"],
  "add": [
    {
      "name": "galaxy-screen/LumaScreen",
      "l_id": 9413,
      "pos_x": -3439.8, "pos_y": -13081.3, "pos_z": -15575.5,
      "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0,
      "scale_x": 1.0, "scale_y": 1.0, "scale_z": 1.0,
      "Obj_arg0": -1, "Obj_arg1": -1, "Obj_arg2": -1, "Obj_arg3": -1,
      "Obj_arg4": -1, "Obj_arg5": -1, "Obj_arg6": -1, "Obj_arg7": -1
    }
  ]
}
```

## 4. The code

`source/main.lum`:

```lumascript
use kit.actor
use svc.movie

var playing: movie.Movie? = none

fn ended(e: movie.Event) {
    playing = none
    log fact "playing" false
}

fn near(screen: LiveActor) {
    if playing != none { return }
    // Full screen with its sound; + skips it.
    playing = movie.play((movie "movies/Trailer").name(), ended)
    log fact "playing" playing != none
}

on start {
    actor.define(actor.Kind {
        name: "LumaScreen"                     // placed in a stage as "galaxy-screen/LumaScreen"
        look: model "Screen"
        sensors: [actor.Sensor.Body(150.0)]
        on_touch_player: near
    })
}
```

What each part does:

- `actor.define` makes the screen. Its body sensor is 150 units across; `on_touch_player` calls `near` when Mario
  touches it.
- `near` plays the movie unless it is playing already. `movie "movies/Trailer"` is checked when the mod is built: a
  missing movie is an error then, not when a player walks up.
- `ended` is called when the movie ends or is skipped.
- `log fact` lines are what the test reads.

## 5. Check, build, test, pack

```sh
lumalink check a-video-in-a-galaxy
lumalink build a-video-in-a-galaxy
lumalink test a-video-in-a-galaxy
lumalink pack a-video-in-a-galaxy
```

`test` starts the game hidden with the mod and LumaKit, walks Mario into the screen, checks that the movie's sound is
heard, presses + and prints `pass` or what failed.

## See also

`lumakit/actor.md`, `content/models.md`, `lumalink/cmd-test.md`.
