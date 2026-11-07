# Finality Manager

## Purpose

Finality Manager is a GTK4 desktop tool for managing multiple Age of History 3
instances. It discovers game installations, manages per-instance mods and
playlists, and launches the game through Finality Loader. Mod enablement is kept
in sync with the game's `settings/ModsOff.txt`, and playlists are stored under
`.finality/manager/` inside each instance.

## Build

Requirements: GTK 4 development packages, a C compiler (GCC or compatible), and
`pkg-config`. From this directory, run:

```sh
make
```

The binary is written to `build/linux/finality-manager`. Run the headless tests
with `make test`. Cross-compilation and packaging targets are also available:

```sh
make windows          # Windows x86_64 executable
make bundle-windows   # self-contained Windows directory
make nsis             # Windows installer
make app-image        # Linux AppImage
```

## License

This project is licensed under the GNU General Public License v3.0 (GPLv3).
See [LICENSE](LICENSE).
