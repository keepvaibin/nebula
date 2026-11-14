# lumalink test
State: generated from the tool's own help.

Build the mod, run each tests/**/test.json in the game (hidden, silent) and print each check with what was found.

The mod is built for every platform its luma.json lists (for this machine's platform when it lists none).

## Smallest example

```sh
lumalink test moon-jump
```

## Reference

```text
Build the mod, run each tests/**/test.json in the game (hidden, silent) and print each check with what was found.

Usage: lumalink test [OPTIONS] <FOLDER>

Arguments:
  <FOLDER>  The mod folder.

Options:
      --bin <FOLDER>         A game build's folder with the harness's parts (the host extension and the game half). Default: LUMA_TEST_BIN.
      --runner <FILE>        The harness's runner (luma_test.py). Default: LUMA_TEST_RUNNER, then tools/lumacore/luma_test.py above --bin.
      --python <FILE>        The Python that runs it. Default: LUMALINK_PYTHON, then python.
      --work <FOLDER>        Where the runs are left. Default: <folder>-tests beside the mod.
      --lock <FILE>          The run lock. Default: LUMA_TEST_LOCK, then run.lock in the work folder.
      --renderer <RENDERER>  d3d12 or vulkan. Default: the runner's.
  -h, --help                 Print this help.
      --language <LANGUAGE>  The language LumaLink and the validator speak: one of the game's 14, by its name in the game (UsEnglish, EuGerman, JpJapanese...). Default: UsEnglish.
```

## Limits

Messages and help in any of the game's 14 languages (`--language`).

## See also

lumalink/test-harness.md
