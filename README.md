# NanoPulse REST Client

Lightweight offline REST client for Windows and Linux, built with C++17 and Qt 6 Widgets.

## Features

- Send standard or custom HTTP methods with query parameters, headers, Basic/Bearer/API-key authentication, and configurable timeouts.
- Create raw JSON, XML, HTML, JavaScript, and text bodies, URL-encoded forms, multipart forms with file uploads, or binary-file bodies.
- Configure redirects, TLS verification, HTTP/SOCKS5 proxies, PEM client certificates, and per-session cookies.
- Organize requests in collections, reuse `{{variables}}` from environments, inspect request history, and work in multiple request tabs.
- Import Swagger/OpenAPI definitions from a file or URL, import/export collections and environments, save responses, and copy requests as cURL commands.
- Inspect streamed response bodies and headers with search, formatting, syntax highlighting, and light/dark themes.

## Run the current source

On Linux:

```bash
./run.sh
```

On Windows:

```bat
run.bat
```

Both launchers accept `--clean` and `--build-only`. Run the Linux launcher as a
regular desktop user, without `sudo`.

## Build

Requirements: Qt 6.4+ with Core, Gui, Network, Sql, Widgets, and the SQLite driver.

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
