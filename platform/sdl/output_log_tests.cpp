// SPDX-License-Identifier: GPL-3.0-only
// keep_output_in_log (output_log.cpp, P-016, P-015): a start whose output nobody reads (/dev/null, as the Finder and
// some Linux menus give; a socket, as systemd's journal takes it) writes <data>/logs/game.log, the run before kept as
// game.previous.log; a start whose output a pipe takes (a shell's, a test's) keeps it there.
#include "sdl_host.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

bool g_ok = true;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        g_ok = false;
    }
}

std::string read_all(const fs::path& path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// A start in a child whose output goes to `fd`: it keeps its output, then says `text` on stdout and stderr.
void start_with_output(const fs::path& data, int fd, const char* text) {
    std::fflush(nullptr);
    const pid_t child = fork();
    if (child == 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        galaxy::sdl::keep_output_in_log(data);
        std::printf("%s out\n", text);
        std::fprintf(stderr, "%s err\n", text);
        std::fflush(nullptr);
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
}

}  // namespace

int main() {
    const fs::path data = fs::temp_directory_path() / ("nebula-output-log-" + std::to_string(getpid()));
    fs::remove_all(data);
    const fs::path log = data / "logs" / "game.log";
    const fs::path previous = data / "logs" / "game.previous.log";

    const int null = open("/dev/null", O_WRONLY);
    start_with_output(data, null, "first");
    expect(read_all(log) == "first out\nfirst err\n", "nobody reads the output (/dev/null): it is in logs/game.log");
    start_with_output(data, null, "second");
    expect(read_all(log) == "second out\nsecond err\n" && read_all(previous) == "first out\nfirst err\n",
           "the next start: game.log anew, the run before in game.previous.log");
    close(null);

    int fds[2];
    expect(pipe(fds) == 0, "a pipe");
    start_with_output(data, fds[1], "third");
    close(fds[1]);
    char said[64]{};
    const ssize_t got = read(fds[0], said, sizeof(said) - 1);
    close(fds[0]);
    expect(got > 0 && std::string(said) == "third out\nthird err\n" && read_all(log) == "second out\nsecond err\n",
           "a pipe reads the output (a shell, a test): it stays there and the log is left alone");

    int journal[2];
    expect(socketpair(AF_UNIX, SOCK_STREAM, 0, journal) == 0, "a socket");
    start_with_output(data, journal[1], "fourth");
    close(journal[0]);
    close(journal[1]);
    expect(read_all(log) == "fourth out\nfourth err\n", "a socket (the user journal of a menu start): the output is in game.log");

    fs::remove_all(data);
    std::printf("%s\n", g_ok ? "output log: every case holds" : "output log: FAILED");
    return g_ok ? 0 : 1;
}
