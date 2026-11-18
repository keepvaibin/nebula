---
title: Tests in the mod
shortTitle: Tests in the mod
intro: Write tests for your mod's own code in LumaScript, and run them with lumalink test.
contentType: reference
---

## About tests in the mod

A `test` block checks your mod's own code. Each `expect` in it states a condition that must be true. A failing `expect` names the file, the line and the condition as you wrote it; a comparison also shows both sides.

```lumascript
fn count_after_throws(throws: int) -> int {     // at most two balls at once
    if throws > 2 { return 2 }
    return throws
}

test "two balls at most" {
    expect count_after_throws(5) == 2
}
```

`lumalink build` and `lumalink pack` leave the tests out: they add nothing to your `.luma`.

## Plain tests and game tests

A `test "NAME"` block is a plain test. It runs without the game, in a process of its own, before any game starts. It can use your mod's functions, structs and module values, your options at their defaults, and the standard modules. Your mod's `on` handlers do not run for plain tests. When it reaches the game, a service or another mod's function, it stops there: `lumalink test` names what it reached, and the tests after it do not run. Write that test as a game test.

A `test game "NAME"` block runs in the game. The game tests start after every `on start` of your mod has run, one after another in the order you wrote them. A game test can `wait`; the next one starts when it ends. When your mod stops during a game test, that test is said to have stopped.

The tests share your mod's values: a value one test changes stays changed for the tests after it.

## Running the tests

`lumalink test` runs the `test` blocks with your mod's test folders:

```shell
lumalink test moon-jump
```

LumaLink copies the mod to `under-test` in the work folder and builds the copy with its tests. The plain tests run first. The game tests run as one more test of the harness, named `lumascript.game-tests`, with the copy as its mod. They have 600 seconds together; for game tests that need longer, write a test folder with its own `limit_seconds`.

```text
Test "doubles": passed
Tests in the mod's code: 1 of 1 passed
lumascript.game-tests (d3d12): fail fact mod.moon-jump.tests has {"count": 3, "passed": 3} (last) (actual: {"kind": "game", "count": 3, "passed": 2})
Test "start ran first": passed
Test "waits two frames": passed
Test "says a failure": failed
  main.lum:29: expect double(base) == 81 does not hold: 80 == 81
Game tests in the mod's code: 2 of 3 passed
0 of 1 tests passed; the runs are in moon-jump-tests
```

A mod with plain tests and no game tests or test folders needs no game run: only the game's names, from the installed game or `--bin`.

The command exits with 0 only when every test passes.

## What a test says

| Line | Meaning |
|---|---|
| `passed` | Every `expect` held. |
| `failed` | An `expect` did not hold. Each one is listed with its place; a comparison of values that can be shown also shows both sides. |
| `a script error ended it` | A script error ended the test. The error follows, with its place. The next test still runs. |
| `it reached NAME, which needs the game` | A plain test reached the game, a service or another mod's function. |
| `the mod stopped before it ended` | The mod stopped during the game test. |
| `not run` | The run ended before the test started. |

A comparison shows its sides when each is a number, text, `true` or `false`, an enum, bytes, JSON, or a list, set, map, tuple, optional value or struct of those.

## Facts

The game tests say facts, which the harness records in the run's `facts.jsonl`:

| Fact | Value |
|---|---|
| `mod.<id>.expect` | A failed `expect`: `test`, `file`, `line`, `text`, and for a comparison `op`, `left`, `right`. |
| `mod.<id>.test` | A test's end: `name`, `result` (`pass`, `fail`, `error` or `stopped`), `failed` (the count of failed expects). |
| `mod.<id>.tests` | All game tests ended: `kind` (`game`), `count`, `passed`. |

## Further reading

* [LumaScript reference](index.md)
* [Testing a mod](../../how-tos/testing-a-mod.md)
* [Test files](../test-files.md)
