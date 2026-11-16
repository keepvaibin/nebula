---
title: Connecting to LiveSplit
shortTitle: LiveSplit link
intro: Control LiveSplit, the speedrun timer, from the game over a local connection.
contentType: tutorials
example: livesplit-link
---

## Introduction

In this tutorial, you make a mod that controls [LiveSplit](https://livesplit.org/), the speedrun timer, through its server on the same computer:

* Choosing a file on the file select screen starts the timer.
* Every Power Star and Grand Star splits.
* Game time pauses while the game loads.
* The credits take the last split.
* Returning to the title screen resets the timer.

You learn how to:

* Ask for the `net.local` permission and open a TCP connection to a program on the same computer.
* Reconnect automatically when the other program starts later or closes.
* Hook the game's own moments, such as collecting a star or starting the credits.
* Test a mod that talks to another program, with a second mod that listens in its place.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* LiveSplit, for playing the finished mod. The tests do not need it.

## Creating the mod

Create the mod folder, and add the mod's permission and option.

```sh
lumalink new livesplit-link --id livesplit-link --name "LiveSplit Link" --summary "Starts, splits and resets LiveSplit, and pauses its game time while the game loads." --author YOUR-NAME
lumalink add permission net.local livesplit-link --why "Talks to LiveSplit's server on this computer."
lumalink add option port livesplit-link --kind number --default 16834 --name "LiveSplit server port"
```

* `net.local` lets the mod open connections to this computer only. The player sees the text after `--why` when the mod asks for the permission.
* The `port` option stores LiveSplit's server port on the **Mods** page. LiveSplit uses port 16834 unless you change it. The option's range is set with `"min"`, `"max"` and `"step"` in `luma.json`.

> [!NOTE]
> In the current version, Nebula does not accept a number option with more than 10,000 values. With a range of 1 to 65535, the mod is listed with an error and does not start. Use a narrower range, such as 16000 to 17000.

To start LiveSplit's server, right-click LiveSplit and choose **Control** > **Start TCP Server**. On Windows, the mod can also connect through LiveSplit's named pipe, which does not need the server.

## Writing the code

Create `livesplit-link/source/main.lum` with this code:

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

* `connect` opens a TCP connection to `127.0.0.1` on the option's port, or on Windows to LiveSplit's named pipe, `LiveSplit`. `heard` runs for each connection event. On `Connected`, it sends the commands that were waiting. On `Error` and `Closed`, it tries the other kind of connection, then both again every 5 seconds, so LiveSplit can start before or after the game.
* `say` sends one of LiveSplit's server commands, followed by the line ending LiveSplit expects (`\r\n`).
* `on frame` compares the game's state with the previous frame. The start and end of `time.State.Loading` pause and resume game time.
* The hooks run at the game's own moments: the file loading after the file select screen, the cutscenes for collecting a Power Star and a Grand Star, the credits, and the change to the title screen.

## Building and checking the mod

```sh
lumalink build livesplit-link
lumalink check livesplit-link
```

The last line of the `check` output is `Errors: 0`, and the output lists `net.local` among the permissions the mod asks for.

## Testing the mod

A test needs a program that listens where LiveSplit would. The finished mod's `tests/run/server/` folder contains a second small mod that listens on its own `port` option and writes everything it receives as the fact `said`. `lumalink test` builds it with the test.

Each test gives both mods the same port, which the test harness picks from the free ports for that run (`{"port": {"free_port": "livesplit"}}`). Two test runs at the same time never connect to each other's listener.

```lumascript
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

* `tests/run` creates a file, enters Good Egg Galaxy, and calls the star cutscene. The test passes when the listener receives `starttimer`, `initgametime`, a pause and resume for each load, and then `split`, in that order.
* `tests/reset` does the same, then returns to the title screen. The test passes when the last command is `reset`.

To run the tests, copy the `tests` folder from the finished mod into `livesplit-link`, then run:

```sh
lumalink test livesplit-link
```

## Playing the mod

1. Pack the mod.

   ```sh
   lumalink pack livesplit-link
   ```

1. Drag the `.luma` file onto the game's window, turn the mod on in the **Mods** page, and allow the connections on this computer that it asks for.
1. Start LiveSplit's server, then choose a file in the game.

## Next steps

* [Adding an online leaderboard](online-leaderboard.md)
* [About permissions](../concepts/permissions.md)
* [Network service](../reference/services/net.md)
