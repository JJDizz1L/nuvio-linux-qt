#!/usr/bin/env bash
# Detached-sign release artifacts (opt-in). Usage:
#   sign-artifacts.sh <artifact> [<artifact>...]
# Signs with NUVIO_SIGN_KEY (a key id, fingerprint, or --local-user value)
# when set; without it every artifact is skipped with a notice (keyless CI
# stays green, releases simply ship unsigned).
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <artifact> [<artifact>...]" >&2
    exit 2
fi

signed=0
skipped=0
for artifact in "$@"; do
    if [[ ! -f "$artifact" ]]; then
        echo "Artifact not found: $artifact" >&2
        exit 1
    fi
    if [[ -z "${NUVIO_SIGN_KEY:-}" ]]; then
        echo "No NUVIO_SIGN_KEY: leaving unsigned: $artifact"
        skipped=$((skipped + 1))
        continue
    fi
    if ! command -v gpg >/dev/null 2>&1; then
        echo "gpg is required for signing." >&2
        exit 1
    fi
    gpg --batch --yes --armor --detach-sign \
        --local-user "$NUVIO_SIGN_KEY" \
        --output "$artifact.asc" "$artifact"
    echo "Signed: $artifact.asc"
    signed=$((signed + 1))
done
printf 'sign-artifacts: %d signed, %d unsigned\n' "$signed" "$skipped"
