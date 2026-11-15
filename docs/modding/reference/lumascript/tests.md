---
title: Tests in the mod
shortTitle: Tests in the mod
intro: Write tests for your mod's own functions in LumaScript.
contentType: reference
---

## About tests in the mod

A `test` block checks your mod's own code. Each `expect` in it states a condition that must be true. A failing `expect` names the file, the line and both sides of the comparison.

```lumascript
test "two balls at most" {
    expect count_after_throws(5) == 2
}
```

A test marked `test game "NAME" { }` runs with the game. A test without `game` is for code that does not touch the game.

To test what your mod does in the game, write test files and run them with `lumalink test`. For more information, see [Testing a mod](../../how-tos/testing-a-mod.md).

## Further reading

* [LumaScript reference](index.md)
* [Test files](../test-files.md)
