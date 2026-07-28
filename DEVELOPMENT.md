# NanoPulse development

## Requirements

- CMake 3.16+
- C++17 GCC, Clang, or MSVC compiler
- Qt 6 Core, Gui, Network, Sql, and Widgets development modules
- Qt SQLite driver
- SQLite3 development package
- Ninja or Make

`nlohmann/json` 3.11.3 is vendored under `third_party/`.

## Linux build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/NanoPulse
```

On Debian/Ubuntu:

```bash
sudo apt-get install build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite libsqlite3-dev
```

## Windows build

From an x64 Native Tools Command Prompt for Visual Studio 2022:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Set `CMAKE_PREFIX_PATH` or `Qt6_DIR` when Qt is not discoverable.

## Run the current source

These commands configure, rebuild changed files, and launch NanoPulse:

```bash
chmod +x run.sh build-portable.sh
./run.sh
```

```bat
run.bat
```

Use `--clean` to force a clean build. `--build-only` skips launching.

## Build portable ZIPs

The portable builders clean-build the current source, deploy Qt runtime files,
create a versioned ZIP under `dist/`, and generate its SHA256 file:

```bash
./build-portable.sh
```

```bat
build-portable.bat
```

The Linux builder requires `curl`, `zip`, `qmake6`, and internet access to obtain
linuxdeploy when it is not supplied through `LINUXDEPLOY`.

## Static Qt

Use a statically built Qt installation:

```bash
cmake -S . -B build-static -DCMAKE_BUILD_TYPE=Release \
  -DNANOPULSE_STATIC_QT=ON -DCMAKE_PREFIX_PATH=/path/to/static/Qt
cmake --build build-static --parallel
```

Qt licensing and static platform-plugin requirements remain the distributor's
responsibility.

## Versioning

The canonical development version is the `VERSION` value in `CMakeLists.txt`.
The executable, local installers, and portable builders read it automatically.
Release tags use semantic versioning, for example `v0.0.1` or
`v0.1.0-beta.1`. Release CI validates the tag's numeric version against CMake
and uses the full tag version for artifact names and installer metadata.

## CI/CD

- `build-linux.yml` builds AppImage, DEB, and Flatpak artifacts.
- `build-windows.yml` builds portable ZIP, NSIS EXE, and WiX MSI artifacts.
- `release.yml` rebuilds all formats for `v*` tags and publishes them.

Optional Windows signing secrets:

- `WINDOWS_CERTIFICATE_BASE64`
- `WINDOWS_CERTIFICATE_PASSWORD`

Artifacts and failure logs are retained for 30 days.
