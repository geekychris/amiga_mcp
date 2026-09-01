# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins

# Sourced helper: exports PPC_IMAGE for the walkero PPC/OS4 GCC image
# and ppc_image_refresh_if_stale() for weekly refresh.
#
# Docker's multi-arch manifest resolves `walkero/amigagccondocker:os4-gcc11`
# to the right platform (arm64 on Apple Silicon, amd64 on Intel/AMD) at
# pull time — no need to pin a per-arch tag. Callers can still override
# by setting PPC_IMAGE before sourcing this file (to pin an older
# arch-specific tag for reproducibility).
#
# Used by build-bridge-ppc.sh, build-example-ppc.sh, install-toolchains.sh.

: "${PPC_IMAGE:=walkero/amigagccondocker:os4-gcc11}"

# Pulls the image if the local copy is older than PPC_IMAGE_MAX_AGE_DAYS
# days (default 7). Silent no-op if the image isn't present at all —
# callers should invoke pull_if_missing first for the initial pull, then
# call this to opportunistically pick up walkero's newer releases.
ppc_image_refresh_if_stale() {
    local max_days="${PPC_IMAGE_MAX_AGE_DAYS:-7}"
    local created
    created=$(docker image inspect --format '{{.Created}}' "$PPC_IMAGE" \
              2>/dev/null) || return 0     # image not present, skip
    local created_epoch now_epoch age_days
    # macOS date wants -j -f; GNU date wants -d. Try BSD first, fall through.
    if ! created_epoch=$(date -j -f "%Y-%m-%dT%H:%M:%S" \
                              "${created%%.*}" +%s 2>/dev/null); then
        created_epoch=$(date -d "$created" +%s 2>/dev/null) || return 0
    fi
    now_epoch=$(date +%s)
    age_days=$(( (now_epoch - created_epoch) / 86400 ))
    if [ "$age_days" -ge "$max_days" ]; then
        echo "  [refresh] $PPC_IMAGE is ${age_days} days old — pulling if newer"
        docker pull "$PPC_IMAGE" 2>&1 | tail -1
    fi
}
