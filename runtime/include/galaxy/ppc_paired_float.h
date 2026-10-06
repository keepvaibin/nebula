#pragma once

#include "galaxy/ppc_float.h"

namespace galaxy {

// Captured binary32 operands; lane0 selects FPRF while both lanes contribute
// exceptions and FI/FR. An enabled fault precedes either destination or CR1.
void ppc_commit_paired_ternary_result(
    PpcContext* context,
    std::uint32_t target,
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand_ps0,
    std::uint32_t multiplicand_ps1,
    std::uint32_t multiplier_ps0,
    std::uint32_t multiplier_ps1,
    std::uint32_t addend_ps0,
    std::uint32_t addend_ps1,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);

}  // namespace galaxy
