"""plan_names_test.py: the names the gate and the step tool must agree on (brief T10, part H), without the game or a build.

    python -X utf8 -I tools/gate/tests/plan_names_test.py [--work FOLDER]

  also_names_agree      every check name docs/plan/native/step-checks.tsv asks for is in the gate's ALSO_PARTS and in the step tool's
                        ALSO_CHECKS (and the two have the same names); a name one of them lacks is named
  long_proofs           G3.7 and G5.5 ask for soak/long in place of soak, G3.7 keeps its other names
  the_last_step_..      speed/anchor is asked by the last step (by order) of every package in steps.tsv and by no other
  also_names_catch      the same reading on planted tables: a name the gate lacks, one the step tool lacks and one the table has
                        and neither has are each reported
  row_labels_agree      every state of tools/gate/kinds.py and of the gate (STATES), printed as the gate prints a row, is read by the
                        step tool's GATE_ROW and counted as itself or as the pass it is; an invented label is not read
  owed_line             the board line for owed rows: a measure alone, the tablet's part alone, both; each names the rows
"""
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
TREE = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(TREE / "tools" / "plan"))
sys.path.insert(0, str(TREE / "tools" / "verify"))
import gate  # noqa: E402
import kinds  # noqa: E402
import plan  # noqa: E402


def table_names(path):
    """The check names of a step-checks table (its second column), each once."""
    names = []
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines()[1:]:
        cells = line.split("\t")
        if len(cells) >= 2 and cells[1] not in names:
            names.append(cells[1])
    return names


def name_problems(used, gate_names, plan_names):
    """What is wrong with the names a table uses: [(name, where it is missing)]."""
    out = []
    for name in used:
        if name not in gate_names:
            out.append((name, "the gate's ALSO_PARTS"))
        if name not in plan_names:
            out.append((name, "the step tool's ALSO_CHECKS"))
    for name in sorted(set(gate_names) - set(plan_names)):
        out.append((name, "the step tool's ALSO_CHECKS"))
    for name in sorted(set(plan_names) - set(gate_names)):
        out.append((name, "the gate's ALSO_PARTS"))
    return sorted(set(out))


class AlsoNames(unittest.TestCase):
    def test_also_names_agree(self):
        used = table_names(plan.native(TREE) / plan.STEP_CHECKS_FILE)
        self.assertTrue(used)
        self.assertEqual(name_problems(used, gate.ALSO_PARTS, plan.ALSO_CHECKS), [])
        self.assertIn("soak/long", plan.ALSO_CHECKS)

    def test_the_two_long_proofs_ask_for_the_long_soak(self):
        rows = [l.split("	") for l in (plan.native(TREE) / plan.STEP_CHECKS_FILE).read_text(encoding="utf-8").splitlines()[1:]]
        for step in ("G3.7", "G5.5"):
            names = [r[1] for r in rows if r[0] == step]
            self.assertIn("soak/long", names, step)
            self.assertNotIn("soak", names, step)
        self.assertEqual([r[1] for r in rows if r[0] == "G3.7"], ["stress", "sanitizer/tour", "soak/long", "speed/anchor"], "G3.7 keeps its other names")

    def test_the_last_step_of_every_package_asks_for_the_anchor(self):
        steps = [l.split("	") for l in (plan.native(TREE) / "steps.tsv").read_text(encoding="utf-8").splitlines()[1:] if l.strip()]
        last = {}
        for cells in steps:                        # order, step, package
            if cells[2] not in last or int(cells[0]) > int(last[cells[2]][0]):
                last[cells[2]] = cells
        rows = [l.split("	") for l in (plan.native(TREE) / plan.STEP_CHECKS_FILE).read_text(encoding="utf-8").splitlines()[1:] if l.strip()]
        asked = sorted(r[0] for r in rows if r[1] == "speed/anchor")
        self.assertEqual(asked, sorted(c[1] for c in last.values()), "speed/anchor: the last step by order of every package, and no other")
        self.assertGreaterEqual(len(asked), 24)

    def test_also_names_catch(self):
        # a name the gate lacks, one the step tool lacks, one neither has
        self.assertEqual(name_problems(["stress", "soak/long"], {"stress": 1}, ("stress", "soak/long")),
                         [("soak/long", "the gate's ALSO_PARTS")])
        self.assertEqual(name_problems(["soak/long"], {"soak/long": 1, "stress": 1}, ("stress",)),
                         [("soak/long", "the step tool's ALSO_CHECKS")])
        self.assertEqual(name_problems(["nightly/x"], {"stress": 1}, ("stress",)),
                         [("nightly/x", "the gate's ALSO_PARTS"), ("nightly/x", "the step tool's ALSO_CHECKS")])
        # a name only one of the two has, though no table row uses it
        self.assertEqual(name_problems([], {"stress": 1, "soak": 1}, ("stress",)), [("soak", "the step tool's ALSO_CHECKS")])
        self.assertEqual(name_problems([], {"stress": 1}, ("stress", "soak")), [("soak", "the gate's ALSO_PARTS")])


class RowLabels(unittest.TestCase):
    @staticmethod
    def printed(state):
        """A row as the gate prints it (gate.py Gate.record)."""
        return "%-34s %-7s %7.1f s  %s" % ("some/check", gate.LABELS.get(state, state), 1.5, "the detail")

    def test_row_labels_agree(self):
        states = set(kinds.ORDER) | set(gate.STATES)
        for state in sorted(states):
            rows = plan.read_gate_output([self.printed(state)])
            self.assertEqual(len(rows), 1, "the row of state %r is not read: %r" % (state, self.printed(state)))
            self.assertEqual(rows[0][0], "some/check")
            self.assertEqual(rows[0][2], "the detail")
        self.assertEqual(plan.read_gate_output([self.printed("worse")])[0][1], "WORSE")
        self.assertEqual(plan.read_gate_output([self.printed("stress")])[0][1], "STRESS")
        self.assertEqual(plan.read_gate_output([self.printed("known")])[0][1], "PASS")

    def test_a_label_the_gate_does_not_print_is_not_a_row(self):
        self.assertEqual(plan.read_gate_output(["some/check  INVENTED   1.5 s  detail"]), [])
        self.assertEqual(plan.read_gate_output([self.printed("pass").replace("pass", "again")]), [])


class OwedLine(unittest.TestCase):
    def line(self, owed):
        return gate.owed_line("Helper rules", "land", "20261121-1-land", "0123456789abcdef", owed)

    def test_the_measure_alone(self):
        text = self.line(["measure/step/pc"])
        self.assertIn("OWED: the speed measure of land gate", text)
        self.assertIn("on 012345678: measure/step/pc.", text)
        self.assertNotIn("tablet", text)
        self.assertIn('--only "measure/step/pc"', text)

    def test_the_tablet_part_alone(self):
        text = self.line(["android/tablet", "tour/tablet"])
        self.assertIn("OWED: the tablet's part of land gate", text)
        self.assertIn(": android/tablet, tour/tablet.", text)
        self.assertNotIn("measure", text)
        self.assertIn("when the tablet is back", text)

    def test_both(self):
        text = self.line(["tour/tablet", "measure/step/pc"])
        self.assertIn("OWED: the tablet's part and the speed measure of land gate", text)
        self.assertIn(": tour/tablet, measure/step/pc.", text)
        self.assertIn("when the tablet is back and the machine is quiet", text)

    def test_the_tablets_measure_waits_for_the_tablet(self):
        text = self.line(["measure/step/tablet"])
        self.assertIn("the speed measure", text)
        self.assertIn("when the tablet is back (", text)
        self.assertNotIn("quiet", text)


def main():
    result = unittest.main(argv=[sys.argv[0]], exit=False, verbosity=1).result
    print("plan_names_test: %s (%d tests)" % ("pass" if result.wasSuccessful() else "FAIL", result.testsRun))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
