// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// NVIDIA DLSS and Intel XeSS, installed on the player's request from their makers' releases on GitHub: Nebula ships
// neither. Only the files the runtime loads are read out of each release zip (ranged requests), and each must match its
// SHA-256. One job at a time, on its own thread.

#include <cstdint>
#include <filesystem>
#include <string>

namespace galaxy::vendor_download {

enum class Package : unsigned { Dlss, Xess };
enum class State : unsigned { Idle, Running, Done, Failed, Canceled };

// What a download of the whole package reads, in megabytes.
[[nodiscard]] unsigned download_megabytes(Package package);
[[nodiscard]] bool installed(Package package, const std::filesystem::path& directory);

// Starts the download of what is missing into `directory`. False while another job runs.
bool start(Package package, const std::filesystem::path& directory);
// Asks the running job to stop; its partial files go.
void cancel();
struct Progress {
    State state = State::Idle;
    std::uint64_t done_bytes = 0u;
    std::uint64_t total_bytes = 0u;
};
[[nodiscard]] Progress progress();
// After Done, Failed or Canceled: back to Idle.
void acknowledge();

// The maker's license, read from the release zip (a few kilobytes), on its own thread. `text` is complete when ready.
struct License {
    bool ready = false;
    bool failed = false;
    std::string text;
};
void fetch_license(Package package);
[[nodiscard]] License license();

}  // namespace galaxy::vendor_download
