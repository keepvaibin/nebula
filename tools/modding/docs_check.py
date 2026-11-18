"""docs_check.py: the modding docs' generated parts, against what the build makes (the gate's docs check).

    python -X utf8 -I tools/modding/docs_check.py --lumalink <program> --lumalink-tree <checkout> --work <folder>
                                                  [--names <folder>] [--pak <game.pak>]

Each part is made again from its source and compared with the page in docs/modding, writing nothing there:
  - the services' pages, from the SDK's headers and the compiler's forms (service_pages.py, with
    `lumalink services --forms`);
  - LumaKit's pages, from LumaKit's sources in the LumaLink checkout (kit_pages.py);
  - the build codes, from the compiler's list and messages (build_codes.py);
  - LumaLink's commands, from the program's help (lumalink_commands.py);
  - the validator's rules (validator_rules.py), the game's events (events_list.py) and nerves (nerves_list.py);
  - with --names (the game's names, as LumaLink builds with them), every LumaScript sample of the docs, built
    (samples_check.py);
  - with --pak, the game's tables (tables_dump.py).
One line per part: pass, or what differs (the whole difference in <work>/<part>.log). Exit 0 only when every part
passes.
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DOCS = ROOT / "docs" / "modding"
PY = [sys.executable, "-X", "utf8", "-I"]


def run(cmd, log):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    log.write_text("$ %s\n%s%s" % (" ".join(str(c) for c in cmd), r.stdout, r.stderr), encoding="utf-8")
    return r.returncode, (r.stdout + r.stderr).strip()


def same_pages(made: pathlib.Path, names):
    """The pages a list generator wrote into `made`, against docs/modding/reference: the names that differ."""
    out = []
    for name in names:
        page = DOCS / "reference" / name
        new = made / name
        if not new.is_file():
            out.append("%s: not made" % name)
        elif not page.is_file() or page.read_text(encoding="utf-8").replace("\r\n", "\n") != new.read_text(encoding="utf-8").replace("\r\n", "\n"):
            out.append(name)
    return out


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--lumalink", required=True)
    p.add_argument("--lumalink-tree", required=True)
    p.add_argument("--work", required=True)
    p.add_argument("--names")
    p.add_argument("--pak")
    args = p.parse_args(argv)
    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    tree = pathlib.Path(args.lumalink_tree)
    sdk = ROOT / "native" / "include"
    results = []

    forms = work / "forms.tsv"
    code, said = run([args.lumalink, "services", "--sdk", sdk, "--forms"], work / "forms.log")
    if code != 0:
        results.append(("services", "lumalink services --forms: exit %d: %s" % (code, said[-200:])))
    else:
        forms.write_text(said + "\n", encoding="utf-8")
        code, said = run(PY + [HERE / "service_pages.py", "--forms", forms, "--sdk", sdk, "--check"], work / "services.log")
        results.append(("services", None if code == 0 else "; ".join(l for l in said.splitlines() if not l.startswith(("+", "-", "@")))[:400]))
    for part, cmd in [
        ("lumakit", PY + [HERE / "kit_pages.py", tree / "lumakit", "--check"]),
        ("build-codes", PY + [HERE / "build_codes.py", tree, "--check"]),
        ("commands", PY + [HERE / "lumalink_commands.py", args.lumalink, "--check"]),
        ("validator-rules", PY + [HERE / "validator_rules.py", "--check"]),
    ] + ([("samples", PY + [HERE / "samples_check.py", "--lumalink", args.lumalink, "--lumalink-tree", tree, "--names",
                            args.names, "--work", work / "samples"])] if args.names else []):
        code, said = run(cmd, work / (part + ".log"))
        results.append((part, None if code == 0 else "; ".join(l for l in said.splitlines() if not l.startswith(("+", "-", "@")))[:400]))
    lists = [("events", [HERE / "events_list.py", ROOT], ["events.md"]),
             ("nerves", [HERE / "nerves_list.py", ROOT], ["nerves.md", "nerves-a-f.md", "nerves-g-m.md", "nerves-n-z.md"])]
    if args.pak:
        lists.append(("tables", [HERE / "tables_dump.py", args.pak, ROOT], ["tables.md"]))
    for part, cmd, names in lists:
        with tempfile.TemporaryDirectory(dir=work) as made:
            code, said = run(PY + cmd + [made], work / (part + ".log"))
            differ = same_pages(pathlib.Path(made), names) if code == 0 else ["exit %d: %s" % (code, said[-200:])]
        results.append((part, None if not differ else "differs: " + ", ".join(differ)))
    for part, why in results:
        print("%-16s %s" % (part, "pass" if why is None else "FAIL  " + why))
    failed = [part for part, why in results if why is not None]
    print("docs: %d of %d parts as the build makes them%s" % (len(results) - len(failed), len(results),
                                                               "" if not failed else "; not: " + ", ".join(failed)))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
