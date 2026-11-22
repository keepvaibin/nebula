"""long_parts.py: how the long run's parts are cut into pieces and how they give way to landings (package B15, points 10, 12 to 14).

A landing is to wait for the long run no longer than one piece (PIECE_SECONDS). So a part whose list of stops would take longer is
run as pieces, each of which takes the lock for itself, and a piece does not take the lock while a branch waits or runs in the
landing queue (landing-queue.tsv). The tablet's pieces take the tablet's lock only and play beside the PC's parts; they too wait
for the landing queue, and for a speed measure on the PC (measure.lock) unless a proof has shown the measure keeps its figures
beside the tablet's lane (TABLET_BESIDE_MEASURE).

Seconds per stop come from the long runs on record (gate-runs 20261120-054245-790626cb5-long and 20261121-002638-c693079e8-long, each
check's seconds in result.json) and from the landing that ran the sanitizer's tour (20261121-202557-e54a39c57-main).
"""
import math
import os
import pathlib
import time

HERE = pathlib.Path(__file__).resolve().parent

PIECE_SECONDS = 420             # no part holds the machine longer than seven minutes (a build of the run's own excepted)
POLL_SECONDS = 30.0             # a part looks at the landing queue every 30 seconds while it waits

# Seconds, from the record. The tours' nine lanes (three discs, three renderers) play side by side: 704 s for 102 stops.
TOURS_PER_STOP = 704.0 / 102
TABLET_PER_STOP = 2058.0 / 102      # 2058 s and 1964 s for 102 stops
TABLET_FIXED = 128 + 2              # tablet/prepare (115 to 128 s: the install and the game's data) and tablet/remove
TABLET_SUITE = 67                   # tests/android, played in the first piece
TABLET_PC_PER_STOP = 792.0 / 102    # the PC's own run of the same stops at the tablet's screen size (771 s, 792 s)
ASAN_PER_STOP = 709.0 / 102         # the sanitizer's tour (the landing of 11-21: 709 s for 102 stops)
COVERAGE_PER_STOP = 685.0 / 102     # the coverage build's tour
COVERAGE_ENGINE = 200               # the engine suite on the coverage build (18 tests); no time of its own on record
VALIDATE_PER_STOP = 125.0 / 12      # one copy per renderer, side by side, the debug layer on: 125 s for 12 stops on Direct3D 12

# Whether a speed pair of a landing is known to keep its figures while the tablet's lane plays beside it. The proof is part 3 of the
# brief of B14; until it is in TESTING-PROGRESS.md no tablet piece starts, and no cable transfer runs, while a measure holds measure.lock.
TABLET_BESIDE_MEASURE = False


def pieces_of(count, per_item, fixed=0.0, limit=PIECE_SECONDS):
    """The sizes of the pieces a list of `count` items is cut into: as few pieces as keep `fixed` + the piece's items * `per_item`
    within `limit` seconds, the sizes alike. One item is the least a piece holds. [] for no item."""
    if count <= 0:
        return []
    room = limit - fixed
    most = max(1, int(room // per_item)) if per_item > 0 else count
    n = max(1, math.ceil(count / most))
    base, extra = divmod(count, n)
    return [base + (1 if i < extra else 0) for i in range(n)]


def split(items, sizes):
    """The list cut into consecutive lists of the given sizes."""
    out, at = [], 0
    for size in sizes:
        out.append(list(items[at:at + size]))
        at += size
    return out


def piece_label(name, index, total):
    """How a piece is named in the lock's holder line and the part record: `tours 2/3` (just the name when there is one piece)."""
    return name if total <= 1 else "%s %d/%d" % (name, index + 1, total)


def part_of(label):
    """The part a piece's label belongs to: `tours 2/3` is `tours`."""
    head, _, tail = label.rpartition(" ")
    return head if head and "/" in tail and tail.replace("/", "").isdigit() else label


def seconds_of(parts, name):
    """The seconds the pieces of part `name` held the machine, and their longest: (sum, longest) from [{"part", "held"}]."""
    held = [p["held"] for p in parts if part_of(p["part"]) == name]
    return (sum(held), max(held)) if held else (0.0, 0.0)


class LandingWatch:
    """Whether a landing waits or runs in the landing queue: the long run's parts and the tablet's pieces do not start then. A row
    `waiting` counts; a row `running` counts while the queue runner (landing-runner.lock) is alive: the runner of a landing that was
    stopped leaves its rows `running`, and a part is never held up by them."""

    def __init__(self, queue_file, runner_lock, alive, read_holder):
        self.queue_file, self.runner_lock = pathlib.Path(queue_file), pathlib.Path(runner_lock)
        self.alive, self.read_holder = alive, read_holder

    def rows(self):
        try:
            text = self.queue_file.read_text(encoding="utf-8")
        except OSError:
            return []
        out = []
        for line in text.splitlines()[1:]:
            cells = line.split("\t")
            if len(cells) >= 4:
                out.append((cells[1], cells[3]))
        return out

    def busy(self):
        """"" when no landing waits or runs, else one plain line saying which."""
        rows = self.rows()
        waiting = [b for b, s in rows if s == "waiting"]
        running = [b for b, s in rows if s == "running"]
        if running:
            pid = self.read_holder(self.runner_lock)
            if pid is None or not self.alive(pid):
                running = []
        if not waiting and not running:
            return ""
        parts = (["%d waiting (%s)" % (len(waiting), ", ".join(waiting[:3]))] if waiting else []) + \
                (["%d running (%s)" % (len(running), ", ".join(running[:3]))] if running else [])
        return "the landing queue has " + " and ".join(parts)


def wait_clear(watch, wait_minutes, say=print, sleep=time.sleep, clock=time.time, poll=POLL_SECONDS, extra=None):
    """Waits (looking every `poll` seconds) while the landing queue is busy, or `extra()` says why not ("" to go on), for up to
    `wait_minutes`. Returns "" when the way is clear, else the last reason (the part is then not run)."""
    deadline = clock() + 60.0 * wait_minutes
    said = ""
    while True:
        why = watch.busy() or (extra() if extra else "")
        if not why:
            return ""
        if why != said:
            say("gate.py: a long run part waits: %s" % why)
            said = why
        if clock() >= deadline:
            return why
        sleep(poll)


def measure_held(plan_folder, alive, read_holder, started=None):
    """Why a speed measure of the PC is being measured ("" when none): measure.lock exists and its holder lives (or is held by hand)."""
    lock = pathlib.Path(plan_folder) / "measure.lock"
    if not lock.exists():
        return ""
    pid = read_holder(lock)
    if pid is not None and not alive(pid):
        return ""
    return "a speed measure holds %s" % lock.name


def process_alive(pid):
    """Whether a process of this id runs (landing.process_alive)."""
    import landing
    return landing.process_alive(pid)
