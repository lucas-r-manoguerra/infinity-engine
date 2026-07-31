#!/usr/bin/env bash
#
# Infinity Engine - third-party license audit (F0.9 / ADR-068).
#
# Verifies that:
#   1. third_party/THIRD_PARTY.md exists.
#   2. Every vendored directory under third_party/ is declared in the table.
#   3. Every table row carries exactly six cells: name, version, license,
#      provenance, SHA-256 and why.
#
# Runs in CI (license-audit job); any violation fails the pipeline.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

MANIFEST="third_party/THIRD_PARTY.md"
fail=0

if [[ ! -f "${MANIFEST}" ]]; then
    echo "ERROR: ${MANIFEST} missing" >&2
    exit 1
fi

# 1+2. Every vendored directory must be declared.
for dir in third_party/*/; do
    [[ -d "${dir}" ]] || continue
    name="$(basename "${dir}")"
    if ! grep -qE "^\|\s*${name}\s*\|" "${MANIFEST}"; then
        echo "ERROR: third_party/${name}/ is not declared in ${MANIFEST}" >&2
        fail=1
    fi
done

# 3. Well-formed rows. Skip the header and separator (first two pipe lines).
pipe_count=0
row_count=0
while IFS= read -r line; do
    [[ "${line}" =~ ^\| ]] || continue
    pipe_count=$((pipe_count + 1))
    if [[ "${pipe_count}" -le 2 ]]; then
        continue
    fi
    row_count=$((row_count + 1))
    cells=$(awk -F'|' '{
        n = 0;
        for (i = 2; i <= NF; ++i) {
            gsub(/^[ \t]+|[ \t]+$/, "", $i);
            if ($i != "") n++;
        }
        print n;
    }' <<<"${line}")
    if [[ "${cells}" -ne 6 ]]; then
        echo "ERROR: malformed row (${cells}/6 cells): ${line}" >&2
        fail=1
    fi
done < "${MANIFEST}"

if [[ "${row_count}" -eq 0 ]]; then
    echo "ERROR: no dependencies declared in ${MANIFEST}" >&2
    fail=1
fi

if [[ "${fail}" -ne 0 ]]; then
    echo "license audit FAILED" >&2
    exit 1
fi

echo "license audit OK: ${row_count} dependency(ies) declared"
