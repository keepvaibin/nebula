"""push_again_test.py: the push of main is tried again when it did not reach GitHub (gate.py move_main, push_is_network, PUSH_WAITS), with a
fake git (a stand-in for subprocess.run that answers the git commands move_main asks): never the real remote, no network, no waiting.

    python -X utf8 -I tools/gate/tests/push_again_test.py [--work FOLDER]

On 2026-11-21 the push of a passed landing died with `fatal: unable to access 'https://github.com/...': Recv failure: Connection was reset`
and a whole run of 12 minutes was not judged. Nobody found a cause for the reset.

  network_twice_then_passes         the push fails by the network twice, then goes: moved, waits of 15 s and 30 s, the allowance file written
                                    for each of the three tries and gone at the end, one line on stderr for each wait
  answer_lost_main_holds_it         the push fails by the network but origin's main holds the commit: a move (the board says the answer of the push
                                    was lost; main holds it), no second push, no wait
  main_moved_on                     the push fails, origin's main is another commit: "main moved during the gate", no second push
  rejected_is_not_tried_again       `! [rejected]` and `[remote rejected]` (even with a network word in the text): the text of today, one push
  http_4xx_is_not_tried_again       `unable to access ... returned error: 403`: GitHub answered and refused: one push, the text of today
  six_network_failures              six pushes, five waits (15 30 60 120 240), the text "GitHub could not be reached for the push of main (6 tries
                                    in 8 minutes): ...", and not_moved_kind gives it `env`
  push_is_network_text              the exact text of this morning, the other network texts, the refusals, a hook's text
  fetch_failing_is_asked_again      a fetch that does not work does not stop the tries: the network text is what decides
  not_moved_ends_not_judged         (gate_github_test.py) the verdict and exit code of a run for a not-moved pass of each kind
Exit 0 when every case holds.
"""
import contextlib
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import gate  # noqa: E402

BEFORE, SHA, OTHER = "b" * 40, "5" * 40, "9" * 40
# What each commit holds (its ancestors): SHA is on top of BEFORE; OTHER is another commit on top of BEFORE, which does not hold SHA.
HOLDS = {BEFORE: {BEFORE}, SHA: {BEFORE, SHA}, OTHER: {BEFORE, OTHER}}
THIS_MORNING = "fatal: unable to access 'https://github.com/keepvaibin/nebula.git/': Recv failure: Connection was reset"


class FakeGit:
    """Answers the git commands of move_main. Commits hold their ancestors (HOLDS); `main` is origin's real main, `seen` what the local
    origin/main says (it follows `main` at each fetch). `pushes`: one answer per push, in order: ("ok",) arrives and succeeds,
    ("lost", text) arrives and fails (the answer was lost), ("fail", text) does not arrive and fails, ("moved", text) does not arrive and
    fails while another commit becomes main."""

    def __init__(self, pushes, main=BEFORE, fetch_ok=True):
        self.pushes, self.main, self.seen, self.fetch_ok = list(pushes), main, main, fetch_ok
        self.calls, self.allowance = [], []

    def __call__(self, cmd, **kw):
        args = [str(a) for a in cmd[3:]]
        self.calls.append(args)
        sub = args[0]
        done = lambda code, out="", err="": subprocess.CompletedProcess(cmd, code, out, err)  # noqa: E731
        if sub == "fetch":
            if not self.fetch_ok:
                return done(128, "", "fatal: unable to access")
            self.seen = self.main
            return done(0)
        if sub == "rev-parse":
            return done(0, self.seen + "\n")
        if sub == "merge-base":
            a, b = args[2], args[3]
            return done(0 if a in HOLDS[b] else 1)
        if sub == "rev-list":
            return done(0, "1\n")
        if sub == "push":
            path = pathlib.Path(kw["env"][gate.ALLOWED_ENV])
            self.allowance.append(path.read_text(encoding="utf-8") if path.exists() else None)
            kind = self.pushes.pop(0)
            if kind[0] == "ok":
                self.main = SHA
                return done(0)
            if kind[0] == "lost":
                self.main = SHA
            if kind[0] == "moved":
                self.main = OTHER       # somebody else moved main while this push failed
            return done(1, "", kind[1])
        raise AssertionError("unexpected git command: %s" % args)

    def pushed(self):
        return len([c for c in self.calls if c[0] == "push"])


class MovePush(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix="push-again-"))
        self.addCleanup(lambda: __import__("shutil").rmtree(self.tmp, ignore_errors=True))
        self.board = self.tmp / "BOARD.md"
        self.waits = []

    def move(self, fake):
        err = io.StringIO()
        with mock.patch.object(gate.subprocess, "run", fake), contextlib.redirect_stderr(err):
            why = gate.move_main(SHA, "run-1", self.tmp, str(self.board), "Agent 1", "covers", tree=self.tmp / "tree",
                                 sleep=self.waits.append)
        return why, err.getvalue().splitlines()

    def board_text(self):
        return self.board.read_text(encoding="utf-8") if self.board.exists() else ""

    def test_network_twice_then_passes(self):
        fake = FakeGit([("fail", THIS_MORNING), ("fail", THIS_MORNING), ("ok",)])
        why, err = self.move(fake)
        self.assertIsNone(why)
        self.assertEqual((fake.pushed(), self.waits), (3, [15, 30]))
        self.assertEqual(fake.allowance, ["%s run-1\n" % SHA] * 3, "the allowance was there for each try")
        self.assertFalse((self.tmp / "main-allowed.txt").exists(), "and is gone at the end")
        self.assertEqual(len(err), 2, err)
        self.assertEqual(err[0], "gate.py: the push of main did not reach GitHub (%s); trying again in 15 s (try 2 of 6)" % THIS_MORNING)
        self.assertTrue(err[1].endswith("trying again in 30 s (try 3 of 6)"), err)
        self.assertIn("MAIN-MOVED: main %s -> %s (1 commits): covers; gate run-1." % (BEFORE[:9], SHA[:9]), self.board_text())
        self.assertNotIn("lost", self.board_text())

    def test_answer_lost_main_holds_it(self):
        fake = FakeGit([("lost", THIS_MORNING)])
        why, err = self.move(fake)
        self.assertIsNone(why, "the push arrived: that is a move")
        self.assertEqual((fake.pushed(), self.waits, err), (1, [], []))
        self.assertIn("the answer of the push was lost; main holds it", self.board_text().replace("The answer", "the answer"))
        self.assertIn("MAIN-MOVED", self.board_text())
        self.assertFalse((self.tmp / "main-allowed.txt").exists())

    def test_main_moved_on(self):
        fake = FakeGit([("moved", THIS_MORNING)])
        why, _ = self.move(fake)
        self.assertEqual(why, "main moved during the gate (now %s): rebase on it and gate again" % OTHER[:9])
        self.assertEqual((fake.pushed(), self.waits), (1, []))

    def test_rejected_is_not_tried_again(self):
        for text in ("To github.com:x/y.git\n ! [rejected]        main -> main (fetch first)\nerror: failed to push some refs to 'x'",
                     "remote: ... timed out waiting for the hook\n ! [remote rejected] main -> main (pre-receive hook declined)"):
            fake = FakeGit([("fail", text)])
            self.waits.clear()
            why, err = self.move(fake)
            self.assertEqual(why, "the push of main failed: " + text.strip()[-300:])
            self.assertEqual((fake.pushed(), self.waits, err), (1, [], []))
        self.assertEqual(gate.not_moved_kind(why), "other")

    def test_http_4xx_is_not_tried_again(self):
        text = "fatal: unable to access 'https://github.com/keepvaibin/nebula.git/': The requested URL returned error: 404"
        fake = FakeGit([("fail", text)])
        why, err = self.move(fake)
        self.assertEqual(why, "the push of main failed: " + text)
        self.assertEqual((fake.pushed(), self.waits, err), (1, [], []))

    def test_http_5xx_is_tried_again(self):
        text = "fatal: unable to access 'https://github.com/x/': The requested URL returned error: 502"
        fake = FakeGit([("fail", text), ("ok",)])
        why, _ = self.move(fake)
        self.assertEqual((why, fake.pushed(), self.waits), (None, 2, [15]))

    def test_six_network_failures(self):
        fake = FakeGit([("fail", THIS_MORNING)] * 6)
        why, err = self.move(fake)
        self.assertEqual(why, "GitHub could not be reached for the push of main (6 tries in 8 minutes): " + THIS_MORNING)
        self.assertEqual((fake.pushed(), self.waits), (6, [15, 30, 60, 120, 240]))
        self.assertEqual(len(err), 5, err)
        self.assertTrue(err[-1].endswith("trying again in 240 s (try 6 of 6)"), err)
        self.assertEqual(gate.not_moved_kind(why), "env", "the text begins with GitHub: the landing does not judge, and counts nothing")
        self.assertFalse((self.tmp / "main-allowed.txt").exists())
        self.assertEqual(self.board_text(), "", "main did not move: no MAIN-MOVED line")

    def test_a_fetch_that_does_not_work_does_not_stop_the_tries(self):
        fake = FakeGit([("fail", THIS_MORNING), ("ok",)])
        calls = {"n": 0}
        real = fake.__call__

        def flaky(cmd, **kw):
            args = [str(a) for a in cmd[3:]]
            if args[0] == "fetch" and calls["n"] == 1:          # the fetch after the failed push
                calls["n"] += 1
                return subprocess.CompletedProcess(cmd, 128, "", "fatal: unable to access")
            if args[0] == "fetch":
                calls["n"] += 1
            return real(cmd, **kw)
        why, _ = self.move(flaky)
        self.assertEqual((why, fake.pushed(), self.waits), (None, 2, [15]))

    def test_push_is_network_text(self):
        yes = (THIS_MORNING, "fatal: unable to access 'x': Could not resolve host: github.com", "fatal: unable to access 'x': Failed to connect to github.com port 443",
               "fatal: the remote end hung up unexpectedly", "error: RPC failed; curl 56 OpenSSL SSL_read", "fatal: early EOF",
               "fatal: unable to access 'x': Connection timed out after 300000 milliseconds", "fatal: unable to access 'x': The requested URL returned error: 503",
               "send-pack: unexpected disconnect while reading sideband packet\nfatal: the remote end hung up unexpectedly",
               "ssh: connect: Connection reset by peer")
        no = ("To x\n ! [rejected] main -> main (fetch first)", " ! [remote rejected] main -> main (pre-receive hook declined)",
              "fatal: unable to access 'x': The requested URL returned error: 403", "fatal: unable to access 'x': The requested URL returned error: 401",
              "main moves only through the gate: use gate.py", "error: failed to push some refs", "")
        for text in yes:
            self.assertTrue(gate.push_is_network(text), text)
        for text in no:
            self.assertFalse(gate.push_is_network(text), text)
        self.assertEqual(gate.PUSH_WAITS, (15, 30, 60, 120, 240))


if __name__ == "__main__":
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("push_again_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    sys.exit(0 if result.wasSuccessful() else 1)
