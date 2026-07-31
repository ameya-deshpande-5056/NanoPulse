#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-run"
BUILD_ONLY=0
CLEAN=0

usage() {
    printf '%s\n' \
        "Usage: ./run.sh [--clean] [--build-only] [--help]" \
        "  --clean       Remove the latest-code build directory first" \
        "  --build-only  Build without launching NanoPulse" \
        "  --help        Show this help"
}

while (($#)); do
    case "$1" in
        --clean) CLEAN=1 ;;
        --build-only) BUILD_ONLY=1 ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Error: unknown option %s\n' "$1" >&2
            exit 2
            ;;
    esac
    shift
done

[[ "$(uname -s)" == "Linux" ]] || {
    printf 'Error: run.sh supports Linux only.\n' >&2
    exit 1
}
command -v cmake >/dev/null 2>&1 || {
    printf 'Error: CMake is required.\n' >&2
    exit 1
}

if (( CLEAN )); then
    case "${BUILD_DIR}" in
        "${PROJECT_ROOT}/build-run") rm -rf -- "${BUILD_DIR}" ;;
        *)
            printf 'Error: unsafe build directory %s\n' "${BUILD_DIR}" >&2
            exit 1
            ;;
    esac
fi

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi
PREFIX_ARGS=()
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    PREFIX_ARGS=(-DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}")
fi

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" "${GENERATOR_ARGS[@]}" \
    "${PREFIX_ARGS[@]}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --config Release --parallel

if (( BUILD_ONLY )); then
    exit 0
fi
[[ -x "${BUILD_DIR}/NanoPulse" ]] || {
    printf 'Error: build completed without producing NanoPulse.\n' >&2
    exit 1
}
exec "${BUILD_DIR}/NanoPulse"
