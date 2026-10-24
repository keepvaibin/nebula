"""Digest of a Nebula frame timeline (GALAXY_FRAME_TIMELINE=1, runtime/include/galaxy/frame_timeline.h).

Usage: python timeline_digest.py <timeline.bin> [--skip-seconds N]

Every timestamp in the file is QPC. The report covers:
  chain      per render chunk: submit -> render start -> game memory reads done ->
             command list submitted -> GPU start/end -> fence observed -> token
             consumed by the simulation thread
  vi         VI edge cadence and the delay from edge to the game-visible level
  present    Present call cadence, new-XFB cadence (copy serial changes), and
             DXGI frame statistics: how many display refreshes each present
             stayed on screen and how many presents never reached the screen
  sim        idle-loop waits, PE-token waits
  render     frame-slot (GPU fence) waits
"""
import argparse
import struct
import sys
from collections import defaultdict

EVENTS = {
    1: 'SubmitBegin', 2: 'SubmitEnd', 3: 'SubmitSource', 4: 'RenderBegin', 5: 'RenderReadsDone',
    6: 'RenderSubmitted', 7: 'GpuBegin', 8: 'GpuEnd', 9: 'FenceObserved', 10: 'PeDelivered',
    11: 'TokenConsumed', 12: 'ViEdge', 13: 'ViLatched', 14: 'PresentBegin', 15: 'PresentEnd',
    16: 'DisplayStats', 17: 'IdleWaitBegin', 18: 'IdleWaitEnd', 19: 'PeWaitBegin', 20: 'PeWaitEnd',
    21: 'XfbCopy', 22: 'SlotWaitBegin', 23: 'SlotWaitEnd', 24: 'VBlank', 25: 'XfbPresented',
    26: 'GuestFrame', 27: 'IdleWake',
}
SOURCES = {1: 'vi', 2: 'draw-done', 3: 'gx-sync', 4: 'other', 5: 'stream'}


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

    # Chains.
    submit_begin = {}
    pending_source = {}
    by_token = defaultdict(dict)
    fence_token = {}
    gpu = defaultdict(dict)
    last_submit_begin = {}
    for qpc, ev, a, b, th in records:
        name = EVENTS.get(ev)
        if name == 'SubmitSource':
            pending_source[th] = a
        elif name == 'SubmitBegin':
            last_submit_begin[th] = (qpc, a, pending_source.pop(th, 0))
        elif name == 'SubmitEnd':
            sb = last_submit_begin.pop(th, None)
            if sb is not None and a != 0:
                d = by_token[a]
                d['submit_begin'], d['bytes'], d['source'] = sb
                d['submit_end'] = qpc
        elif name == 'RenderBegin':
            by_token[a].setdefault('render_begin', qpc)
        elif name == 'RenderReadsDone':
            by_token[a].setdefault('reads_done', qpc)
        elif name == 'RenderSubmitted':
            fence = b & ((1 << 63) - 1)
            d = by_token[a]
            d.setdefault('render_submitted', qpc)
            d['fence'] = fence
            d['pe'] = 1 if b >> 63 else 0
            fence_token[fence] = a
        elif name == 'GpuBegin':
            gpu[a]['begin'] = qpc
        elif name == 'GpuEnd':
            gpu[a]['end'] = qpc
        elif name == 'FenceObserved':
            by_token[b].setdefault('fence_observed', qpc)
        elif name == 'TokenConsumed':
            by_token[a].setdefault('consumed', qpc)

    rows = defaultdict(list)
    for token, d in by_token.items():
        if 'submit_end' not in d or 'render_begin' not in d:
            continue
        src = SOURCES.get(d.get('source', 0), 'unknown')
        rows[('sim-submit-call', src)].append((d['submit_end'] - d['submit_begin']) * ms)
        rows[('queue', src)].append((d['render_begin'] - d['submit_end']) * ms)
        if 'reads_done' in d:
            rows[('reads', src)].append((d['reads_done'] - d['render_begin']) * ms)
        if 'render_submitted' in d:
            rows[('record', src)].append((d['render_submitted'] - d['render_begin']) * ms)
            g = gpu.get(d.get('fence'))
            if g and 'begin' in g and 'end' in g:
                rows[('gpu-wait', src)].append((g['begin'] - d['render_submitted']) * ms)
                rows[('gpu-exec', src)].append((g['end'] - g['begin']) * ms)
                if 'fence_observed' in d:
                    rows[('fence-observe-lag', src)].append((d['fence_observed'] - g['end']) * ms)
        if 'consumed' in d:
            rows[('submit-to-consumed', src)].append((d['consumed'] - d['submit_begin']) * ms)
            if d.get('pe') and 'render_submitted' in d:
                rows[('pe-frame:submitted-to-consumed', src)].append((d['consumed'] - d['render_submitted']) * ms)
        rows[('fifo-kb', src)].append(d.get('bytes', 0) / 1024.0)
    for key in sorted(rows):
        print(f'[timeline-chain] {key[0]:<32} source={key[1]:<9} ms {stats(rows[key])}' if key[0] != 'fifo-kb'
              else f'[timeline-chain] {key[0]:<32} source={key[1]:<9} kb {stats(rows[key])}')
    gpu_exec_all = [(g['end'] - g['begin']) * ms for g in gpu.values() if 'begin' in g and 'end' in g]
    print(f'[timeline-gpu] all-command-lists exec-ms {stats(gpu_exec_all)}')

    # VI delivery lateness: consume time minus the deadline (timeline ticks
    # at 60.75 MHz). The two clocks share no known offset, so report the
    # spread above the run's smallest value: a constant offset cancels.
    edge_records = [r for r in records if r[1] == 12]
    if edge_records:
        raw = [r[0] * ms - r[3] / 60750.0 for r in edge_records]
        floor = min(raw)
        print(f'[timeline-vi] delivery-lateness-above-min-ms {stats([x - floor for x in raw])}')
    vi_edges = [r[0] for r in records if r[1] == 12]
    vi_latched = [r[0] for r in records if r[1] == 13]
    intervals = [(b - a) * ms for a, b in zip(vi_edges, vi_edges[1:])]
    print(f'[timeline-vi] edge-interval-ms {stats(intervals)}')
    late = sum(1 for x in intervals if x > 16.667 * 1.5)
    print(f'[timeline-vi] edges={len(vi_edges)} per-s={len(vi_edges) / max(span_s, 1e-9):.3f} intervals>25ms={late}')
    li = 0
    delays = []
    for e in vi_edges:
        while li < len(vi_latched) and vi_latched[li] < e:
            li += 1
        if li < len(vi_latched):
            delays.append((vi_latched[li] - e) * ms)
    print(f'[timeline-vi] edge-to-latched-ms {stats(delays)}')

    # Presents.
    pb = [r for r in records if r[1] == 14]
    pe = {r[2]: r[0] for r in records if r[1] == 15}
    present_iv = [(b[0] - a[0]) * ms for a, b in zip(pb, pb[1:])]
    present_dur = [(pe[r[2]] - r[0]) * ms for r in pb if r[2] in pe]
    print(f'[timeline-present] presents={len(pb)} per-s={len(pb) / max(span_s, 1e-9):.3f} sync-interval={pb[0][3] if pb else "-"}')
    print(f'[timeline-present] call-interval-ms {stats(present_iv)}')
    print(f'[timeline-present] call-duration-ms {stats(present_dur)}')
    xp = [r for r in records if r[1] == 25]
    new_times = []
    last_serial = None
    repeats = 0
    for r in xp:
        if r[3] != last_serial:
            new_times.append(r[0])
            last_serial = r[3]
        else:
            repeats += 1
    new_iv = [(b - a) * ms for a, b in zip(new_times, new_times[1:])]
    print(f'[timeline-present] new-xfb={len(new_times)} per-s={len(new_times) / max(span_s, 1e-9):.3f} repeated-xfb={repeats}')
    print(f'[timeline-present] new-xfb-interval-ms {stats(new_iv)}')
    hist = defaultdict(int)
    for x in new_iv:
        hist[round(x / 16.667)] += 1
    print('[timeline-present] new-xfb-interval-in-vi-periods ' + ' '.join(f'{k}:{v}' for k, v in sorted(hist.items())))
    copies = [r[0] for r in records if r[1] == 21]
    print(f'[timeline-xfb] xfb-copies={len(copies)} per-s={len(copies) / max(span_s, 1e-9):.3f}')

    # DXGI frame statistics: per displayed present, refreshes on screen.
    ds = [r for r in records if r[1] == 16]
    shown = {}
    for qpc, ev, a, b, th in ds:
        pc, prc = a >> 32, a & 0xFFFFFFFF
        if pc and pc not in shown:
            shown[pc] = prc
    seq = sorted(shown.items())
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
    def paired(begin_ev, end_ev):
        open_at = {}
        out = []
        for qpc, ev, a, b, th in records:
            if ev == begin_ev:
                open_at[th] = qpc
            elif ev == end_ev and th in open_at:
                out.append((qpc - open_at.pop(th)) * ms)
        return out
    idle = paired(17, 18)
    pew = paired(19, 20)
    slot = paired(22, 23)

    # Draw-done windows: from the draw-done submission until its PE events
    # were delivered (finish raised for the game). The share of that window
    # the simulation thread spent idle-waiting says whether the game had
    # anything else to run while it waited for the render thread and GPU.
    idle_spans = []
    open_at = {}
    for qpc, ev, a, b, th in records:
        if ev == 17:
            open_at[th] = qpc
        elif ev == 18 and th in open_at:
            idle_spans.append((open_at.pop(th), qpc))
    idle_spans.sort()
    pe_delivered = sorted(r[0] for r in records if r[1] == 10 and r[2] == 1)
    windows = []
    for token, d in by_token.items():
        if d.get('source') != 2 or not d.get('pe') or 'submit_begin' not in d:
            continue
        s = d['submit_begin']
        import bisect
        i = bisect.bisect_left(pe_delivered, s)
        if i >= len(pe_delivered):
            continue
        e = pe_delivered[i]
        idle_in = 0
        j = bisect.bisect_left(idle_spans, (s, 0)) - 1
        j = max(j, 0)
        while j < len(idle_spans) and idle_spans[j][0] < e:
            lo, hi = max(idle_spans[j][0], s), min(idle_spans[j][1], e)
            if hi > lo:
                idle_in += hi - lo
            j += 1
        windows.append(((e - s) * ms, idle_in * ms))
    if windows:
        print(f'[timeline-drawdone] submit-to-finish-delivered ms {stats([w[0] for w in windows])}')
        print(f'[timeline-drawdone] sim-idle-inside-window ms {stats([w[1] for w in windows])}')
        tot = sum(w[0] for w in windows)
        print(f'[timeline-drawdone] idle-share={sum(w[1] for w in windows) / tot * 100 if tot else 0:.1f}% '
              f'of {tot / 1000:.2f} s over {len(windows)} frames')
    print(f'[timeline-sim] idle-wait ms {stats(idle)} total-s={sum(idle) / 1000:.2f} per-s={len(idle) / max(span_s, 1e-9):.1f}')
    woke = sum(1 for r in records if r[1] == 18 and r[2] == 1)
    print(f'[timeline-sim] idle-wait woken-by-publication={woke} of {len(idle)}')
    kinds = {0: 'vi', 1: 'ai-dma', 2: 'input', 3: 'decrementer', 4: 'dsp', 5: 'failure', 6: 'ios', 7: 'transfer', 100: 'host-pe-ios'}
    wakes = defaultdict(int)
    for r in records:
        if r[1] == 27:
            wakes[r[2]] += 1
    if wakes:
        print('[timeline-sim] idle-wake-sources per-s ' + ' '.join(
            f'{kinds.get(k, k)}={v / max(span_s, 1e-9):.1f}' for k, v in sorted(wakes.items())))
    print(f'[timeline-sim] pe-token-wait ms {stats(pew)} total-s={sum(pew) / 1000:.2f}')
    print(f'[timeline-render] slot-fence-wait ms {stats(slot)} total-s={sum(slot) / 1000:.2f}')


if __name__ == '__main__':
    main()
