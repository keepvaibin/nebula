// A path as text, UTF-8 on every host: path.string() is the system's code page on Windows and throws on a name it cannot hold.
#pragma once

#include <filesystem>
#include <string>

namespace galaxy {

[[nodiscard]] inline std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

[[nodiscard]] inline std::filesystem::path text_path(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

}  // namespace galaxy
