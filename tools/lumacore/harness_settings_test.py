"""harness_settings_test.py: a test's "settings", without a game.

  1. a name the run's settings file takes (tools/verify/settings_args.py) is read; so is a test hook (TEST_HOOKS);
     any other GALAXY_ name is a harness error naming it, where it was once dropped without a word;
  2. TEST_HOOKS is every GALAXY_TEST_ name the test extension reads from the environment (test_extension.cpp), no
     more and no fewer.
Exit 0 only when every check holds.
"""
import json
import pathlib
import re
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import luma_test  # noqa: E402

EXTENSION = HERE.parent / "test_driver" / "extension" / "test_extension.cpp"


def main():
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    body = {"harness": 1, "name": "t", "about": "settings", "proves": ["services.net.local"], "steps": [{"at": 1, "end": True}],
            "expect": [{"fact": "f", "is": True}]}
    with tempfile.TemporaryDirectory() as folder:
        folder = pathlib.Path(folder)

        def read(settings):
            (folder / "test.json").write_text(json.dumps(dict(body, settings=settings)), encoding="utf-8")
            return luma_test.read_test(folder)

        try:
            t = read(["GALAXY_MAX_FPS=30", "GALAXY_TEST_MODS_SCREEN=1"])
            check(t["settings"] == ["GALAXY_MAX_FPS=30", "GALAXY_TEST_MODS_SCREEN=1"], "a setting and a hook are read")
        except luma_test.HarnessError as error:
            check(False, "a setting and a hook refused: %s" % error)
        for bad in ("GALAXY_MAX_FSP=30", "GALAXY_TEST_NOT_A_HOOK=1", "GALAXY_TEST_GPU_LOST_FRAME=300"):
            try:
                read([bad])
                check(False, "%s was taken" % bad)
            except luma_test.HarnessError as error:
                check(bad.partition("=")[0] in str(error), "the error names %s: %s" % (bad, error))

    read_names = set(re.findall(r'env(?:_u64|_flag)?\("(GALAXY_TEST_[A-Z0-9_]+)"', EXTENSION.read_text(encoding="utf-8")))
    check(read_names == set(luma_test.TEST_HOOKS),
          "TEST_HOOKS is the extension's list: missing %s, extra %s" % (sorted(read_names - set(luma_test.TEST_HOOKS)),
                                                                        sorted(set(luma_test.TEST_HOOKS) - read_names)))

    for f in failed:
        print("FAIL: %s" % f)
    print("harness_settings_test: %s" % ("pass" if not failed else "%d failed" % len(failed)))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
