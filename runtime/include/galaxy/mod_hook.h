// SPDX-License-Identifier: GPL-3.0-only
// Function hooking on platforms without MinHook (Linux, macOS, Android).
#pragma once

namespace galaxy::mods {

// Makes `target` run `replacement` and sets `*original` to a function that runs the old code. Returns false, changing
// nothing, when the function can't be hooked.
[[nodiscard]] bool hook_function(void* target, void* replacement, void** original);

}  // namespace galaxy::mods
