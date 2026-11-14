// SPDX-License-Identifier: GPL-3.0-only
// One program of the Windows x64 host running another in its place (host_entry_win32.cpp): the AVX2 host's entry on
// a CPU without x86-64-v3 (<name>-x86-64-v2.exe), and the Direct3D 12 host's hand-off to Direct3D 11 (host_main.cpp,
// NebulaNativeD11). Runs `path` with this process's command line, startup information, inherited handles, environment
// and working folder in a job that ends it when this process ends, waits for it and stores its exit code: a caller
// that waits on this process sees one lifetime and one code. False when it did not start (GetLastError says why).
// Only kernel32: callable before the C runtime starts.
#pragma once

#if defined(_WIN32) && defined(_M_X64)
extern "C" int nebula_run_sibling(const wchar_t* path, unsigned long* exit_code);
#endif
