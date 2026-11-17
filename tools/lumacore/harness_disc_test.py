"""harness_disc_test.py: the proof harness's "disc" key, without a game.

  1. a game.pak's disc is the six characters at byte 48 of its header (nebula-import's store), None for another id or a
     short file;
  2. a test with no disc, or with the disc of the run's own pak, runs on that pak; a test of another disc runs on the
     pak --pak-for (or LUMA_TEST_PAKS) names for it; with none named it is not run, and the reason names the disc;
  3. a pak named for a disc that is of another disc, a malformed --pak-for and an unknown disc in a test are harness
     errors.
Exit 0 only when every check holds.
"""
import argparse
import json
import os
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import luma_test  # noqa: E402


def store(path, disc):
    head = b"NBSTORE\0" + (34).to_bytes(4, "little") + bytes(36) + disc.encode("ascii").ljust(16, b"\0")
    path.write_bytes(head + bytes(100))
    return path


def main():
    failed = []

    def check(ok, what):
        if not ok:
            failed.append(what)

    def raises(kind, fn, words):
        try:
            fn()
        except kind as error:
            return words in str(error)
        return False

    with tempfile.TemporaryDirectory() as folder:
        folder = pathlib.Path(folder)
        us, eu, jp = store(folder / "us.pak", "RMGE01"), store(folder / "eu.pak", "RMGP01"), store(folder / "jp.pak", "RMGJ01")
        odd = store(folder / "odd.pak", "RMGK01")
        (folder / "short.pak").write_bytes(b"NBSTORE\0")
        check(luma_test.pak_disc(us) == "RMGE01" and luma_test.pak_disc(eu) == "RMGP01" and luma_test.pak_disc(jp) == "RMGJ01",
              "the disc at byte 48")
        check(luma_test.pak_disc(odd) is None and luma_test.pak_disc(folder / "short.pak") is None, "no known disc: None")

        def args(*pak_for):
            return argparse.Namespace(pak=str(us), pak_for=list(pak_for))

        os.environ.pop("LUMA_TEST_PAKS", None)
        check(luma_test.pak_for({"disc": None}, args(), folder) == us, "no disc: the run's pak")
        check(luma_test.pak_for({"disc": "RMGE01"}, args(), folder) == us, "the run's own disc: the run's pak")
        check(luma_test.pak_for({"disc": "RMGP01"}, args("RMGP01=%s" % eu, "RMGJ01=%s" % jp), folder) == eu,
              "another disc: the pak named for it")
        check(raises(luma_test.NotRun, lambda: luma_test.pak_for({"disc": "RMGJ01"}, args("RMGP01=%s" % eu), folder),
                     "needs disc RMGJ01"), "none named: not run, naming the disc")
        os.environ["LUMA_TEST_PAKS"] = os.pathsep.join(("RMGP01=%s" % eu, "RMGJ01=%s" % jp))
        check(luma_test.pak_for({"disc": "RMGJ01"}, args(), folder) == jp, "LUMA_TEST_PAKS when no --pak-for")
        os.environ.pop("LUMA_TEST_PAKS")
        check(raises(luma_test.HarnessError, lambda: luma_test.pak_for({"disc": "RMGP01"}, args("RMGP01=%s" % jp), folder),
                     "a game of RMGJ01"), "a pak of another disc named for a disc: a harness error")
        for bad in ("RMGP01", "RMGX01=%s" % eu, "RMGP01="):
            check(raises(luma_test.HarnessError, lambda: luma_test.paks_for(args(bad)), "--pak-for"),
                  "malformed --pak-for %s: a harness error" % bad)

        test = folder / "t"
        test.mkdir()
        body = {"harness": 1, "name": "t", "about": "a disc", "proves": ["services.net.local"], "steps": [{"at": 1, "end": True}],
                "expect": [{"fact": "f", "is": True}]}
        (test / "test.json").write_text(json.dumps(dict(body, disc="RMGP01")), encoding="utf-8")
        try:
            check(luma_test.read_test(test)["disc"] == "RMGP01", "read_test keeps the disc")
        except luma_test.HarnessError as error:
            check(False, "read_test refused a test with a disc: %s" % error)
        (test / "test.json").write_text(json.dumps(dict(body, disc="RMGX01")), encoding="utf-8")
        check(raises(luma_test.HarnessError, lambda: luma_test.read_test(test), "disc RMGX01"), "an unknown disc: a harness error")

    for f in failed:
        print("FAIL: %s" % f)
    print("harness_disc_test: %s" % ("pass" if not failed else "%d failed" % len(failed)))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
