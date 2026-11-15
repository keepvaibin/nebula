---
title: State machines
shortTitle: State machines
intro: States and nerves in LumaScript.
contentType: reference
---

## About state machines

A state machine runs one state at a time and moves between states, the way the game's objects use nerves.

```lumascript
states Phases for GoldBall {               // one machine per GoldBall object, stored with its fields
    state Fly {
        enter { MR::startAction(self, "Fly") }
        update { if MR::isBindedWall(self) { go Burst } }
    }
    state Burst {
        enter { effect "Burst"; sound "Pop" }
        wait 30 frames                        // a state's body may wait like a task
        kill
    }
    start Fly
}
```

A machine is the same generated code as a task, with `go` jumping to a state's start and `enter`/`update`/`exit`
blocks run as their names say. On a derived class it runs from the class's `control` by default. LumaKit's `actor`
uses the same machines; the game's own nerves are reached as functions and hooks, not through `states`.

## Further reading

* [LumaScript reference](index.md)
