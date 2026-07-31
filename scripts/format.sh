#!/usr/bin/env bash
#
# Infinity Engine - code formatting and linting (F0.6).
#
# Usage:
#   ./scripts/format.sh            apply clang-format in place (default)
#   ./scripts/format.sh --fix      same as default
#   ./scripts/format.sh --check    CI mode: clang-format --dry-run --Werror
#                                  plus clang-tidy over the compile database
#
# clang-format-20 is preferred, clang-format is the fallback. clang-tidy
# requires a configured preset build (build/<preset>/compile_commands.json);
# the script skips it with a warning when that is unavailable.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

MODE="${1:---fix}"
case "${MODE}" in
    --fix|--check) ;;
    *) echo "usage: $0 [--fix|--check]" >&2; exit 2 ;;
esac

resolve() {
    local tool="$1"
    local candidate
    for candidate in "${tool}-20" "${tool}"; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            echo "${candidate}"
            return 0
        fi
    done
    return 1
}

CLANG_FORMAT="$(resolve clang-format || true)"
RUN_CLANG_TIDY="$(resolve run-clang-tidy || true)"

if [[ -z "${CLANG_FORMAT}" ]]; then
    echo "error: clang-format-20 or clang-format not found" >&2
    exit 1
fi

# Tracked + untracked (non-ignored) C++ sources. Vendored code and generated
# tooling are never touched.
mapfile -t SOURCES < <(
    git ls-files -co --exclude-standard \
        -- '*.cpp' '*.cc' '*.cxx' '*.h' '*.hpp' '*.hxx' \
        | grep -vE '^(build/|third_party/|\.devcontainer/|\.github/)' || true
)

if [[ "${#SOURCES[@]}" -eq 0 ]]; then
    echo "no C++ sources to format"
    exit 0
fi

if [[ "${MODE}" == "--check" ]]; then
    if ! "${CLANG_FORMAT}" --dry-run --Werror "${SOURCES[@]}"; then
        echo "error: clang-format found diffs (run ./scripts/format.sh)" >&2
        exit 1
    fi
else
    "${CLANG_FORMAT}" -i "${SOURCES[@]}"
    echo "formatted ${#SOURCES[@]} files"
fi

# --- clang-tidy -------------------------------------------------------------

if [[ -z "${RUN_CLANG_TIDY}" ]]; then
    echo "warning: run-clang-tidy not found; skipping clang-tidy" >&2
    exit 0
fi

BUILD_DIR=""
for candidate in build/ci build/debug build/release; do
    if [[ -f "${candidate}/compile_commands.json" ]]; then
        BUILD_DIR="${candidate}"
        break
    fi
done

if [[ -z "${BUILD_DIR}" ]]; then
    echo "warning: no build/<preset>/compile_commands.json found; skipping clang-tidy" >&2
    exit 0
fi

if [[ "${MODE}" == "--check" ]]; then
    "${RUN_CLANG_TIDY}" -p "${BUILD_DIR}" "${SOURCES[@]}"
else
    "${RUN_CLANG_TIDY}" -p "${BUILD_DIR}" -fix "${SOURCES[@]}"
fi
