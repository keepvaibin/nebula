"""gate_tablet_test.py: the gate's PC-against-device comparison plays the PC at the device's screen size: the size is
read from a device run's log (the Vulkan renderer's swap chain), and the tour written for it carries that window on every
stop. The game runs the actors the picture shows, so a PC run in another shape runs other actors and its steps differ
from the device's from the first one (gate 20261117-121553: 1 of 101 stops the same at 1280x720 against a 2800x1752
tablet)."""
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import gate  # noqa: E402
import gate_tour  # noqa: E402


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        run = t / "runs" / "tour-tablet" / "001-BattleShipGalaxy-1" / "vulkan"
        run.mkdir(parents=True)
        (run / "run.log").write_text("[vulkan] swapchain 1280x720 transform=1 images=5\n"
                                     "[Renderer] internal resolution 1280x720 -> 2800x1752\n"
                                     "[vulkan] swapchain 2800x1752 transform=1 images=5\n", encoding="utf-8")
        check(gate.device_screen(t / "runs") == (2800, 1752), "the device's screen is its last swap chain size")
        check(gate.device_screen(t / "none") is None, "no runs: no size")

        suite = gate_tour.write(t / "tour-tablet-pc", ["BattleShipGalaxy 1", "HoneyBeeKingdomGalaxy 1"], window=(2800, 1752))
        names = json.loads(suite.read_text(encoding="utf-8"))["tests"]
        windows = [json.loads((suite.parent / n / "test.json").read_text(encoding="utf-8")).get("window") for n in names]
        check(len(names) == 2 and windows == [[2800, 1752], [2800, 1752]], "every stop plays at the size: %s" % windows)
        plain = gate_tour.write(t / "tour", ["BattleShipGalaxy 1"])
        test = json.loads((plain.parent / json.loads(plain.read_text(encoding="utf-8"))["tests"][0] / "test.json")
                          .read_text(encoding="utf-8"))
        check("window" not in test, "without a size the tour keeps the harness's window")
        a = gate_tour.test_for(0, "BattleShipGalaxy", 1)[1]
        b = gate_tour.test_for(0, "BattleShipGalaxy", 1, window=(2800, 1752))[1]
        b.pop("window")
        check(a == b, "the size changes nothing else: the same input")

    if failures:
        print("gate_tablet_test.py: FAILED:\n  " + "\n  ".join(failures))
        return 1
    print("gate_tablet_test.py: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
