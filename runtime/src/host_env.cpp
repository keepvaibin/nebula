// Environment variables the host reads (host_env.h).
#include "galaxy/host_env.h"
#include "galaxy/windows_paths.h"

#include "galaxy/windows_api.h"
#include <Shlobj.h>
#include <shellapi.h>


namespace galaxy::host {

std::optional<std::filesystem::path> get_env_path(const wchar_t* name) {
    const std::wstring value = windows::environment_text(name);
    if (value.empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(value);
}

}  // namespace galaxy::host
