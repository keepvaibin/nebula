// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/diagnostic_stick_lease.h"
#include <initializer_list>
#include <iostream>
#include <limits>

namespace {
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main() {
    using galaxy::input::DiagnosticStickLease;
    using galaxy::input::parse_diagnostic_stick_command;
    bool ok = true;
    const auto command = parse_diagnostic_stick_command("1:2000:-1:0.5\r\n");
    ok &= check(command.has_value() && command->sequence == 1u &&
                command->duration_ms == 2000u && command->x == -1.0f &&
                command->y == 0.5f, "valid finite command and CRLF");
    for (const auto text : {
            "", "0:100:0:1", "-1:100:0:1", "+1:100:0:1", "1:-1:0:1",
            "1:2001:0:1", "18446744073709551616:10:0:1", "1:1:nan:0",
            "1:1:inf:0", "1:1:1.01:0", "1:1:0:-1.01", "1:1:0:0:2",
            "1:1:0:0:-1", "1:1:0:0:1:1", "1:1:0:0:",
            "1:1:0:0\n2:1:0:1", " 1:1:0:0", "1:1:0:0 ", "1::0:0"}) {
        ok &= check(!parse_diagnostic_stick_command(text).has_value(),
                    "malformed/range/overflow input is rejected");
    }
    DiagnosticStickLease lease;
    const auto with_a = parse_diagnostic_stick_command("1:600:0:0:1\n");
    const auto without_a = parse_diagnostic_stick_command("1:600:0:0:0");
    ok &= check(with_a.has_value() && with_a->a && without_a.has_value() &&
                !without_a->a && !command->a, "explicit A and legacy neutral commands");
    DiagnosticStickLease button_lease;
    button_lease.observe("1:600:0:0:1", 100u);
    ok &= check(button_lease.active(699u)->a && !button_lease.active(700u).has_value(),
                "A expires without a follow-up file or operating-system release");
    button_lease.observe("1:600:0:0:1", 701u);
    ok &= check(!button_lease.active(701u).has_value(), "same A command cannot repeat a press");
    button_lease.observe("2:600:0:0:1", 800u);
    button_lease.observe("3:0:0:0", 810u);
    ok &= check(!button_lease.active(810u).has_value(), "neutral terminal command releases A");
    lease.observe("1:2000:0:1", 100u);
    ok &= check(lease.active(100u).has_value() &&
                lease.active(2099u).has_value() &&
                !lease.active(2100u).has_value(), "exact maximum lease boundary");
    lease.observe("1:2000:0:1", 2101u);
    ok &= check(!lease.active(2101u).has_value(), "same terminal sequence never renews");
    lease.observe("2:600:-1:0", 2200u);
    lease.observe("1:2000:0:1", 2300u);
    ok &= check(lease.active(2300u).has_value() &&
                lease.active(2300u)->x == -1.0f, "lower sequence cannot replace active command");
    lease.observe("2:2000:1:0", 2700u);
    ok &= check(lease.active(2700u).has_value() &&
                lease.active(2700u)->x == -1.0f &&
                !lease.active(2800u).has_value(), "changed same sequence cannot mutate or renew");
    lease.observe("3:100:0:1", 3000u);
    lease.cancel();
    lease.observe("3:100:0:1", 3010u);
    ok &= check(!lease.active(3010u).has_value(), "missing file cancellation cannot resurrect");
    lease.observe("4:100:0:1", 3100u);
    lease.observe("malformed", 3110u);
    lease.observe("4:100:0:1", 3120u);
    ok &= check(!lease.active(3120u).has_value(), "malformed file cancels without losing identity");
    lease.observe("5:2000:0:1", 3200u);
    lease.observe("6:0:0:0", 3210u);
    ok &= check(!lease.active(3210u).has_value(), "new zero-duration command cancels");
    lease.observe("7:2000:0:1", 3300u);
    ok &= check(!lease.active(3299u).has_value(), "backward observation cannot become active");
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    lease.observe("8:2000:0:1", maximum - 1000u);
    ok &= check(lease.active(maximum).has_value(), "elapsed subtraction avoids deadline overflow");
    if (ok) std::cout << "diagnostic stick lease tests passed\n";
    return ok ? 0 : 1;
}
