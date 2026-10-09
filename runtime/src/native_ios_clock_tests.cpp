#include "galaxy/native_ios_anomaly.h"

#include <cstdio>

int main() {
    using namespace galaxy::host;
    unsigned wall_calls = 0u, counter_calls = 0u;
    auto wall = [&]() noexcept { return 100u + 10u * ++wall_calls; };
    auto counters = [&](NativeIosClockSample& sample) noexcept {
        ++counter_calls;
        sample.cpu_100ns = 123u;
        sample.cycles = 456u;
        sample.cpu_valid = sample.cycles_valid = true;
    };
    const auto ordinary = sample_native_ios_request_clock(false, wall, counters);
    if (wall_calls != 1u || counter_calls != 0u || ordinary.before_ns != 110u ||
        ordinary.after_ns != 110u || ordinary.cpu_valid || ordinary.cycles_valid ||
        ordinary.cpu_100ns != 0u || ordinary.cycles != 0u) return 1;
    const auto detailed = sample_native_ios_request_clock(true, wall, counters);
    if (wall_calls != 3u || counter_calls != 1u || detailed.before_ns != 120u ||
        detailed.after_ns != 130u || !detailed.cpu_valid || !detailed.cycles_valid ||
        detailed.cpu_100ns != 123u || detailed.cycles != 456u) return 2;
    const auto unavailable = sample_native_ios_request_clock(true, wall,
        [](NativeIosClockSample&) noexcept {});
    if (wall_calls != 5u || unavailable.before_ns != 140u || unavailable.after_ns != 150u ||
        unavailable.cpu_valid || unavailable.cycles_valid) return 3;
    // Exact ordinary endpoints still retain the 5 ms wall anomaly without
    // fabricated counter validity. Detailed brackets retain their old bounds.
    NativeIosAnomalyLedger ledger;
    NativeIosAnomalyRecord record{};
    record.entry.before_ns = record.entry.after_ns = 1u;
    record.exit.before_ns = record.exit.after_ns = 5'000'001u;
    ledger.finish(record);
    if (ledger.records().size() != 1u || ledger.slow() != 1u ||
        record.minimum_elapsed_ns() != 5'000'000u ||
        record.maximum_elapsed_ns() != 5'000'000u) return 4;
    record.entry.after_ns = 11u;
    record.exit.after_ns = 5'000'011u;
    if (record.minimum_elapsed_ns() != 4'999'990u ||
        record.maximum_elapsed_ns() != 5'000'010u) return 5;
    std::puts("IOS clock policy: ordinary, detailed, failed-counter and wall-ledger checks passed");
    return 0;
}
