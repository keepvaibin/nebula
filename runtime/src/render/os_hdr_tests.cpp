// SPDX-License-Identifier: GPL-3.0-only
// os_hdr.h: what the game turns on and off, what it leaves to the player, and the list file.
#include "galaxy/render/os_hdr.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace plan = galaxy::render::os_hdr::plan;

void check(bool value, const char* text) {
    if (!value) throw std::runtime_error(text);
}

const std::string kA = "display-a";
const std::string kB = "display-b";

plan::Target target(const std::string& path, bool on, bool supported = true, bool blocked = false) {
    return {path, supported, on, blocked};
}

using Paths = std::vector<std::string>;
}  // namespace

int main() {
    try {
        // Wished on, off: turned on. The caller records it as owned.
        {
            plan::Memory memory;
            const plan::Steps steps = plan::next({target(kA, false), target(kB, false)}, {kA}, true, false, memory);
            check(steps.on == Paths{kA} && steps.off.empty() && !steps.refused, "a wished display with HDR off is turned on");
        }
        // Already on (the player's own): nothing turned on, nothing owned, nothing turned off when the wish ends.
        {
            plan::Memory memory;
            plan::Steps steps = plan::next({target(kA, true)}, {kA}, true, false, memory);
            check(steps.on.empty() && memory.owned.empty() && memory.seen_on == Paths{kA}, "a display on already is the player's");
            steps = plan::next({target(kA, true)}, {kA}, false, true, memory);
            check(steps.off.empty(), "the player's HDR stays on when the game's HDR goes off");
        }
        // Not supported, or blocked by a policy: not turned on; blocked is refused.
        {
            plan::Memory memory;
            plan::Steps steps = plan::next({target(kA, false, false)}, {kA}, true, false, memory);
            check(steps.on.empty() && !steps.refused, "an SDR display is not turned on");
            steps = plan::next({target(kA, false, true, true)}, {kA}, true, false, memory);
            check(steps.on.empty() && steps.refused, "a blocked display is refused");
        }
        // Owned and no longer wished: off only once the change is due.
        {
            plan::Memory memory;
            memory.owned = {kA};
            plan::Steps steps = plan::next({target(kA, true)}, {kA}, false, false, memory);
            check(steps.off.empty() && memory.owned == Paths{kA}, "an owned display waits for the change to hold");
            steps = plan::next({target(kA, true)}, {kA}, false, true, memory);
            check(steps.off == Paths{kA}, "an owned display goes off when the game's HDR goes off");
            steps = plan::next({target(kA, true)}, {kA}, true, true, memory);
            check(steps.off.empty() && steps.on.empty(), "an owned display still wished stays on");
        }
        // The window moved: the old display off (when due), the new one on at once.
        {
            plan::Memory memory;
            memory.owned = {kA};
            const plan::Steps steps = plan::next({target(kA, true), target(kB, false)}, {kB}, true, true, memory);
            check(steps.off == Paths{kA} && steps.on == Paths{kB}, "a move turns the old display off and the new one on");
        }
        // Turned off outside the game while owned and wished: dropped, declined, not turned on again.
        {
            plan::Memory memory;
            memory.owned = {kA};
            memory.seen_on = {kA};
            plan::Steps steps = plan::next({target(kA, false)}, {kA}, true, false, memory);
            check(memory.owned.empty() && memory.declined == Paths{kA} && steps.on.empty() && steps.refused,
                  "an owned display turned off outside the game is left off");
            steps = plan::next({target(kA, false)}, {kA}, true, false, memory);
            check(steps.on.empty() && steps.refused, "and stays left off while the wish holds");
            memory.declined.clear();  // the wish changed (want clears it)
            steps = plan::next({target(kA, false)}, {kA}, true, false, memory);
            check(steps.on == Paths{kA}, "a new wish turns it on again");
        }
        // The player's own HDR turned off outside the game while wished: left off.
        {
            plan::Memory memory;
            (void)plan::next({target(kA, true)}, {kA}, true, false, memory);
            const plan::Steps steps = plan::next({target(kA, false)}, {kA}, true, false, memory);
            check(steps.on.empty() && steps.refused && memory.declined == Paths{kA}, "the player's own HDR turned off is left off");
        }
        // An owned display that is gone (unplugged) stays owned and is not switched.
        {
            plan::Memory memory;
            memory.owned = {kA};
            const plan::Steps steps = plan::next({target(kB, false)}, {kB}, false, true, memory);
            check(steps.off.empty() && memory.owned == Paths{kA}, "a display that is gone stays listed");
        }
        // The list file.
        {
            const plan::List list{4242u, {kA, kB}};
            const plan::List read = plan::read_list(plan::list_text(list));
            check(read.process == 4242u && read.paths == list.paths, "the list reads back");
            const plan::List crlf = plan::read_list("process 7\r\n  display-a \r\n\r\ndisplay-a\r\ndisplay-b");
            check(crlf.process == 7u && crlf.paths == (Paths{kA, kB}), "CRLF, spaces, blank and repeated lines");
            check(plan::read_list("").paths.empty() && plan::read_list("").process == 0u, "an empty list");
        }
        std::cout << "OS HDR: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OS HDR: " << error.what() << '\n';
        return 1;
    }
}
