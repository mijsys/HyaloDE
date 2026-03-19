# HyaloDE — Nowoczesne środowisko graficzne Wayland

> 🌐 [English version below](#hyalode--modern-wayland-desktop-environment)

<p align="center">
  <img src="https://raw.githubusercontent.com/mijsys/HyaloDE/main/assets/hyalo.svg" alt="HyaloDE" width="600">
</p>

**HyaloDE** to nowoczesne środowisko graficzne oparte na Wayland, zbudowane w C++20 z GTK4. Zawiera własny panel, launcher, control center, terminal, file manager, greeter LightDM i wiele więcej.

## Szybka instalacja (Arch Linux)

### Metoda 1 — Interaktywny instalator

```bash
curl -fsSL https://raw.githubusercontent.com/mijsys/HyaloDE/main/install-hyalode.sh | sudo bash
```

Lub pobierz i uruchom:

```bash
git clone https://github.com/mijsys/HyaloDE.git
cd HyaloDE
sudo ./install-hyalode.sh
```

### Metoda 2 — Repozytorium Pacman

Dodaj repozytorium do pacmana:

```bash
sudo ./install-hyalode.sh --add-repo
sudo pacman -S hyalode
```

Lub ręcznie dodaj na koniec `/etc/pacman.conf`:

```ini
[hyalode]
SigLevel = Optional TrustAll
Server = https://mijsys.github.io/HyaloDE/repo/$arch
```

Następnie:

```bash
sudo pacman -Sy hyalode
```

### Metoda 3 — Z AUR (PKGBUILD)

```bash
git clone https://github.com/mijsys/HyaloDE.git
cd HyaloDE/packaging/arch
makepkg -si
```

Wersja git:

```bash
cd packaging/arch
makepkg -si -p PKGBUILD-git
```

### Metoda 4 — Budowanie ze źródeł

```bash
sudo ./install-hyalode.sh --from-source
```

## Zależności

### Wymagane (budowanie)

| Pakiet | Opis |
|--------|------|
| `cmake` ≥ 3.24 | System budowania |
| `meson` + `ninja` | Budowanie kompozytora |
| `pkg-config` | Wykrywanie bibliotek |
| `git` | Pobieranie źródeł |
| `gcc`/`g++` (C++20) | Kompilator |

### Wymagane (runtime)

| Pakiet (Arch) | Pakiet (Debian/Ubuntu) | Opis |
|---------------|------------------------|------|
| `gtkmm-4.0` | `libgtkmm-4.0-dev` | GTK4 C++ bindings |
| `gtk4-layer-shell` | `libgtk-4-layer-shell-dev` | Wayland layer shell |
| `lightdm` | `lightdm` | Display manager |
| `liblightdm-gobject-1` | `liblightdm-gobject-1-dev` | LightDM API |
| `vte4` | `libvte-2.91-gtk4-dev` | Emulator terminala |
| `gdk-pixbuf2` | `libgdk-pixbuf-2.0-dev` | Ładowanie obrazów |
| `wayland` | `libwayland-dev` | Protokół Wayland |
| `nlohmann-json` | `nlohmann-json3-dev` | Parser JSON |
| `swww` | `swww` | Tapety |
| `mako` | `mako-notifier` | Powiadomienia |
| `dex` | `dex` | XDG autostart |
| `grim` | `grim` | Zrzuty ekranu |
| `slurp` | `slurp` | Zaznaczanie regionu |
| `wl-clipboard` | `wl-clipboard` | Schowek Wayland |

## Konfiguracja LightDM

Instalator automatycznie konfiguruje LightDM do używania `hyalo-greeter`. Konfiguracja zostaje zapisana do:

```
/etc/lightdm/lightdm.conf.d/50-hyalo-greeter.conf
```

Aby ręcznie zmienić greeter:

```bash
sudo nano /etc/lightdm/lightdm.conf.d/50-hyalo-greeter.conf
```

Zawartość:

```ini
[Seat:*]
greeter-session=hyalo-greeter
user-session=hyalo
```

## Komponenty

| Komponent | Opis |
|-----------|------|
| **hyalo-panel** | Panel/taskbar z launcherem, zegarkiem, tray |
| **hyalo-greeter** | Ekran logowania LightDM |
| **hyalo-control-center** | Centrum ustawień |
| **hyalo-terminal** | Emulator terminala |
| **hyalo-files** | Menedżer plików |
| **hyalo-wallpaper** | Ustawianie tapet |
| **hyalo-software-store** | Sklep z oprogramowaniem |
| **hyalo-update-center** | Centrum aktualizacji |

## Skróty klawiszowe

| Skrót | Akcja |
|-------|-------|
| `Super+Space` | Otwórz launcher |
| `Print` | Zrzut ekranu (pełny) |
| `Shift+Print` | Zrzut ekranu (zaznaczenie) |

## Odinstalowanie

```bash
sudo ./install-hyalode.sh --uninstall
```

Lub przez pacman:

```bash
sudo pacman -Rns hyalode
```

## Struktura projektu

```
├── apps/                    # Aplikacje HyaloDE
│   ├── hyalo-panel/         # Panel główny
│   ├── hyalo-greeter/       # LightDM greeter
│   ├── hyalo-control-center/# Centrum ustawień
│   ├── hyalo-terminal/      # Terminal
│   ├── hyalo-files/         # Menedżer plików
│   ├── hyalo-wallpaper/     # Tapety
│   ├── hyalo-software-store/# Sklep
│   └── hyalo-update-center/ # Aktualizacje
├── assets/                  # Ikony, motywy, dekoracje
├── config/                  # Domyślna konfiguracja
├── libs/hyalo-core/         # Biblioteka współdzielona
├── packaging/               # PKGBUILD i instalator
│   ├── arch/PKGBUILD        # Arch Linux (stable)
│   ├── arch/PKGBUILD-git    # Arch Linux (git)
│   └── install-hyalode.sh   # Uniwersalny instalator
└── compositor/              # HyaloWM (labwc fork)
```

---

# HyaloDE — Modern Wayland Desktop Environment

<p align="center">
  <img src="https://raw.githubusercontent.com/mijsys/HyaloDE/main/assets/banner.png" alt="HyaloDE" width="600">
</p>

**HyaloDE** is a modern Wayland desktop environment built with C++20 and GTK4. It includes a custom panel, launcher, control center, terminal, file manager, LightDM greeter and more.

## Quick install (Arch Linux)

### Method 1 — Interactive installer

```bash
curl -fsSL https://raw.githubusercontent.com/mijsys/HyaloDE/main/install-hyalode.sh | sudo bash
```

Or download and run:

```bash
git clone https://github.com/mijsys/HyaloDE.git
cd HyaloDE
sudo ./install-hyalode.sh
```

### Method 2 — Pacman repository

Add the repository to pacman:

```bash
sudo ./install-hyalode.sh --add-repo
sudo pacman -S hyalode
```

Or manually add to the end of `/etc/pacman.conf`:

```ini
[hyalode]
SigLevel = Optional TrustAll
Server = https://mijsys.github.io/HyaloDE/repo/$arch
```

Then:

```bash
sudo pacman -Sy hyalode
```

### Method 3 — From AUR (PKGBUILD)

```bash
git clone https://github.com/mijsys/HyaloDE.git
cd HyaloDE/packaging/arch
makepkg -si
```

Git version:

```bash
cd packaging/arch
makepkg -si -p PKGBUILD-git
```

### Method 4 — Build from source

```bash
sudo ./install-hyalode.sh --from-source
```

## Dependencies

### Build requirements

| Package | Description |
|---------|-------------|
| `cmake` ≥ 3.24 | Build system |
| `meson` + `ninja` | Compositor build |
| `pkg-config` | Library detection |
| `git` | Source fetching |
| `gcc`/`g++` (C++20) | Compiler |

### Runtime requirements

| Package (Arch) | Package (Debian/Ubuntu) | Description |
|----------------|-------------------------|-------------|
| `gtkmm-4.0` | `libgtkmm-4.0-dev` | GTK4 C++ bindings |
| `gtk4-layer-shell` | `libgtk-4-layer-shell-dev` | Wayland layer shell |
| `lightdm` | `lightdm` | Display manager |
| `liblightdm-gobject-1` | `liblightdm-gobject-1-dev` | LightDM API |
| `vte4` | `libvte-2.91-gtk4-dev` | Terminal emulator |
| `gdk-pixbuf2` | `libgdk-pixbuf-2.0-dev` | Image loading |
| `wayland` | `libwayland-dev` | Wayland protocol |
| `nlohmann-json` | `nlohmann-json3-dev` | JSON parser |
| `swww` | `swww` | Wallpaper |
| `mako` | `mako-notifier` | Notifications |
| `dex` | `dex` | XDG autostart |
| `grim` | `grim` | Screenshots |
| `slurp` | `slurp` | Region selection |
| `wl-clipboard` | `wl-clipboard` | Wayland clipboard |

## LightDM configuration

The installer automatically configures LightDM to use `hyalo-greeter`. Configuration is saved to:

```
/etc/lightdm/lightdm.conf.d/50-hyalo-greeter.conf
```

To manually change the greeter:

```bash
sudo nano /etc/lightdm/lightdm.conf.d/50-hyalo-greeter.conf
```

Contents:

```ini
[Seat:*]
greeter-session=hyalo-greeter
user-session=hyalo
```

## Components

| Component | Description |
|-----------|-------------|
| **hyalo-panel** | Panel/taskbar with launcher, clock, tray |
| **hyalo-greeter** | LightDM login screen |
| **hyalo-control-center** | Settings center |
| **hyalo-terminal** | Terminal emulator |
| **hyalo-files** | File manager |
| **hyalo-wallpaper** | Wallpaper setter |
| **hyalo-software-store** | Software store |
| **hyalo-update-center** | Update center |

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| `Super+Space` | Open launcher |
| `Print` | Full screenshot |
| `Shift+Print` | Area screenshot |

## Uninstall

```bash
sudo ./install-hyalode.sh --uninstall
```

Or via pacman:

```bash
sudo pacman -Rns hyalode
```

## Project structure

```
├── apps/                    # HyaloDE applications
│   ├── hyalo-panel/         # Main panel
│   ├── hyalo-greeter/       # LightDM greeter
│   ├── hyalo-control-center/# Settings center
│   ├── hyalo-terminal/      # Terminal
│   ├── hyalo-files/         # File manager
│   ├── hyalo-wallpaper/     # Wallpaper
│   ├── hyalo-software-store/# Software store
│   └── hyalo-update-center/ # Updates
├── assets/                  # Icons, themes, decorations
├── config/                  # Default configuration
├── libs/hyalo-core/         # Shared runtime library
├── packaging/               # PKGBUILD and installer
│   ├── arch/PKGBUILD        # Arch Linux (stable)
│   ├── arch/PKGBUILD-git    # Arch Linux (git)
│   └── install-hyalode.sh   # Universal installer
└── compositor/              # HyaloWM (labwc fork)
```

## License

MIT

## Author

[MijSys](https://github.com/mijsys)
