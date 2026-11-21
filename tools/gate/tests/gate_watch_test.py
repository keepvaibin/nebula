"""gate_watch_test.py: tools/gate/gate.py turns the game's watches on in the lanes of their renderer (ledger P-332):
every harness run and every mod test run on Direct3D 11 gets --diagnostics constants-watch through the runner's
LUMA_TEST_DIAGNOSTICS, a name given already is kept and not repeated, and the other renderers' runs get nothing. The
lanes' runs are caught at gate.run, so no game starts."""
import pathlib
import sys
import tempfile
import types

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import gate  # noqa: E402


def main():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    check(gate.watch_env("d3d11", {}).get("LUMA_TEST_DIAGNOSTICS") == "constants-watch",
          "a Direct3D 11 run asks for the constants watch")
    for renderer in ("d3d12", "vulkan"):
        check("LUMA_TEST_DIAGNOSTICS" not in gate.watch_env(renderer, {"LUMA_TEST_PAKS": "p"}),
              "a %s run asks for no watch" % renderer)
    kept = gate.watch_env("d3d11", {"LUMA_TEST_DIAGNOSTICS": "ring-stress"})
    check(kept["LUMA_TEST_DIAGNOSTICS"] == "ring-stress,constants-watch", "a name given already is kept: %s" % kept)
    once = gate.watch_env("d3d11", {"LUMA_TEST_DIAGNOSTICS": "constants-watch,probe-reads"})
    check(once["LUMA_TEST_DIAGNOSTICS"] == "constants-watch,probe-reads", "the watch is named once: %s" % once)
    check(gate.watch_env("d3d11", {"LUMA_TEST_PAKS": "p"})["LUMA_TEST_PAKS"] == "p", "the rest of the environment stays")

    # The lanes themselves: what each passes to the runner, caught at gate.run.
    caught = []

    def fake_run(cmd, log, env=None, cwd=None, timeout=None):
        caught.append(dict(env or {}))
        pathlib.Path(log).write_text("", encoding="utf-8")
        return 0

    real_run = gate.run
    gate.run = fake_run
    try:
        with tempfile.TemporaryDirectory() as tmp:
            t = pathlib.Path(tmp)
            g = types.SimpleNamespace(work=t, commit="0123456789", bin=t / "bin", level="main", android_bin=t / "abin",
                                      args=types.SimpleNamespace(paks="paks", build_root=t, lumalink="lumalink",
                                                                 jobs="4", mod_parallel=2))
            g.paks_text = lambda: g.args.paks    # gate.Gate.paks_text: --paks as given
            g.mods = "full"    # the mod lane runs (mods = off would leave it out)
            for renderer in gate.RENDERERS:
                caught.clear()
                gate.harness(g, t / "suite.json", renderer, "engine")(t / ("harness-%s.log" % renderer))
                gate.mod_tests(g, renderer)(t / ("mods-%s.log" % renderer))
                diagnostics = [e.get("LUMA_TEST_DIAGNOSTICS") for e in caught]
                want = ["constants-watch"] * 2 if renderer == "d3d11" else [None, None]
                check(diagnostics == want, "the %s lanes' runs ask for %s, not %s" % (renderer, want, diagnostics))
                check(all(e.get("LUMA_TEST_PAKS") == "paks" for e in caught), "the %s lanes keep their paks" % renderer)
    finally:
        gate.run = real_run

    print("\n".join(failures) if failures else "gate watch: every case holds")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
