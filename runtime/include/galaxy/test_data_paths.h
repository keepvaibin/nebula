// Private writable paths required by automated host runs.
#pragma once

#include <cwctype>
#include <filesystem>

namespace galaxy::host {

[[nodiscard]] inline bool path_is_within(const std::filesystem::path& path,
                                         const std::filesystem::path& root) {
    if (!path.is_absolute() || !root.is_absolute()) return false;
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) return false;
    const auto canonical_path = std::filesystem::weakly_canonical(path, error);
    if (error) return false;
    auto p = canonical_path.begin();
    for (const auto& component : canonical_root) {
        if (p == canonical_path.end()) return false;
#if defined(_WIN32)
        auto a = component.native(), b = p->native();
        for (auto& ch : a) ch = static_cast<wchar_t>(std::towlower(ch));
        for (auto& ch : b) ch = static_cast<wchar_t>(std::towlower(ch));
        if (a != b) return false;
#else
        if (component != *p) return false;
#endif
        ++p;
    }
    return true;
}

}  // namespace galaxy::host
