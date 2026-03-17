# HyaloOS
Autor Patryk "MijagiKutasamoto" Szkudarek

HyaloOS is a C++-based Wayland desktop environment stack focused on a custom panel, shared JSON configuration, and a control center that reuse the same runtime foundation.

## Current scope

- `hyalo-panel`: a GTK4 panel prototype for Wayland compositors, using `gtk4-layer-shell` when available, with interactive taskbar control via `zwlr_foreign_toplevel_manager_v1` and real workspace switching via `ext_workspace_manager_v1`.
- `hyalo-control-center`: a minimal control center shell wired to the same config and localization stack.
- `hyalo-core`: shared runtime services for configuration, translations, and global CSS loading.

## Dependencies

- CMake 3.24+
- gtkmm-4.0
- gtk4-layer-shell for true layer-shell panel mode; without it `hyalo-panel` falls back to a regular GTK4 window
- wayland-client + wayland-scanner, plus protocol XML from system packages or vendored copies in `third_party/`
- nlohmann_json 3.11+ if available locally; otherwise CMake fetches `nlohmann/json` automatically during configure

Optional runtime tools:

- grim for screenshots
- slurp for area selection screenshots
- libnotify or another notification backend providing notify-send for screenshot completion notifications
- mako for HyaloOS-themed notification banners in the Wayland session

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Install

For a real display-manager-visible session, use the installer script:

```bash
sudo ./install.sh --system
```

This does the following:

- builds and installs the CMake-based HyaloOS applications
- builds `hyalo-compositor` with Meson and installs only the HyaloOS/HyaloWM runtime artifacts instead of the full fallback subproject tree
- installs a `hyalo.desktop` Wayland session entry for SDDM, LightDM, and similar display managers
- installs a `hyalo-session` wrapper that autostarts `hyalo-panel`
- records an install manifest at `PREFIX/share/hyalo/install-manifest.txt` for later removal

For local testing without touching system directories:

```bash
./install.sh --user
```

If `dex` is installed, the generated session wrapper will also run XDG autostart entries for the `HyaloOS` desktop name.

To remove a previous install:

```bash
sudo ./install.sh --system --remove
```

or for a user-local test install:

```bash
./install.sh --user --remove
```

## Running

`hyalo-panel` is primarily intended for a Wayland session.

- With `gtk4-layer-shell` installed, it can behave like a real edge-anchored panel.
- Without `gtk4-layer-shell`, it falls back to a regular GTK4 window.
- In X11 sessions, GTK may still print `libEGL` or renderer warnings depending on the local graphics stack. These warnings do not indicate a HyaloOS crash; Wayland is the recommended runtime target.

## Default shortcuts

- `Super+Space`: toggle Hyalo launcher
- `Print`: save a full-screen screenshot to `~/Pictures/Screenshots`
- `Shift+Print`: select an area and save it to `~/Pictures/Screenshots`

## Runtime configuration

The applications load user overrides from `~/.config/hyalo/`.

- `config.json`: global settings
- `theme.css`: custom CSS override
- `locales/<lang>.json`: optional user translations

If user files do not exist, defaults from the repository are used.

## Workspace mapping

`hyalo-panel` now reads a lightweight runtime export from the HyaloWM fork at `XDG_RUNTIME_DIR/hyalo/window-workspaces-v1.tsv`.

- The compositor rewrites this file whenever a mapped window changes workspace, title, app ID, or omnipresent state.
- The panel uses it to populate `WindowSnapshot.workspace`, so task filtering by active workspace works with real compositor data instead of placeholders.