# LiveSplit link

A mod that drives LiveSplit, the speedrun timer, through its server on the same computer: choosing a file on the
file select starts the timer, every star and grand star splits, game time pauses while the game loads, the credits
take the last split and going back to the title screen resets. The whole mod is the folder `livesplit-link/` beside
this page.

## 1. Make the folder

```sh
lumalink new livesplit-link --id livesplit-link --name "LiveSplit Link" --summary "Starts, splits and resets LiveSplit, and pauses its game time while the game loads." --author you
lumalink add permission net.local livesplit-link --why "Talks to LiveSplit's server on this computer."
lumalink add option port livesplit-link --kind number --default 16834 --name "LiveSplit server port"
```

`net.local` lets the mod open connections to this computer only; the player sees the line after `--why` when the mod
asks for it. The option keeps LiveSplit's port (16834 unless changed in LiveSplit) on the Mods screen. Its range is
1 to 65535 (`"min"`, `"max"` and `"step"` in `luma.json`).

In LiveSplit: right-click, Control, Start TCP Server. On Windows the mod also finds LiveSplit's named pipe, which
needs no server started.

## 2. Write the code

Put this in `livesplit-link/source/main.lum`:

```lumascript
use svc.net.local as local
use svc.time

option port: int

var link: local.Request? = none
var connected = false
var waiting: list<text> = []
var running = false
var loading = false
var by_pipe = false

// LiveSplit's server: TCP on the option's port, or on Windows its named pipe "LiveSplit". Each failed try goes on to
// the other one.
fn connect() {
    connected = false
    if by_pipe {
        link = local.ipc_connect("LiveSplit", heard)
    } else {
        link = local.tcp_connect("127.0.0.1", port as u16, heard)
    }
    if link == none {
        lost()
    }
}

fn heard(e: local.Event) {
    match e {
        local.Event.Connected(socket, data) => {
            connected = true
            if by_pipe {
                log "connected to LiveSplit's pipe"
            } else {
                log "connected to LiveSplit on port {port}"
            }
            let lines = waiting
            waiting = []
            for line in lines {
                say(line)
            }
        }
        local.Event.Error(socket, status, value) => lost()
        local.Event.Closed(socket, status, value, data) => lost()
        else => return
    }
}

fn lost() {
    link = none
    connected = false
    by_pipe = not by_pipe
    if by_pipe {
        connect()
        return
    }
    log "no LiveSplit server on port {port} or pipe; trying again in 5 seconds"
    start retry()
}

task retry() {
    let again = time.monotonic_ns() + 5_000_000_000
    wait until time.monotonic_ns() >= again
    if link == none {
        connect()
    }
}

// One command of LiveSplit's server, kept until the link is up.
fn say(command: text) {
    if connected {
        if let l = link {
            local.send(l, "{command}\r\n".bytes())
            return
        }
    }
    if waiting.len() < 32 {
        waiting.push(command)
    }
}

fn begin() {
    if running {
        say("reset")
    }
    say("starttimer")
    say("initgametime")
    if loading {
        say("pausegametime")
    }
    running = true
}

on start {
    connect()
}

on option port {
    if let l = link {
        local.close(l)
    }
    link = none
    by_pipe = false
    connect()
}

// Game time stops while the game loads a scene.
on frame {
    let now_loading = time.now().state == time.State.Loading
    if now_loading != loading {
        loading = now_loading
        if running {
            if loading {
                say("pausegametime")
            } else {
                say("unpausegametime")
            }
        }
    }
}

// A file chosen on the file select starts the run.
hook after GameSequenceFunction::startGameDataLoadSequence(file, first) {
    begin()
}

hook before GameSceneFunction::requestPowerStarGetDemo() {
    if running {
        say("split")
    }
}

hook before GameSceneFunction::requestGrandStarGetDemo() {
    if running {
        say("split")
    }
}

// The last split: the credits start.
hook before GameSceneFunction::requestStaffRoll() {
    if running {
        say("split")
        running = false
    }
}

// Back to the title screen: the run is over.
hook before MR::requestChangeSceneTitle() {
    if running {
        say("reset")
        running = false
    }
}
```

- `connect` opens a TCP connection to `127.0.0.1` on the option's port, or LiveSplit's named pipe `LiveSplit`
  (Windows). `heard` is called with each event: `Connected` sends the commands that waited; `Error` and `Closed`
  try the other way, then both again every 5 seconds, so LiveSplit may be started before or after the game.
- `say` sends one of LiveSplit's server commands with the line end it expects (`\r\n`).
- `on frame` compares the game's state with the frame before: `time.State.Loading` begins and ends a pause.
- The hooks are the game's own moments: the file loaded after the file select, a star's and a grand star's
  "got it" scene, the credits, and the change to the title scene.

## 3. Build and check

```sh
lumalink build livesplit-link
lumalink check livesplit-link
```

`check` ends `Errors: 0` and lists `net.local` among what the mod asks for.

## 4. Test it

A test needs something that listens where LiveSplit would. `tests/run/server/` is a second small mod that listens on
its `port` option and writes everything it is sent as the fact `said`; `lumalink test` builds it with the test. Each
test gives both mods the same port, one the harness finds free for the run (`{"port": {"free_port": "livesplit"}}`),
so two runs side by side never reach each other's listener:

```lumascript skip the test's listener, built by lumalink test as a mod of its own
use svc.net.local as local

option port: int

var listener: local.Request? = none
var said: text = ""

on start {
    listener = local.tcp_listen(port as u16, 0, fn(e: local.Event) {
        match e {
            local.Event.Accepted(socket) => log fact "accepted" true
            local.Event.Data(socket, data) => {
                said = "{said}{data.text() ?? "?"}"
                log fact "said" said
            }
            else => return
        }
    })
    log fact "listening" listener != none
}
```

- `tests/run`: the harness creates a file and enters Good Egg Galaxy, then calls the star's "got it" scene. It passes
  when the listener got `starttimer`, `initgametime`, a pause and unpause for each load, then `split`, in that order.
- `tests/reset`: the same, then back to the title screen. It passes when the last command is `reset`.

```sh
lumalink test livesplit-link
```

Not yet: the game drops a number option with more than 10,000 values, so with the port's range of 1 to 65535 the
mod is listed with an error and does not start. Both tests pass with a range of 16000 to 17000.

## 5. Play it

```sh
lumalink pack livesplit-link
```

Drop `livesplit-link.luma` on the game's window, switch it on and allow what it asks for (connections on this computer). Start
LiveSplit's server, then choose a file.

## See also

`services/net.md` (`net.local`), `services/time.md`, `language/language.md` (hooks, tasks), `format/luma.md`
(permissions, options), `lumalink/test-harness.md`.
