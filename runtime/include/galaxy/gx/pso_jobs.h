// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Compute pipelines compiled on worker threads. The device's pipeline creation is free-threaded, and on a cold driver
// cache (the first start of every build) each ray tracing pipeline takes the driver seconds to compile: one after
// another, the ray tracer's held the first frame for half a minute.

#include <d3d12.h>
#include <wrl/client.h>

#include "galaxy/gx/rt_prepare.h"

#include <cstdint>
#include <future>
#include <vector>

namespace galaxy::gx {

class PsoJobs {
public:
    PsoJobs() = default;
    PsoJobs(const PsoJobs&) = delete;
    PsoJobs& operator=(const PsoJobs&) = delete;
    ~PsoJobs() { wait(); }

    // Starts `out`'s compile (`root` and `code` must outlive wait()); `group` (0-63) names it in wait()'s result. It
    // loads from the ray tracing pipeline library when the library has it (rt_prepare.h).
    void compute(ID3D12Device* device, ID3D12RootSignature* root, const void* code, std::size_t bytes,
                 Microsoft::WRL::ComPtr<ID3D12PipelineState>& out, unsigned group = 0u) {
        jobs_.push_back({std::async(std::launch::async,
                                    [device, root, code, bytes, &out] {
                                        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
                                        desc.pRootSignature = root;
                                        desc.CS = {code, bytes};
                                        return rt_prepare::compute(device, desc, out);
                                    }),
                         group});
    }

    // Joins every compile started; the groups with a failure (bit per group).
    std::uint64_t wait() {
        std::uint64_t failed = 0u;
        for (Job& job : jobs_) {
            if (job.done.valid() && !job.done.get()) {
                failed |= std::uint64_t{1} << (job.group & 63u);
            }
        }
        jobs_.clear();
        return failed;
    }

private:
    struct Job {
        std::future<bool> done;
        unsigned group = 0u;
    };
    std::vector<Job> jobs_;
};

}  // namespace galaxy::gx
