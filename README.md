# HyaloDE

**Nowoczesne środowisko graficzne Wayland** zbudowane w C++20 z GTK4.

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
- Uruchamia `hyalo-session` z panelem, powiadomieniami (mako), tapetami (swww) i autostarter (dex)

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
| nlohmann-json | nlohmann-json3-dev | JSON (CMake pobiera automatycznie jeśli brak) |
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

## Licencja

MIT

## Autor

[MijSys](https://github.com/mijsys)
