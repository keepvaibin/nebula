// SPDX-License-Identifier: GPL-3.0-only
// Where diagnostics reports go and the key they are sealed to (diag_report.h, tools/telemetry/README.md). Both empty
// in the sources: such a build refuses --send-report and every app hides the upload choice, so a fork never sends to
// Nebula's developer by accident. The developer's release build sets both: the Worker's https URL and the public key
// that `NebulaNative --report-keygen` printed (64 hex digits). Never a private key here.
#pragma once

namespace galaxy::diag {

inline constexpr char kEndpoint[] = "";
inline constexpr char kPublicKey[] = "";

}  // namespace galaxy::diag
