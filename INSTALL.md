# NanoPulse installation

## Linux

Supported package managers are APT, DNF, YUM, Pacman, and Zypper.

```bash
chmod +x install.sh uninstall.sh
./install.sh
```

Non-root installations default to `~/.local/bin`. Root installations default to
`/usr/local/bin`.

Options:

```bash
./install.sh --prefix /custom/bin
./install.sh --skip-deps
./install.sh --help
```

Uninstall:

```bash
./uninstall.sh
./uninstall.sh --purge
```

The normal uninstall command asks before deleting the SQLite database and user
settings. `--purge` deletes them without prompting.

## Windows

Run from an x64 Native Tools Command Prompt for Visual Studio 2022:

```bat
install.bat --portable
```

The default installation uses `%PROGRAMFILES%\NanoPulse` and requires an
Administrator prompt. Alternatives:

```bat
install.bat --user
install.bat --path "D:\Applications\NanoPulse"
install.bat --user --qt-auto
```

`--qt-auto` installs Qt 6.8.3 through `aqtinstall` when Qt is unavailable.
SQLite3 development headers must be available through `SQLITE3_ROOT` or vcpkg.
The installer can optionally add the selected directory to PATH.

Uninstall from Windows Apps, the installed `uninstall.bat`, or:

```bat
uninstall.bat
uninstall.bat --purge
```

`--purge` deletes settings and the local database without prompting.

## Prebuilt artifacts

CI produces:

- `NanoPulse-x86_64-v0.0.1.AppImage`
- `nanopulse_0.0.1_amd64.deb`
- `com.example.NanoPulse-v0.0.1.flatpak`
- `NanoPulse-portable-x64-v0.0.1.zip`
- `NanoPulse-installer-x64-v0.0.1.exe`
- `NanoPulse-installer-x64-v0.0.1.msi`

Every artifact includes a `.sha256` file. Tagged releases also contain a
consolidated `CHECKSUMS.txt`.
