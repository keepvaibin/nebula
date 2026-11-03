#!/usr/bin/env python3
"""What a run's interpolation showed, from GALAXY_TRACE_INTERP_SLOTS: python tools/verify/interp_slots.py <file> <target fps>

Content time is game frames shown so far: a slot's sequence number plus its t. Even motion means content advances by 60 / rate each
present. Prints the presents per second, the step's spread against that ideal, how late slots began, and how long presents took.
"""
import sys


def percentile(values, p):
    return values[min(len(values) - 1, int(len(values) * p))]


def main():
    path, target = sys.argv[1], float(sys.argv[2])
    rows = []
    with open(path) as f:
        frequency = float(f.readline().split()[1])
        for line in f:
            slot, began, ended, t, sequence, kind = line.split()
            rows.append((int(slot), int(began), int(ended), float(t), int(sequence), int(kind)))
    if len(rows) < 100:
        print('too few slots', len(rows))
        return 1
    rows = rows[len(rows) // 10:]  # past the boot
    seconds = (rows[-1][1] - rows[0][1]) / frequency
    ideal = 60.0 / target
    steps = []
    backward = 0
    jumps = 0
    for a, b in zip(rows, rows[1:]):
        step = (b[4] + b[3]) - (a[4] + a[3])
        if step < -1e-6:
            backward += 1
        if step > 1.5:
            jumps += 1  # a pause, a stall or a cut: reported, not averaged in
            continue
        steps.append(step)
    steps.sort()
    late = sorted((r[1] - r[0]) * 1000.0 / frequency for r in rows)
    took = sorted((r[2] - r[1]) * 1000.0 / frequency for r in rows)
    intervals = sorted((b[1] - a[1]) * 1000.0 / frequency for a, b in zip(rows, rows[1:]))
    off = sorted(abs(s - ideal) / ideal for s in steps)
    print(f'target {target:g}: presents/s {len(rows) / seconds:.1f}, real frames {sum(r[5] for r in rows) / seconds:.1f}/s, '
          f'step ideal {ideal:.4f} p1 {percentile(steps, .01):.4f} p50 {percentile(steps, .5):.4f} p99 {percentile(steps, .99):.4f}, '
          f'off by p50 {percentile(off, .5) * 100:.1f}% p99 {percentile(off, .99) * 100:.1f}%, backward {backward}, jumps {jumps}, '
          f'interval ms p50 {percentile(intervals, .5):.2f} p99 {percentile(intervals, .99):.2f}, '
          f'late ms p99 {percentile(late, .99):.2f}, present ms p50 {percentile(took, .5):.2f} p99 {percentile(took, .99):.2f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
