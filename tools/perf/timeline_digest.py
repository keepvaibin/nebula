"""Digest of a Nebula frame timeline (--diagnostics timeline, runtime/include/galaxy/frame_timeline.h).

Usage: python timeline_digest.py <timeline.bin> [--skip-seconds N]

Every timestamp in the file is QPC. The report covers:
  frame      game frame cadence (GameFrame events) and frames over 25 ms
  chain      per render chunk, paired by thread: submit call, render recording; per fence: command list
             submitted -> GPU start/end
  present    Present call cadence and duration, display picture cadence (picture number changes), and
             DXGI frame statistics: how many display refreshes each present
             stayed on screen and how many presents never reached the screen
  render     frame-slot (GPU fence) waits
"""
import argparse
import struct
import sys
from collections import defaultdict

EVENTS = {
    1: 'SubmitBegin', 2: 'SubmitEnd', 4: 'RenderBegin', 6: 'RenderSubmitted', 7: 'GpuBegin', 8: 'GpuEnd', 14: 'PresentBegin',
    15: 'PresentEnd', 16: 'DisplayStats', 21: 'DisplayCopy', 22: 'SlotWaitBegin', 23: 'SlotWaitEnd', 24: 'VBlank',
    25: 'DisplayPresented', 26: 'GameFrame',
}


def load(path):
    with open(path, 'rb') as f:
        header = f.read(64)
        if header[:8] != b'NBTL0001':
            sys.exit('not a Nebula timeline file')
        version, freq, count, written, pid = struct.unpack_from('<5Q', header, 8)
        data = f.read(count * 32)
    records = []
    for qpc, a, b, thread, event, valid in struct.iter_unpack('<QQQIHH', data):
        if valid:
            records.append((qpc, event, a, b, thread))
    records.sort(key=lambda r: r[0])
    return freq, written, pid, records


def stats(values):
    if not values:
        return 'n=0'
    v = sorted(values)
    n = len(v)

    def q(p):
        return v[min(n - 1, int(p * (n - 1) + 0.5))]
    return (f'n={n} mean={sum(v) / n:.3f} p50={q(0.5):.3f} p90={q(0.9):.3f} '
            f'p99={q(0.99):.3f} max={v[-1]:.3f}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('path')
    ap.add_argument('--skip-seconds', type=float, default=0.0,
                    help='ignore events in the first N seconds (boot, shader warmup)')
    args = ap.parse_args()
    freq, written, pid, records = load(args.path)
    if not records:
        sys.exit('empty timeline')
    t0 = records[0][0]
    start = t0 + int(args.skip_seconds * freq)
    records = [r for r in records if r[0] >= start]
    ms = 1000.0 / freq
    span_s = (records[-1][0] - records[0][0]) / freq
    counts = defaultdict(int)
    for r in records:
        counts[r[1]] += 1
    print(f'[timeline] pid={pid} events={len(records)} written={written} span-s={span_s:.1f} qpc-hz={freq}')
    print('[timeline-counts] ' + ' '.join(f'{EVENTS.get(e, e)}={c}' for e, c in sorted(counts.items())))

    # Game frames.
    frames = [r[0] for r in records if r[1] == 26]
    intervals = [(b - a) * ms for a, b in zip(frames, frames[1:])]
    print(f'[timeline-frame] frame-interval-ms {stats(intervals)}')
    late = sum(1 for x in intervals if x > 16.667 * 1.5)
    print(f'[timeline-frame] frames={len(frames)} per-s={len(frames) / max(span_s, 1e-9):.3f} intervals>25ms={late}')

    # Chunks: a begin and its end on one thread are a pair; command lists pair by fence value.
    def paired(begin_ev, end_ev):
        open_at = {}
        out = []
        for qpc, ev, a, b, th in records:
            if ev == begin_ev:
                open_at[th] = qpc
            elif ev == end_ev and th in open_at:
                out.append((qpc - open_at.pop(th)) * ms)
        return out
    submit_call = paired(1, 2)
    record_time = paired(4, 6)
    print(f'[timeline-chain] submit-call ms {stats(submit_call)}')
    print(f'[timeline-chain] render-record ms {stats(record_time)}')
    submitted = {}
    for qpc, ev, a, b, th in records:
        if ev == 6:
            submitted.setdefault(b & ((1 << 63) - 1), qpc)
    gpu = defaultdict(dict)
    for qpc, ev, a, b, th in records:
        if ev == 7:
            gpu[a]['begin'] = qpc
        elif ev == 8:
            gpu[a]['end'] = qpc
    gpu_wait = [(g['begin'] - submitted[f]) * ms for f, g in gpu.items() if 'begin' in g and f in submitted]
    gpu_exec = [(g['end'] - g['begin']) * ms for g in gpu.values() if 'begin' in g and 'end' in g]
    print(f'[timeline-gpu] submitted-to-start ms {stats(gpu_wait)}')
    print(f'[timeline-gpu] all-command-lists exec-ms {stats(gpu_exec)}')

    # Presents.
    pb = [r for r in records if r[1] == 14]
    pe = {r[2]: r[0] for r in records if r[1] == 15}
    present_iv = [(b[0] - a[0]) * ms for a, b in zip(pb, pb[1:])]
    present_dur = [(pe[r[2]] - r[0]) * ms for r in pb if r[2] in pe]
    print(f'[timeline-present] presents={len(pb)} per-s={len(pb) / max(span_s, 1e-9):.3f} sync-interval={pb[0][3] if pb else "-"}')
    print(f'[timeline-present] call-interval-ms {stats(present_iv)}')
    print(f'[timeline-present] call-duration-ms {stats(present_dur)}')
    shown = [r for r in records if r[1] == 25]
    new_times = []
    last_number = None
    repeats = 0
    for r in shown:
        if r[3] != last_number:
            new_times.append(r[0])
            last_number = r[3]
        else:
            repeats += 1
    new_iv = [(b - a) * ms for a, b in zip(new_times, new_times[1:])]
    print(f'[timeline-present] new-pictures={len(new_times)} per-s={len(new_times) / max(span_s, 1e-9):.3f} repeated-pictures={repeats}')
    print(f'[timeline-present] new-picture-interval-ms {stats(new_iv)}')
    hist = defaultdict(int)
    for x in new_iv:
        hist[round(x / 16.667)] += 1
    print('[timeline-present] new-picture-interval-in-frame-periods ' + ' '.join(f'{k}:{v}' for k, v in sorted(hist.items())))
    copies = [r[0] for r in records if r[1] == 21]
    print(f'[timeline-picture] display-copies={len(copies)} per-s={len(copies) / max(span_s, 1e-9):.3f}')

    # DXGI frame statistics: per displayed present, refreshes on screen.
    ds = [r for r in records if r[1] == 16]
    seen = {}
    for qpc, ev, a, b, th in ds:
        pc, prc = a >> 32, a & 0xFFFFFFFF
        if pc and pc not in seen:
            seen[pc] = prc
    seq = sorted(seen.items())
    on_screen = defaultdict(int)
    dropped = 0
    for (pc1, r1), (pc2, r2) in zip(seq, seq[1:]):
        if pc2 - pc1 > 1:
            dropped += pc2 - pc1 - 1
        if pc2 > pc1:
            on_screen[r2 - r1] += 1
    vb = [r for r in records if r[1] == 24]
    if vb:
        periods = [r[2] * ms for r in vb]
        print(f'[timeline-display] refresh-period-ms {stats(periods)}')
    print(f'[timeline-display] stats-samples={len(ds)} distinct-present-counts={len(seq)} '
          f'unseen-presents={dropped}')
    print('[timeline-display] refreshes-between-displayed-presents ' +
          ' '.join(f'{k}:{v}' for k, v in sorted(on_screen.items())))

    # Waits.
    slot = paired(22, 23)
    print(f'[timeline-render] slot-fence-wait ms {stats(slot)} total-s={sum(slot) / 1000:.2f}')


if __name__ == '__main__':
    main()
