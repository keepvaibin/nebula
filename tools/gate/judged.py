"""judged.py: what the stress and validate lanes judge besides the runner's verdict, test by test (package T12), without the game.

The lanes read each test's result after its run, and again after the second run alone, so a hash or a picture that differs
is a result of the test like any other: it is run again alone, and a test that passes alone is settled as the lane's rule says.

  hashes     the step hashes of a tour test that has a recording (tests/replays) against the recording (tools/verify/step_hash.py);
  pictures   the pictures a test took against their approved copies (tools/gate/pictures.py check, which the gate runs).
"""
import pathlib
import sys

TREE = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(TREE / "tools" / "verify"))
import step_hash  # noqa: E402

# What pictures.py prints for a picture, by its first word (judged here: a difference; the rest are no fault of the run).
DIFFERENT = ("changed", "missing")


def hash_results(work, results, recordings=None):
    """`results` {test: (kind, message)} with a test whose step hashes differ from its recording (a test that passed, has a
    recording and wrote hashes) made a fail; a test that passed and wrote none is a fail too. A test that passed with a window other
    than the recordings' (step_hash.RECORDED_WINDOW) is a tool fault, "recorded at WxH, run at WxH": its hashes are not compared. Others
    are left as they are."""
    recordings = pathlib.Path(recordings) if recordings is not None else step_hash.RECORDINGS
    runs = step_hash.runs_by_test(work)
    out = dict(results)
    for test, (kind, _) in results.items():
        run = runs.get(test)
        ref = step_hash.plain_steps_of_file(recordings / (test + ".tsv"))
        if kind != "pass" or run is None or ref is None:
            continue
        note = step_hash.window_note(run)
        if note:
            out[test] = ("tool", note)
            continue
        same, text = step_hash.same_steps(step_hash.plain_steps(step_hash.read_facts(run)[0]), ref, test)
        if not same:
            out[test] = ("fail", "the step hashes differ from the recording: " + text)
    return out


def picture_results(lines, results):
    """`results` with a test whose picture differs from the approved one (a line of pictures.py `changed` or `missing`) made a fail
    when it passed: the first such picture is named."""
    out = dict(results)
    for line in lines:
        words = line.split(None, 2)
        if len(words) < 2 or words[0] not in DIFFERENT:
            continue
        parts = words[1].split("/")
        test = parts[1] if len(parts) >= 4 else ""
        if test in out and out[test][0] == "pass":
            out[test] = ("fail", "the picture %s differs from its approved copy: %s" % (
                "/".join(parts[3:]), words[2].strip() if len(words) > 2 else words[0]))
    return out
