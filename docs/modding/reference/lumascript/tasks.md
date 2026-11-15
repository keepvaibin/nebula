---
title: Tasks
shortTitle: Tasks
intro: Code that waits across frames.
contentType: reference
---

## About tasks

A task is code that runs across several frames. It can wait for frames, time, a condition, another task or a service's answer.

```lumascript
task blink(h: ref<LiveActor>) {
    for i in 0..6 {
        if let a = h.get { MR::hideModel(a) } else { stop }
        wait 5 frames
        if let a = h.get { MR::showModel(a) } else { stop }
        wait 5 frames
    }
}
...
start blink(ref(self))            // runs from the next frame; returns a task handle
```

- A task runs on the game thread, at the frame's start (after `on frame`), until it reaches `wait`, `stop` or its
  end. `wait N frames`, `wait N seconds` (game time; paused when the game pauses), `wait until <condition>` (checked
  once per frame), `wait <task>` (until it ends), `wait event` for a service's event, and
  `wait <pending>`: an expression whose value is the result of a call that answers later (`let data = wait
  fs.read("best.bin")`, `let i = wait screen.choose(...)`).
- The compiler turns a task into a struct holding its locals and a step number, and a function that runs from the
  step to the next `wait`. Nothing in the game schedules it: the mod's run-time support calls each live task from the
  mod's own `on frame` hook (service `time`'s frame event). No stack is kept between frames.
- Locals that live across a `wait` may not be views; the compiler names the line.
- `stop` ends the task; `t.stop` from outside; a task whose `ref` argument empties is not stopped by itself (the
  script checks, as above).

11.1a A task with a result. `task f(params) -> T { ... }` gives a value: each `return v` is
checked against `T`, and a path that ends without one is S0101 (MISSING_RETURN), as in a function. A call `f(args)`
starts the task as `start` does and gives `pending<T>`; `start f(args)` still gives its `task`. `wait p` gives the
value when the task returns it. `p.stop` cancels: the task is stopped (its `defer`s run, its way out as any stopped
task's), and a `wait p` on a task that ended without a value (stopped, its own `stop`, a script error, or `p` waited
already) stops the waiting task too, its way out run; it never gives `T`'s zero. A library's `pub task f(...) -> T` is
exported (its interface line `pub task`): the caller gets the handle with
the library's own operations, and `wait` copies the value into the caller's memory. A `pending` dropped without `wait`
is still the warning of D4.14.

A task without a result called gives `pending<>`: `wait p` goes on once the task ended by
itself, and stops the waiting task as above when it was stopped or failed. Dropped, it is no warning (it loses
nothing); `start f(args)` still gives the `task`. A library's result-less `pub task` is exported the same way (its
`lumi1` result `pending<>`).

## Further reading

* [LumaScript reference](index.md)
