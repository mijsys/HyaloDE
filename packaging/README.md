# HyaloDE — Modern Wayland Desktop Environment

<p align="center">
  <img src="https://raw.githubusercontent.com/mijsys/HyaloDE/main/assets/banner.png" alt="HyaloDE" width="600">
</p>

**HyaloDE** to nowoczesne środowisko graficzne oparte na Wayland, zbudowane w C++ z GTK4. Zawiera własny panel, launcher, control center, terminal, file manager, greeter LightDM i wiele więcej.

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
| `swww` | `swww` | Tło pulpitu (wallpaper daemon) |
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

## Licencja

MIT

## Autor

[MijSys](https://github.com/mijsys)
