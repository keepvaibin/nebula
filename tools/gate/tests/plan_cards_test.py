"""plan_cards_test.py: what the proofs of the step cards ask for, read by tools/plan/proofs.py (AH, after the trial).

    python -X utf8 -I tools/gate/tests/plan_cards_test.py

  The cards of steps 12 to 145 (the plan's own files, as they are):
    proofs_name_only_what_the_agent_can_use   every program, script and test a proof names is a file or a name on main, is not on AGENT.md's list
                                              `Programs you never run`, and every option written beside it is one it takes (where the source says);
                                              the findings that stand are listed in KNOWN_FINDINGS by step: a new one fails, and so does a listed one
                                              that is gone (the card was mended: take it out of the list)
    step_lines_hold_every_check_the_proofs_name   every extra check a proof names (stress, sanitizer/tour, soak, soak/long, replays/software,
                                              replays/rates, speed/anchor) is in the step's line of docs/plan/native/step-checks.tsv
    the_forbidden_list                        AGENT.md's list is there and each program on it is a file of the tree
  The reader on proofs written here (a planted fault is named, the right case is silent):
    a forbidden program, a program that is not on main, an option the program does not take, a sub-command it does not have, a test that is not
    on main, a game option the game does not take, a diagnostics name it does not know; and the right form of each.
"""
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(TREE / "tools" / "plan"))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import plan  # noqa: E402
import proofs  # noqa: E402

FIRST_STEP, LAST_STEP = 12, 145  # 138 until 11-24, when G2.3 became eight steps

# What the test found in the cards when it was written (11-23), each a card that is to be mended; the cards are another helper's and the
# planner's to change. A finding is the step and the text `proofs.problems` gives.
KNOWN_FINDINGS = {}


def agent_md():
    return (TREE / "docs" / "plan" / "native" / "AGENT.md").read_text(encoding="utf-8")


def cards_12_to_145():
    steps = [s for s in plan.read_base(TREE) if FIRST_STEP <= int(s["order"]) <= LAST_STEP]
    return [(s, proofs.proof_of(plan.card_text(TREE, s))) for s in steps]


class RealCards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.repo = proofs.Repo(TREE)
        cls.forbidden = proofs.forbidden_programs(agent_md())
        cls.cards = cards_12_to_145()

    def test_there_are_134_cards_and_each_has_a_proof(self):
        self.assertEqual(len(self.cards), LAST_STEP - FIRST_STEP + 1)
        self.assertEqual([s["step"] for s, proof in self.cards if not proof.strip()], [], "a card with no Proof item")

    def test_the_forbidden_list(self):
        self.assertIn("tools/verify/size_probe.py", self.forbidden, "the program the trial's S0.2.6 card named")
        self.assertGreaterEqual(len(self.forbidden), 8)
        for path in self.forbidden:
            self.assertTrue((TREE / path).exists(), "AGENT.md lists %s, and it is not in the tree" % path)

    def test_proofs_name_only_what_the_agent_can_use(self):
        found = {}
        for s, proof in self.cards:
            bad = proofs.problems(proofs.read(proof), self.repo, self.forbidden)
            if bad:
                found[s["step"]] = bad
        new = {k: v for k, v in found.items() if v != KNOWN_FINDINGS.get(k)}
        gone = {k: v for k, v in KNOWN_FINDINGS.items() if k not in found}
        self.assertEqual(new, {}, "a proof names what the agent cannot use (mend the card, or list it in KNOWN_FINDINGS)")
        self.assertEqual(gone, {}, "the card was mended: take its finding out of KNOWN_FINDINGS")

    def test_step_lines_hold_every_check_the_proofs_name(self):
        missing = {}
        for s, proof in self.cards:
            lacking = [c for c in proofs.checks_named(proof) if c not in plan.also_names(TREE, s["step"])]
            if lacking:
                missing[s["step"]] = lacking
        self.assertEqual(missing, {}, "docs/plan/native/step-checks.tsv lacks a line the proof asks for")

    def test_the_checks_a_proof_names_are_the_tools_extra_checks(self):
        self.assertEqual(sorted(name for name, _ in proofs.CHECK_WORDS), sorted(plan.ALSO_CHECKS))


class Reader(unittest.TestCase):
    """The reader on proofs written here. The repository is the real one: the names below exist or do not in it."""

    @classmethod
    def setUpClass(cls):
        cls.repo = proofs.Repo(TREE)
        cls.forbidden = proofs.forbidden_programs(agent_md())

    def problems(self, text):
        return proofs.problems(proofs.read("\n  1. " + text), self.repo, self.forbidden)

    def test_a_program_the_agent_never_runs_is_named(self):
        self.assertEqual(self.problems("Run `python tools/verify/size_probe.py --bin x` and compare."),
                         ["tools/verify/size_probe.py is a program the agent never runs (AGENT.md, Programs you never run)"])

    def test_a_program_that_is_not_on_main_is_named(self):
        self.assertEqual(self.problems("The load logs (`loads.py`) before and after."),
                         ["loads.py is not a file on main (or its name is not one file's)"])

    def test_an_option_the_program_does_not_take_is_named_and_one_it_takes_is_not(self):
        self.assertEqual(self.problems("`python tools/settings_table.py --no-such-option` passes."),
                         ["tools/settings_table.py takes no option --no-such-option"])
        self.assertEqual(self.problems("`python tools/settings_table.py --check` passes."), [])

    def test_a_sub_command_the_program_does_not_have_is_named(self):
        self.assertEqual(self.problems("`pgo.py check` passes."), [])
        self.assertEqual(self.problems("`pgo.py frobnicate` passes."), ["tools/gate/pgo.py has no sub-command frobnicate"])

    def test_a_test_that_is_not_on_main_is_named_and_one_that_is_is_not(self):
        self.assertEqual(self.problems("The test `galaxy_no_such_thing_tests` passes."), ["test galaxy_no_such_thing_tests is not on main"])
        self.assertEqual(self.problems("`galaxy_pacing_tests` passes."), [])
        self.assertEqual(self.problems("`frame_schedule_tests.cpp` on simulated displays."), [])

    def test_a_game_option_the_game_does_not_take_is_named(self):
        self.assertEqual(self.problems("Run with `--diagnose resolution-steps`."), ["the game takes no option --diagnose"])
        self.assertEqual(self.problems("Run with `--diagnostics timeline`."), [])
        self.assertEqual(self.problems("Run with `--diagnostics no-such-name`."), ["--diagnostics has no name no-such-name"])

    def test_the_checks_a_proof_names(self):
        self.assertEqual(proofs.checks_named("The `soak` check passes."), ["soak"])
        self.assertEqual(proofs.checks_named("The `soak/long` check passes."), ["soak/long"])
        self.assertEqual(proofs.checks_named("A time rate of 2 gives the same hashes; a software device too."),
                         ["replays/software", "replays/rates"])
        self.assertEqual(proofs.checks_named("The thread sanitizer clean over the tour."), ["sanitizer/tour"])
        self.assertEqual(proofs.checks_named("The pictures are the same."), [])


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_cards_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
