"""check_clock_domains.py: no std::chrono::steady_clock value is turned into, or read as, a moment on the runtime's clock.

    python -X utf8 -I tools/check_clock_domains.py [<tree>]

The runtime's clock is galaxy::qpc_now() (platform_time.h): QueryPerformanceCounter on Windows, CLOCK_MONOTONIC on Linux and
Android, CLOCK_UPTIME_RAW on Apple. std::chrono::steady_clock is the same clock on the first three and CLOCK_MONOTONIC_RAW on Apple,
which goes on counting while the Mac sleeps: a steady_clock value used as a qpc_now() moment is hours off on a Mac that has slept
(the 2.0.0 beta's Mac start that slept for ever in its frame wait). Differences of two steady_clock readings are fine.

Refused in the program's sources (native/, runtime/ and platform/, not the tests' fakes of the clocks):
  - qpc_of( : the conversion of a steady_clock time point into qpc_now()'s ticks (removed; qpc_from_ns takes the runtime clock's
    own nanoseconds);
  - a line that names steady_clock and also qpc_now, qpc_to_ns, qpc_from_ns, clock_gettime_nsec_np, CLOCK_UPTIME_RAW or
    mach_absolute_time.
Exit 0 when there is none, 1 when there is (each printed with file and line), 2 when the tree cannot be read.
"""
import pathlib
import re
import sys

ROOTS = ("native", "runtime", "platform")
SUFFIXES = (".cpp", ".h", ".hpp", ".mm", ".c")
OTHER_CLOCK = re.compile(r"qpc_now|qpc_to_ns|qpc_from_ns|clock_gettime_nsec_np|CLOCK_UPTIME_RAW|mach_absolute_time")


def code_of(line):
    """The line without its // comment."""
    at = line.find("//")
    return line if at < 0 else line[:at]


def scan(tree):
    """[(path, line number, what)] of every use of the two clocks together, or of the removed conversion."""
    found = []
    for root in ROOTS:
        base = tree / root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SUFFIXES or not path.is_file() or "third_party" in path.parts:
                continue
            if path.name.endswith("_tests.cpp"):
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                code = code_of(line)
                if "qpc_of(" in code:
                    found.append((path.relative_to(tree).as_posix(), number, "qpc_of( turns a steady_clock time point into qpc_now()'s ticks"))
                if "steady_clock" in code and OTHER_CLOCK.search(code):
                    found.append((path.relative_to(tree).as_posix(), number, "steady_clock and the runtime's clock on one line"))
    return found


def main(argv):
    tree = pathlib.Path(argv[1]) if len(argv) > 1 else pathlib.Path(__file__).resolve().parents[1]
    if not tree.is_dir():
        print("check_clock_domains.py: no such folder: " + str(tree))
        return 2
    found = scan(tree)
    for path, number, what in found:
        print("%s:%d: %s" % (path, number, what))
    print("check_clock_domains.py: %s" % ("%d places" % len(found) if found else "no steady_clock value meets the runtime's clock"))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
