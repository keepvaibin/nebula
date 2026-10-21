#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Discord Rich Presence over the Discord desktop app's local IPC (the named pipe discord-ipc-N): what the player is
// doing, in the game's own words (its galaxy and star names, in the played language). A worker thread does all of it;
// while Discord is not running it retries every 15 s and the game never waits on it.
namespace galaxy::discord {

struct Activity {
    std::string details;  // first line: the galaxy, or "Choosing a file"
    std::string state;    // second line: the star, or the Power Star count
};

// Once, at start. An empty client id (no Discord application) keeps presence off.
void start(std::string client_id);
// The GENERAL page's DISCORD STATUS, read by the worker each second (off clears the status and closes the pipe).
void set_enabled_provider(bool (*provider)());
// What the player is doing now; sent when it changes.
void set_activity(const Activity& activity);

// The protocol's pieces, for tests: a frame (opcode, length, both little-endian, then the JSON), the handshake and
// the SET_ACTIVITY command (an empty activity clears the status).
std::string frame(std::uint32_t opcode, std::string_view json);
std::string handshake_json(std::string_view client_id);
std::string set_activity_json(std::uint32_t pid, const Activity& activity, std::int64_t start_seconds,
                              std::uint32_t nonce, bool clear);
std::string json_string(std::string_view text);

}  // namespace galaxy::discord
