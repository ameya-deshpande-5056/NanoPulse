#!/usr/bin/env bash
set -Eeuo pipefail

APP_NAME="NanoPulse"
PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
PREFIX=""
SKIP_DEPS=0
INSTALLED_BINARY=""
DESKTOP_FILE="${HOME}/.local/share/applications/com.example.NanoPulse.desktop"
ICON_FILE="${HOME}/.local/share/icons/hicolor/scalable/apps/com.example.NanoPulse.svg"
STATE_DIR="${XDG_STATE_HOME:-${HOME}/.local/state}/NanoPulse"
METADATA_FILE="${STATE_DIR}/install.conf"
BACKUP_FILE=""
QT_CMAKE_PREFIX="${CMAKE_PREFIX_PATH:-}"

usage() {
    printf '%s\n' \
        "Usage: ./install.sh [--prefix DIR] [--skip-deps] [--help]" \
        "  --prefix DIR  Install the executable in DIR" \
        "  --skip-deps   Skip prerequisite checks and package installation" \
        "  --help        Show this help"
}

fail() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

VERSION="$(grep -A3 'project(NanoPulse' "${PROJECT_ROOT}/CMakeLists.txt" \
    | grep VERSION | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)"
[[ -n "${VERSION}" ]] || fail "Version not found in CMakeLists.txt."

run_privileged() {
    local target="${INSTALL_DIR:-}"
    if (( EUID == 0 )) || [[ -z "${target}" || -w "${target}" \
        || ( ! -e "${target}" && -w "$(dirname -- "${target}")" ) ]]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        fail "Administrator access is required for ${1}."
    fi
}

rollback() {
    local status=$?
    if (( status == 0 )); then
        return
    fi
    printf 'Installation failed; rolling back.\n' >&2
    if [[ -n "${INSTALLED_BINARY}" && -e "${INSTALLED_BINARY}" ]]; then
        run_privileged rm -f -- "${INSTALLED_BINARY}" || true
    fi
    if [[ -n "${BACKUP_FILE}" && -e "${BACKUP_FILE}" ]]; then
        run_privileged mv -- "${BACKUP_FILE}" "${INSTALLED_BINARY}" || true
    fi
    rm -f -- "${DESKTOP_FILE}" "${ICON_FILE}" "${METADATA_FILE}" 2>/dev/null || true
    exit "${status}"
}
trap rollback ERR

while (($#)); do
    case "$1" in
        --prefix)
            (($# >= 2)) || fail "--prefix requires a directory."
            PREFIX="$2"
            shift 2
            ;;
        --skip-deps)
            SKIP_DEPS=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            fail "Unknown option: $1"
            ;;
    esac
done

[[ "$(uname -s)" == "Linux" ]] || fail "This installer supports Linux only."

install_packages() {
    local manager=""
    local packages=()
    if command -v apt-get >/dev/null 2>&1; then
        manager="apt"
        packages=(build-essential cmake pkg-config ninja-build libsqlite3-dev)
        if apt-cache show qt6-base-dev >/dev/null 2>&1; then
            packages+=(qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite)
        else
            packages+=(python3-pip libgl1-mesa-dev libxkbcommon-dev)
        fi
    elif command -v dnf >/dev/null 2>&1; then
        manager="dnf"
        packages=(gcc-c++ cmake pkgconf-pkg-config ninja-build qt6-qtbase-devel
                  sqlite-devel)
    elif command -v yum >/dev/null 2>&1; then
        manager="yum"
        packages=(gcc-c++ cmake pkgconfig ninja-build qt6-qtbase-devel sqlite-devel)
    elif command -v pacman >/dev/null 2>&1; then
        manager="pacman"
        packages=(base-devel cmake pkgconf ninja qt6-base sqlite)
    elif command -v zypper >/dev/null 2>&1; then
        manager="zypper"
        packages=(gcc-c++ cmake pkg-config ninja qt6-base-devel sqlite3-devel)
    else
        fail "No supported package manager found (apt, dnf, yum, pacman, zypper)."
    fi

    local elevate=()
    if (( EUID != 0 )); then
        command -v sudo >/dev/null 2>&1 || fail "sudo is required to install dependencies."
        elevate=(sudo)
    fi
    case "${manager}" in
        apt)
            "${elevate[@]}" apt-get update
            "${elevate[@]}" apt-get install -y "${packages[@]}"
            if ! apt-cache show qt6-base-dev >/dev/null 2>&1; then
                local qt_root="${HOME}/.local/share/NanoPulse/Qt"
                python3 -m pip install --user aqtinstall
                python3 -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
                    -O "${qt_root}"
                QT_CMAKE_PREFIX="${qt_root}/6.8.3/gcc_64"
            fi
            ;;
        dnf) "${elevate[@]}" dnf install -y "${packages[@]}" ;;
        yum) "${elevate[@]}" yum install -y "${packages[@]}" ;;
        pacman) "${elevate[@]}" pacman -Sy --needed --noconfirm "${packages[@]}" ;;
        zypper) "${elevate[@]}" zypper --non-interactive install "${packages[@]}" ;;
    esac
}

check_prerequisites() {
    local missing=0
    command -v cmake >/dev/null 2>&1 || missing=1
    command -v pkg-config >/dev/null 2>&1 || missing=1
    command -v ninja >/dev/null 2>&1 || command -v make >/dev/null 2>&1 || missing=1
    command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 \
        || command -v clang++ >/dev/null 2>&1 || missing=1
    pkg-config --exists sqlite3 2>/dev/null || missing=1
    pkg-config --exists Qt6Core Qt6Gui Qt6Network Qt6Sql Qt6Widgets 2>/dev/null \
        || command -v qtpaths6 >/dev/null 2>&1 \
        || [[ -f "${QT_CMAKE_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake" ]] || missing=1
    if (( missing )); then
        printf 'Missing build dependencies; installing them.\n'
        install_packages
    fi

    command -v cmake >/dev/null 2>&1 || fail "CMake is unavailable."
    local cmake_version
    cmake_version="$(cmake --version | awk 'NR==1 {print $3}')"
    [[ "$(printf '%s\n' "3.16" "${cmake_version}" | sort -V | head -n1)" == "3.16" ]] \
        || fail "CMake 3.16+ is required; found ${cmake_version}."

    local compiler
    compiler="$(command -v c++ || command -v g++ || command -v clang++ || true)"
    [[ -n "${compiler}" ]] || fail "A GCC or Clang C++ compiler is required."
    local probe
    probe="$(mktemp -d)"
    printf 'int main(){return 0;}\n' >"${probe}/cxx17.cpp"
    "${compiler}" -std=c++17 "${probe}/cxx17.cpp" -o "${probe}/cxx17" \
        || fail "The available compiler does not support C++17."
    rm -rf -- "${probe}"
}

if (( ! SKIP_DEPS )); then
    check_prerequisites
fi

if [[ -n "${PREFIX}" ]]; then
    INSTALL_DIR="${PREFIX%/}"
elif (( EUID == 0 )); then
    INSTALL_DIR="/usr/local/bin"
else
    INSTALL_DIR="${HOME}/.local/bin"
fi
[[ -n "${INSTALL_DIR}" && "${INSTALL_DIR}" != "/" ]] || fail "Unsafe installation prefix."

case "${BUILD_DIR}" in
    "${PROJECT_ROOT}/build") rm -rf -- "${BUILD_DIR}" ;;
    *) fail "Unsafe build directory: ${BUILD_DIR}" ;;
esac

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi
CMAKE_PREFIX_ARGS=()
if [[ -n "${QT_CMAKE_PREFIX}" ]]; then
    CMAKE_PREFIX_ARGS=(-DCMAKE_PREFIX_PATH="${QT_CMAKE_PREFIX}")
fi
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG" \
    "${CMAKE_PREFIX_ARGS[@]}"
cmake --build "${BUILD_DIR}" --config Release --parallel

BINARY="${BUILD_DIR}/NanoPulse"
[[ -x "${BINARY}" ]] || fail "Build completed without producing ${BINARY}."
run_privileged mkdir -p -- "${INSTALL_DIR}"
INSTALLED_BINARY="${INSTALL_DIR}/NanoPulse"
if [[ -e "${INSTALLED_BINARY}" ]]; then
    BACKUP_FILE="${INSTALLED_BINARY}.install-backup"
    run_privileged cp -p -- "${INSTALLED_BINARY}" "${BACKUP_FILE}"
fi
run_privileged install -m 0755 "${BINARY}" "${INSTALLED_BINARY}"

mkdir -p -- "$(dirname -- "${DESKTOP_FILE}")" "$(dirname -- "${ICON_FILE}")" "${STATE_DIR}"
install -m 0644 "${PROJECT_ROOT}/packaging/linux/com.example.NanoPulse.svg" "${ICON_FILE}"
cat >"${DESKTOP_FILE}" <<EOF
[Desktop Entry]
Type=Application
Name=NanoPulse
Comment=Lightweight REST API testing client
Exec="${INSTALLED_BINARY}"
Icon=com.example.NanoPulse
Terminal=false
Categories=Development;Utility;
EOF
printf 'BINARY=%s\nDESKTOP=%s\nICON=%s\nBUILD=%s\nVERSION=%s\n' \
    "${INSTALLED_BINARY}" "${DESKTOP_FILE}" "${ICON_FILE}" "${BUILD_DIR}" "${VERSION}" \
    >"${METADATA_FILE}"
[[ -z "${BACKUP_FILE}" ]] || run_privileged rm -f -- "${BACKUP_FILE}"
BACKUP_FILE=""
trap - ERR

printf '\nNanoPulse %s installed successfully.\nBinary: %s\nUninstall: %s/uninstall.sh\n' \
    "${VERSION}" "${INSTALLED_BINARY}" "${PROJECT_ROOT}"
