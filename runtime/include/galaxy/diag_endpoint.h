// SPDX-License-Identifier: GPL-3.0-only
// Where diagnostics reports go and the key they are sealed to (diag_report.h, tools/telemetry/worker/README.md).
// Empty in the sources: such a build refuses --send-report and every app hides the upload choice, so a fork never
// sends to Nebula's developer by accident. The developer's builds read both from diag_endpoint_local.h beside this
// file, which git ignores: the Worker's https URL (kEndpoint) and the public key `diag_reports.py keygen` printed
// (kPublicKey, 64 hex digits). Never a private key there.
#pragma once

#if __has_include("galaxy/diag_endpoint_local.h")
#include "galaxy/diag_endpoint_local.h"
#else
namespace galaxy::diag {

inline constexpr char kEndpoint[] = "";
inline constexpr char kPublicKey[] = "";

}  // namespace galaxy::diag
#endif
