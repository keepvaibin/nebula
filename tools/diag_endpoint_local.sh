#!/usr/bin/env bash
# Writes runtime/include/galaxy/diag_endpoint_local.h (diag_endpoint.h) from NEBULA_DIAG_ENDPOINT and
# NEBULA_DIAG_PUBLIC_KEY, the release workflow's repository variables. Without both, nothing is written and the build
# sends no reports (a fork's build).
set -euo pipefail

endpoint="${NEBULA_DIAG_ENDPOINT:-}"
key="${NEBULA_DIAG_PUBLIC_KEY:-}"
out="$(dirname "$0")/../runtime/include/galaxy/diag_endpoint_local.h"

if [[ -z "$endpoint" && -z "$key" ]]; then
    echo "No diagnostics address: this build sends no reports."
    exit 0
fi
if [[ ! "$endpoint" =~ ^https://[A-Za-z0-9.-]+(/[A-Za-z0-9._/-]*)?$ ]]; then
    echo "NEBULA_DIAG_ENDPOINT is not an https address." >&2
    exit 1
fi
if [[ ! "$key" =~ ^[0-9a-f]{64}$ ]]; then
    echo "NEBULA_DIAG_PUBLIC_KEY is not 64 lowercase hex digits." >&2
    exit 1
fi

cat > "$out" <<EOF
// The developer's diagnostics endpoint and public key (diag_endpoint.h). Git ignores this file.
#pragma once

namespace galaxy::diag {

inline constexpr char kEndpoint[] = "$endpoint";
inline constexpr char kPublicKey[] = "$key";

}  // namespace galaxy::diag
EOF
echo "Diagnostics address written."
