// Discord Rich Presence: the IPC frames and JSON the worker sends (galaxy/discord_presence.h).
#include "galaxy/discord_presence.h"

#include <cstdio>
#include <string>

namespace {
int s_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}
}  // namespace

int main() {
    using namespace galaxy::discord;
    // Frame: opcode and length little-endian, then the JSON.
    const std::string f = frame(1u, "{}");
    expect(f.size() == 10u, "frame size");
    expect(f[0] == 1 && f[1] == 0 && f[2] == 0 && f[3] == 0, "frame opcode");
    expect(f[4] == 2 && f[5] == 0 && f[6] == 0 && f[7] == 0, "frame length");
    expect(f.substr(8) == "{}", "frame body");

    expect(handshake_json("123") == "{\"v\":1,\"client_id\":\"123\"}", "handshake");

    // Escaping: quotes, backslashes, control characters; UTF-8 (the game's accented names) passes through.
    expect(json_string("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"", "escape");
    expect(json_string("\x01") == "\"\\u0001\"", "control escape");
    expect(json_string("Galaxie \xC3\xA9toil\xC3\xA9" "e") == "\"Galaxie \xC3\xA9toil\xC3\xA9" "e\"", "utf-8 kept");

    const Activity activity{"Good Egg Galaxy", "Dino Piranha"};
    const std::string set = set_activity_json(42u, activity, 1000, 7u, false);
    expect(set.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos, "cmd");
    expect(set.find("\"pid\":42") != std::string::npos, "pid");
    expect(set.find("\"details\":\"Good Egg Galaxy\"") != std::string::npos, "details");
    expect(set.find("\"state\":\"Dino Piranha\"") != std::string::npos, "state");
    expect(set.find("\"start\":1000") != std::string::npos, "timestamp");
    expect(set.find("\"nonce\":\"7\"") != std::string::npos, "nonce");

    // An empty line is left out; clearing sends a null activity.
    const std::string menu = set_activity_json(1u, Activity{"Choosing a file", ""}, 5, 1u, false);
    expect(menu.find("\"state\"") == std::string::npos, "empty state omitted");
    expect(set_activity_json(1u, {}, 0, 2u, true).find("\"activity\":null") != std::string::npos, "clear");

    if (s_failures == 0) std::printf("discord presence: all passed\n");
    return s_failures == 0 ? 0 : 1;
}
