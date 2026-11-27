rate-60-80s: the first real tablet capture read by the parser of G10.2 (G10.2m, R52WA0FXE5J, Android 16, the test rate-60 of tests/pace/tablet.json, 20:42 on 11-26 with M-010 in).
The Perfetto trace itself is 68 MB (the ftrace part fills its 64 MB buffer in about 20 s) and stays in the run folder
D:\NebulaWork\build\Nebula-native\g102m\c_clean\vulkan\runs\pace\pace-rate-60\vulkan\frametimeline.pftrace.
Kept here: the three tables trace_processor_shell v59.1 gave for frametimeline.py's QUERIES (frames, expected, clocks; CSV, gzip), SurfaceFlinger's --latency reads and
--frametimeline -jank dump (gzip), capture.json, and frametimeline.json (the rows the parser gave, method latency, start step).
