# HyaloDE

**Nowoczesne środowisko graficzne Wayland** zbudowane w C++20 z GTK4.

> 🌐 [English version below](#hyalode-english)

HyaloDE to kompletny stos desktopowy — panel z launcherem i taskbarem, ekran logowania LightDM, centrum ustawień, terminal, menedżer plików, tapety i więcej. Wszystkie aplikacje współdzielą konfigurację JSON, lokalizację i motywy CSS.

## Komponenty

| Aplikacja | Opis |
|-----------|------|
| **hyalo-panel** | Panel/taskbar z launcherem, zegarkiem, tray, śledzeniem okien (wlr-foreign-toplevel) |
| **hyalo-greeter** | Ekran logowania LightDM z obsługą wielu użytkowników i sesji |
| **hyalo-control-center** | Centrum ustawień (wygląd, klawiatura, autostart) |
| **hyalo-terminal** | Emulator terminala (VTE/GTK4) |
| **hyalo-files** | Menedżer plików |
| **hyalo-wallpaper** | Ustawianie tapet |
| **hyalo-software-store** | Sklep z oprogramowaniem |
| **hyalo-update-center** | Centrum aktualizacji |
| **hyalo-core** | Współdzielona biblioteka: konfiguracja, lokalizacja, CSS |

## Instalacja

### Szybka instalacja (zalecane)

```bash
git clone https://github.com/mijsys/HyaloDE.git
cd HyaloDE
sudo ./install.sh --system
```

Instalator automatycznie:
- Instaluje wszystkie zależności (Arch/Debian/Fedora/openSUSE)
- Buduje wszystkie aplikacje HyaloDE (CMake) i kompozytor HyaloWM (Meson)
- Instaluje sesję Wayland widoczną w LightDM/SDDM
- Konfiguruje LightDM do używania hyalo-greeter
- Uruchamia `hyalo-session` z panelem, powiadomieniami (mako), tapetami (swww) i autostartem (dex)

### Interaktywny instalator

```bash
sudo ./packaging/install-hyalode.sh
```

Oferuje menu z opcjami:
1. Instalacja z repozytorium pacman (Arch)
2. Budowanie ze źródeł
3. Zarządzanie repo pacman
4. Odinstalowanie

### Instalacja lokalna (testowanie)

```bash
./install.sh --user
```

### Odinstalowanie

```bash
sudo ./install.sh --system --remove
```

## Zależności

### Budowanie

| Pakiet | Wersja |
|--------|--------|
| CMake | ≥ 3.24 |
| Meson + Ninja | dowolna |
| GCC/G++ | C++20 |
| pkg-config | dowolna |

### Runtime

| Pakiet (Arch) | Pakiet (Debian) | Cel |
|---------------|-----------------|-----|
| gtkmm-4.0 | libgtkmm-4.0-dev | GTK4 C++ |
| gtk4-layer-shell | libgtk-4-layer-shell-dev | Wayland layer shell |
| lightdm + liblightdm-gobject-1 | lightdm + liblightdm-gobject-1-dev | Display manager + API |
| vte4 | libvte-2.91-gtk4-dev | Terminal |
| gdk-pixbuf2 | libgdk-pixbuf-2.0-dev | Obrazy |
| wayland + wayland-protocols | libwayland-dev + wayland-protocols | Protokół Wayland |
| nlohmann-json | nlohmann-json3-dev | JSON (auto-fetch jeśli brak) |
| swww | swww | Tapety |
| mako | mako-notifier | Powiadomienia |
| dex | dex | XDG autostart |
| grim + slurp | grim + slurp | Zrzuty ekranu |
| wl-clipboard | wl-clipboard | Schowek |

## Skróty klawiszowe

| Skrót | Akcja |
|-------|-------|
| `Super+Space` | Launcher |
| `Print` | Zrzut ekranu (pełny) → `~/Pictures/Screenshots` |
| `Shift+Print` | Zrzut ekranu (zaznaczenie) |

## Konfiguracja

Użytkownik może nadpisać ustawienia w `~/.config/hyalo/`:

| Plik | Opis |
|------|------|
| `config.json` | Globalne ustawienia (język, wygląd, panel, skróty) |
| `theme.css` | Nadpisanie motywu CSS |
| `locales/<lang>.json` | Własne tłumaczenia |

Jeśli pliki użytkownika nie istnieją, używane są wartości domyślne z `config/defaults/`.

## Budowanie ręczne

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Flagi opcjonalne:

```
-DHYALO_BUILD_GREETER=ON/OFF
-DHYALO_BUILD_TERMINAL=ON/OFF
-DHYALO_BUILD_CONTROL_CENTER=ON/OFF
-DHYALO_BUILD_UPDATE_CENTER=ON/OFF
-DHYALO_BUILD_SOFTWARE_STORE=ON/OFF
-DHYALO_BUILD_FILES=ON/OFF
```

## Struktura projektu

```
├── apps/                     # Aplikacje
│   ├── hyalo-panel/          # Panel z launcherem i taskbarem
│   ├── hyalo-greeter/        # LightDM greeter
│   ├── hyalo-control-center/ # Ustawienia
│   ├── hyalo-terminal/       # Terminal
│   ├── hyalo-files/          # Menedżer plików
│   ├── hyalo-wallpaper/      # Tapety
│   ├── hyalo-software-store/ # Sklep
│   └── hyalo-update-center/  # Aktualizacje
├── assets/                   # Ikony, motywy, dekoracje
├── config/                   # Domyślna konfiguracja + labwc + mako
├── libs/hyalo-core/          # Współdzielona biblioteka runtime
├── packaging/                # PKGBUILD, instalator, CI
├── compositor/               # HyaloWM (fork labwc)
└── third_party/              # Protokoły Wayland (submoduły)
```

---

# HyaloDE (English)

**A modern Wayland desktop environment** built with C++20 and GTK4.

HyaloDE is a complete desktop stack — panel with launcher and taskbar, LightDM login screen, settings center, terminal, file manager, wallpaper manager and more. All applications share JSON configuration, localization and CSS themes.

## Components

| Application | Description |
|-------------|-------------|
| **hyalo-panel** | Panel/taskbar with launcher, clock, tray, window tracking (wlr-foreign-toplevel) |
| **hyalo-greeter** | LightDM login screen with multi-user and session support |
| **hyalo-control-center** | Settings center (appearance, keyboard, autostart) |
| **hyalo-terminal** | Terminal emulator (VTE/GTK4) |
| **hyalo-files** | File manager |
| **hyalo-wallpaper** | Wallpaper setter |
| **hyalo-software-store** | Software store |
| **hyalo-update-center** | Update center |
| **hyalo-core** | Shared library: configuration, localization, CSS |

## Installation

### Quick install (recommended)

```bash
git clone https://github.com/mijsys/HyaloDE.git
cd HyaloDE
sudo ./install.sh --system
```

The installer automatically:
- Installs all dependencies (Arch/Debian/Fedora/openSUSE)
- Builds all HyaloDE apps (CMake) and the HyaloWM compositor (Meson)
- Installs a Wayland session visible in LightDM/SDDM
- Configures LightDM to use hyalo-greeter
- Launches `hyalo-session` with panel, notifications (mako), wallpaper (swww) and autostart (dex)

### Interactive installer

```bash
sudo ./packaging/install-hyalode.sh
```

Menu options:
1. Install from pacman repository (Arch)
2. Build from source
3. Manage pacman repository
4. Uninstall

### Local install (testing)

```bash
./install.sh --user
```

### Uninstall

```bash
sudo ./install.sh --system --remove
```

## Dependencies

### Build

| Package | Version |
|---------|---------|
| CMake | ≥ 3.24 |
| Meson + Ninja | any |
| GCC/G++ | C++20 |
| pkg-config | any |

### Runtime

| Package (Arch) | Package (Debian) | Purpose |
|----------------|------------------|---------|
| gtkmm-4.0 | libgtkmm-4.0-dev | GTK4 C++ |
| gtk4-layer-shell | libgtk-4-layer-shell-dev | Wayland layer shell |
| lightdm + liblightdm-gobject-1 | lightdm + liblightdm-gobject-1-dev | Display manager + API |
| vte4 | libvte-2.91-gtk4-dev | Terminal |
| gdk-pixbuf2 | libgdk-pixbuf-2.0-dev | Image loading |
| wayland + wayland-protocols | libwayland-dev + wayland-protocols | Wayland protocol |
| nlohmann-json | nlohmann-json3-dev | JSON (auto-fetched if missing) |
| swww | swww | Wallpaper |
| mako | mako-notifier | Notifications |
| dex | dex | XDG autostart |
| grim + slurp | grim + slurp | Screenshots |
| wl-clipboard | wl-clipboard | Clipboard |

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Super+Space` | Launcher |
| `Print` | Full screenshot → `~/Pictures/Screenshots` |
| `Shift+Print` | Area screenshot |

## Configuration

User overrides go in `~/.config/hyalo/`:

| File | Description |
|------|-------------|
| `config.json` | Global settings (language, appearance, panel, shortcuts) |
| `theme.css` | CSS theme override |
| `locales/<lang>.json` | Custom translations |

Defaults are loaded from `config/defaults/` when user files don't exist.

## Manual build

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Optional flags:

```
-DHYALO_BUILD_GREETER=ON/OFF
-DHYALO_BUILD_TERMINAL=ON/OFF
-DHYALO_BUILD_CONTROL_CENTER=ON/OFF
-DHYALO_BUILD_UPDATE_CENTER=ON/OFF
-DHYALO_BUILD_SOFTWARE_STORE=ON/OFF
-DHYALO_BUILD_FILES=ON/OFF
```

## Project structure

```
├── apps/                     # Applications
│   ├── hyalo-panel/          # Panel with launcher and taskbar
│   ├── hyalo-greeter/        # LightDM greeter
│   ├── hyalo-control-center/ # Settings
│   ├── hyalo-terminal/       # Terminal
│   ├── hyalo-files/          # File manager
│   ├── hyalo-wallpaper/      # Wallpaper
│   ├── hyalo-software-store/ # Software store
│   └── hyalo-update-center/  # Updates
├── assets/                   # Icons, themes, decorations
├── config/                   # Default config + labwc + mako
├── libs/hyalo-core/          # Shared runtime library
├── packaging/                # PKGBUILD, installer, CI
├── compositor/               # HyaloWM (labwc fork)
└── third_party/              # Wayland protocols (submodules)
```

## License

MIT

## Author

[MijSys](https://github.com/mijsys)
