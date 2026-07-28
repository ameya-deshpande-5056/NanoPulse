#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
STATE_DIR="${XDG_STATE_HOME:-${HOME}/.local/state}/NanoPulse"
METADATA_FILE="${STATE_DIR}/install.conf"
DESKTOP_DEFAULT="${HOME}/.local/share/applications/com.example.NanoPulse.desktop"
ICON_DEFAULT="${HOME}/.local/share/icons/hicolor/scalable/apps/com.example.NanoPulse.svg"
PURGE=0

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    printf 'Usage: ./uninstall.sh [--purge]\n  --purge  Remove user configuration without prompting\n'
    exit 0
elif [[ "${1:-}" == "--purge" ]]; then
    PURGE=1
elif (($#)); then
    printf 'Error: unknown option %s\n' "$1" >&2
    exit 2
fi

[[ "$(uname -s)" == "Linux" ]] || {
    printf 'Error: this uninstaller supports Linux only.\n' >&2
    exit 1
}

remove_path() {
    local path="$1"
    [[ -e "${path}" || -L "${path}" ]] || return 1
    if [[ -w "$(dirname -- "${path}")" || ${EUID} -eq 0 ]]; then
        rm -f -- "${path}"
    elif command -v sudo >/dev/null 2>&1; then
        sudo rm -f -- "${path}"
    else
        printf 'Error: cannot remove %s without administrator access.\n' "${path}" >&2
        return 2
    fi
    printf 'Removed: %s\n' "${path}"
}

BINARY=""
DESKTOP="${DESKTOP_DEFAULT}"
ICON="${ICON_DEFAULT}"
BUILD="${PROJECT_ROOT}/build"
if [[ -f "${METADATA_FILE}" ]]; then
    BINARY="$(sed -n 's/^BINARY=//p' "${METADATA_FILE}" | head -n1)"
    DESKTOP="$(sed -n 's/^DESKTOP=//p' "${METADATA_FILE}" | head -n1)"
    ICON="$(sed -n 's/^ICON=//p' "${METADATA_FILE}" | head -n1)"
    BUILD="$(sed -n 's/^BUILD=//p' "${METADATA_FILE}" | head -n1)"
fi

found=0
if [[ -n "${BINARY}" ]] && remove_path "${BINARY}"; then
    found=1
fi
for candidate in "${HOME}/.local/bin/NanoPulse" "/usr/local/bin/NanoPulse"; do
    [[ "${candidate}" == "${BINARY}" ]] && continue
    if remove_path "${candidate}"; then
        found=1
    fi
done
remove_path "${DESKTOP}" || true
remove_path "${ICON}" || true

case "${BUILD}" in
    "${PROJECT_ROOT}/build")
        if [[ -d "${BUILD}" ]]; then
            rm -rf -- "${BUILD}"
            printf 'Removed: %s\n' "${BUILD}"
        fi
        ;;
    *) printf 'Skipped unsafe build path: %s\n' "${BUILD}" >&2 ;;
esac
if [[ -d "${HOME}/.cache/NanoPulse" ]]; then
    rm -rf -- "${HOME}/.cache/NanoPulse"
    printf 'Removed: %s\n' "${HOME}/.cache/NanoPulse"
fi

remove_config=0
if (( PURGE )); then
    remove_config=1
elif [[ -t 0 ]]; then
    read -r -p "Remove NanoPulse user configuration and database? [y/N] " answer
    [[ "${answer}" =~ ^[Yy]$ ]] && remove_config=1
fi
if (( remove_config )); then
    for config in \
        "${XDG_CONFIG_HOME:-${HOME}/.config}/NanoPulse/NanoPulse" \
        "${XDG_DATA_HOME:-${HOME}/.local/share}/NanoPulse/NanoPulse" \
        "${XDG_CONFIG_HOME:-${HOME}/.config}/NanoPulse" \
        "${XDG_DATA_HOME:-${HOME}/.local/share}/NanoPulse"; do
        if [[ -d "${config}" ]]; then
            rm -rf -- "${config}"
            printf 'Removed: %s\n' "${config}"
        fi
    done
fi
rm -f -- "${METADATA_FILE}"
rmdir --ignore-fail-on-non-empty "${STATE_DIR}" 2>/dev/null || true

if (( ! found )); then
    printf 'NanoPulse was not installed in a known location. Remaining artifacts were cleaned.\n'
else
    printf 'NanoPulse uninstallation completed.\n'
fi
