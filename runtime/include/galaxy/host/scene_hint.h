#pragma once

// Host-only cosmetic hint: true while the guest is likely on the save-file
// select screen. Derived from which files\LayoutData\*.arc was most recently
// DVD-read (native_host.cpp); guest memory is never read or written. A free
// function over one process-wide atomic so renderer_d3d12.cpp can query it
// without including native_host.h. Used to show the file-select UI button.
namespace galaxy::host {

[[nodiscard]] bool file_select_scene_active() noexcept;

// Writer side, for native_host.cpp's DVDLowRead scene detection only.
// 0 = unknown, 1 = FileSelect.arc most recently read, 2 = another
// LayoutData archive most recently read.
void set_file_select_scene_hint(int value) noexcept;
// Like set_file_select_scene_hint, but returns the previous value.
int exchange_file_select_scene_hint(int value) noexcept;

}  // namespace galaxy::host
