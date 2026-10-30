"""perf_summary.py RUN_LOG TAG: perf.sh's numbers from a run's log (GALAXY_TRACE_PRESENT_STATS, GALAXY_TRACE_THREAD_CPU):
the median GPU busy time per second (present-stats gpu-total-ms, two per second) and per frame, the frame rate, and
the render and game threads' CPU per second from 8 s after the tour reached its stop."""
import re
import sys

log = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
start = next((i for i, line in enumerate(log) if "tour: 1/" in line), len(log))
t0 = float(re.search(r"\[\s*([0-9.]+)\] tour: 1/", log[start]).group(1)) if start < len(log) else 1e9
gpu, fps, render, game = [], [], [], []
for line in log[start:]:
    if "[present-stats]" in line:
        m = re.search(r"gpu-total-ms=([0-9.]+)", line)
        f = re.search(r" fps=([0-9.]+)", line)
        if m and f:
            gpu.append(float(m.group(1)))
            fps.append(float(f.group(1)))
    if "] cpu: total" in line:
        t = float(re.match(r"\[\s*([0-9.]+)\]", line).group(1))
        if t < t0 + 8:
            continue
        r = re.search(r"Nebula GX Render ([0-9.]+)", line)
        g = re.search(r"Nebula Game ([0-9.]+)", line)
        if r:
            render.append(float(r.group(1)))
        if g:
            game.append(float(g.group(1)))
gpu, fps = gpu[4:], fps[4:]


def median(values):
    return sorted(values)[len(values) // 2] if values else float("nan")


busy = median(gpu) / 2.0
print("%-14s gpu busy %.0f ms/s = %.2f ms/frame at %.1f fps | render %.1f game %.1f ms/s | %d samples" %
      (sys.argv[2], busy, busy / max(median(fps), 1), median(fps), median(render), median(game), len(gpu)))
