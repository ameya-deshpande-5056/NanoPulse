#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_WORK="${PROJECT_ROOT}/build-portable-linux"
PORTABLE_ROOT="${PACKAGE_WORK}/NanoPulse-portable"
DIST_DIR="${PROJECT_ROOT}/dist"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    printf 'Usage: ./build-portable.sh\nBuild the current source as a Linux portable ZIP.\n'
    exit 0
elif (($#)); then
    printf 'Error: unknown option %s\n' "$1" >&2
    exit 2
fi

[[ "$(uname -s)" == "Linux" ]] || {
    printf 'Error: build-portable.sh supports Linux only.\n' >&2
    exit 1
}
[[ "$(uname -m)" == "x86_64" ]] || {
    printf 'Error: this portable builder currently supports x86_64 only.\n' >&2
    exit 1
}
for tool in cmake curl zip sha256sum; do
    command -v "${tool}" >/dev/null 2>&1 || {
        printf 'Error: %s is required.\n' "${tool}" >&2
        exit 1
    }
done
QMAKE="${QMAKE:-$(command -v qmake6 || true)}"
[[ -n "${QMAKE}" ]] || {
    printf 'Error: qmake6 is required by the Qt deployment plugin.\n' >&2
    exit 1
}

bash "${PROJECT_ROOT}/run.sh" --build-only --clean
case "${PACKAGE_WORK}" in
    "${PROJECT_ROOT}/build-portable-linux") rm -rf -- "${PACKAGE_WORK}" ;;
    *)
        printf 'Error: unsafe package directory %s\n' "${PACKAGE_WORK}" >&2
        exit 1
        ;;
esac
mkdir -p -- "${PORTABLE_ROOT}/usr" "${PACKAGE_WORK}/tools" "${DIST_DIR}"
cmake --install "${PROJECT_ROOT}/build-run" --config Release \
    --prefix "${PORTABLE_ROOT}/usr"

LINUXDEPLOY_BIN="${LINUXDEPLOY:-${PACKAGE_WORK}/tools/linuxdeploy-x86_64.AppImage}"
QT_PLUGIN_BIN="${LINUXDEPLOY_PLUGIN_QT:-${PACKAGE_WORK}/tools/linuxdeploy-plugin-qt-x86_64.AppImage}"
if [[ ! -x "${LINUXDEPLOY_BIN}" ]]; then
    curl --fail --location --retry 3 --output "${LINUXDEPLOY_BIN}" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "${LINUXDEPLOY_BIN}"
fi
if [[ ! -x "${QT_PLUGIN_BIN}" ]]; then
    curl --fail --location --retry 3 --output "${QT_PLUGIN_BIN}" \
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    chmod +x "${QT_PLUGIN_BIN}"
fi

export QMAKE
export LINUXDEPLOY_PLUGIN_QT="${QT_PLUGIN_BIN}"
export APPIMAGE_EXTRACT_AND_RUN=1
"${LINUXDEPLOY_BIN}" --appdir "${PORTABLE_ROOT}" \
    --executable "${PORTABLE_ROOT}/usr/bin/NanoPulse" \
    --desktop-file "${PROJECT_ROOT}/packaging/linux/com.example.NanoPulse.desktop" \
    --icon-file "${PROJECT_ROOT}/packaging/linux/com.example.NanoPulse.svg" \
    --plugin qt

printf '%s\n' \
    "NanoPulse portable build." \
    "Run ./AppRun from this directory." \
    >"${PORTABLE_ROOT}/README.txt"
VERSION="$(grep -A3 'project(NanoPulse' "${PROJECT_ROOT}/CMakeLists.txt" \
    | grep VERSION | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)"
[[ -n "${VERSION}" ]] || {
    printf 'Error: version not found in CMakeLists.txt.\n' >&2
    exit 1
}
ARCH="$(uname -m)"
ARCHIVE="${DIST_DIR}/NanoPulse-portable-linux-${ARCH}-v${VERSION}.zip"
rm -f -- "${ARCHIVE}" "${ARCHIVE}.sha256"
(cd "${PACKAGE_WORK}" && zip -qr "${ARCHIVE}" NanoPulse-portable)
(cd "${DIST_DIR}" && sha256sum "$(basename -- "${ARCHIVE}")" \
    >"$(basename -- "${ARCHIVE}").sha256")
printf 'Portable ZIP: %s\nChecksum: %s.sha256\n' "${ARCHIVE}" "${ARCHIVE}"
