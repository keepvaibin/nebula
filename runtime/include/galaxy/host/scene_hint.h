#pragma once

// Process-global, host-side-only, cosmetic scene hint: true while the guest
// is very likely showing (or about to show) the save-file-select screen.
//
// Derived purely by observing which files\LayoutData\*.arc archive was most
// recently DVD-read (see GuestAddressSpace::install_fst_scene_table /
// note_disc_read_for_scene_hint in native_host.cpp) -- never by inspecting
// or modifying guest memory, and never guest-visible. Exactly one
// GuestAddressSpace exists per process, so this is a free function backed
// by a single process-wide atomic rather than an instance method, letting
// the renderer (a separate translation unit that intentionally never
// includes the large native_host.h) query it without new coupling.
//
// Intended consumer: the native settings-overlay's file-select UI button
// (renderer_d3d12.cpp), so it only appears while the guest is actually on
// that screen instead of unconditionally.
namespace galaxy::host {

[[nodiscard]] bool file_select_scene_active() noexcept;

// Writer side, used only by native_host.cpp's DVDLowRead-observing scene
// detection (GuestAddressSpace::note_disc_read_for_scene_hint /
// install_fst_scene_table). Not for renderer_d3d12.cpp or other readers.
// 0 = unknown, 1 = FileSelect.arc most recently read, 2 = some other
// LayoutData archive most recently read.
void set_file_select_scene_hint(int value) noexcept;
// Same as set_file_select_scene_hint, but also returns the previous value
// (used to log only on an actual transition).
int exchange_file_select_scene_hint(int value) noexcept;

}  // namespace galaxy::host
