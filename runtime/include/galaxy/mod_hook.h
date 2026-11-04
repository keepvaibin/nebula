// SPDX-License-Identifier: GPL-3.0-only
// Replacing a function, where MinHook isn't.
#pragma once

namespace galaxy::mods {

// `target` now runs `replacement`; `*original` runs what `target` did. False: this function can't be hooked, and nothing has changed.
[[nodiscard]] bool hook_function(void* target, void* replacement, void** original);

}  // namespace galaxy::mods
