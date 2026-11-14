#pragma once

#include "Game/NameObj/NameObjFactory.hpp"

// References (docs/modding/language/references.md): the classes the after-start check found missing (luma_refs.h), and
// the placeholder object that stands at each of their placements.
namespace luma {
    namespace refs {
        // The placeholder object's creator when `pClassName` is a class the after-start check found missing (case as
        // the factory compares); null otherwise, so every other unknown name stays as the game has it. Game thread.
        CreatorFuncPtr placeholderFor(const char* pClassName);
        // How many classes were found missing; 0 before the check, and with no mod.
        u32 missingCount();
    }  // namespace refs
}  // namespace luma
