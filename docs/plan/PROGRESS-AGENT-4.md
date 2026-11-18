# Progress: Agent 4 (mods: the LumaLink repository)

One dated entry per package step. Newest last. Ledger rows are in ledger.tsv (agent 4); new rows are N-4xxx.

Packages, in order (plan file 05, section 5.1): M10 camera, look and first person; M7 the compiler; M8 LumaLink;
M12 platforms for mods; M14 the six reference mods; M11 documentation, from the build; Agent 4's part of M13.

## 2026-11-17 Start

- Work trees: Nebula-A4 on link/work from engine/n1011 bbeeab141 (main was at 7b541c50b); lumalink-a4 on link/work
  from LumaLink mods/m2 9cf2ebe.
- 70 rows of M10, M7, M8, M12 and M11 carry agent 4; 61 of them open.

## 2026-11-17 M8 first sweep (LumaLink 01aa60e)

- Fixed with tests: M-023, M-141, M-142, M-147, M-148, M-149, M-165, N-2002, N-2006. Each test fails on the old
  build except the two that test a new part (files::Part, running.rs): M-149's comma path was run on the old link
  flag and failed (ld.lld: cannot find linker script .../sd).
- LumaLink's workspace tests: 29 test programs, all pass. The window's tests take under a second now (N-2002).
- Docs: compiled-form.md (arm64 Linux atomics, --script), packing-and-sharing-a-mod.md, and every
  lumalink-commands page written again by tools/modding/lumalink_commands.py from this LumaLink.
- M8 left: none open in this package but what M11 owns of its docs.
