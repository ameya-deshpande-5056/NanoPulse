# NanoPulse REST Client

Lightweight offline REST client for Windows and Linux, built with C++17 and Qt 6 Widgets.

## Build

Requirements: Qt 6.5+ with Core, Gui, Network, Sql, Widgets, and the SQLite driver.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

For a static executable, point `CMAKE_PREFIX_PATH` at a static Qt build and add
`-DNANOPULSE_STATIC_QT=ON`. Static Qt licensing and platform plugin requirements
remain the distributor's responsibility.

Runtime data is stored only in the platform user data/config directories. NanoPulse
has no telemetry, cloud sync, accounts, updater, or background process.

See [INSTALL.md](INSTALL.md) for native installers and [DEVELOPMENT.md](DEVELOPMENT.md)
for package and CI/CD details.
