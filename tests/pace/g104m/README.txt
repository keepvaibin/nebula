G10.4m: the recorded results (tablet R52WA0FXE5J, SM-X810, Android 16, 11-27)

Every run was played by tools/gate/pace.py on the tablet under the tablet's lock (D:/NebulaWork/plan/tablet.lock) and the measure lock, with
the test app built from main 03c3155f7 (G10.4 landed): the tests tests/pace/rate-120-300 and rate-60-300 (the test rate-N lasting 5
minutes), the variant of the hint given with --diagnostics (hint-share-N, hint-per-step), the capture of frametimeline.py (SurfaceFlinger's
--latency every second at 120 Hz, every two seconds at 60, and Perfetto's trace). Beside each run, the CPU clusters' time_in_state
(/sys/devices/system/cpu/cpufreq/policy0, 3, 7), the GPU's /sys/class/kgsl/kgsl-3d0/devfreq/trans_stat and the platform's thermal headroom
(cmd thermalservice headroom 0 every 3 s; headroom 60 at the end) were read from the shell, each before and after the capture.

ab-runs.tsv        every run of the A/B and of the plants: the A/B row (events a minute), the jumps a minute, the late share and how many of the
                   late frames were shown on time, the pictures a second, the game step's and the walk's median, the heat headroom's slope and
                   last value, its forecast 60 s on, the average clocks of the three CPU clusters and of the GPU (MHz), whether the run counted
                   (a run whose latency record covers under 85% (120 Hz) or 78% (60 Hz) of the pictures the game counted, or whose display
                   stalled, was played again and is listed here as not used; the plants are used whatever their picture rate).
classified-120hz.tsv   the classification of G10.4m (b): for each of the five baseline runs at 120 Hz the counts of its late frames by verdict, then
                   the first 50 late frames of each run with their figures and verdict.
plants.txt         the rows of the planted faults against the bounds of the baseline's runs (median + range), the rows that see them in every
                   plant run, and the clean runs of the shares 90, 80 and 67 that count as clean for the false refusals.
The stored baseline is tests/pace/tablet-base.json (format 2: each run's rows and the bound of each row) from the five baseline runs ab120_b_1
to 5 and ab60_b_1 to 5. The run folders (the traces, 70 to 160 MB each) are in D:/NebulaWork/build/Nebula-native/g104m/runs.
