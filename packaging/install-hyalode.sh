#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────
# HyaloDE Installer — https://github.com/mijsys/HyaloDE
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/mijsys/HyaloDE/main/install-hyalode.sh | bash
#   or:
#   git clone https://github.com/mijsys/HyaloDE.git && cd HyaloDE && ./install-hyalode.sh
#
# Options:
#   --add-repo      Add the HyaloDE pacman repository (Arch Linux)
#   --remove-repo   Remove the HyaloDE pacman repository
#   --from-source   Build from source instead of using the pacman repo
#   --uninstall     Remove HyaloDE completely
#   --help          Show this help
# ──────────────────────────────────────────────────────────────

set -euo pipefail

# ── Colors ──
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

REPO_URL="https://github.com/mijsys/HyaloDE"
REPO_NAME="hyalode"
REPO_SERVER="https://mijsys.github.io/HyaloDE/repo/\$arch"
REPO_SERVER_FALLBACK="https://raw.githubusercontent.com/mijsys/HyaloDE/gh-pages/repo/\$arch"
PACMAN_CONF="/etc/pacman.conf"

# ── Helpers ──
info()  { printf "${CYAN}:: ${NC}%s\n" "$*"; }
ok()    { printf "${GREEN}:: ${NC}%s\n" "$*"; }
warn()  { printf "${YELLOW}:: ${NC}%s\n" "$*"; }
err()   { printf "${RED}:: ${NC}%s\n" "$*" >&2; }

have_cmd() { command -v "$1" >/dev/null 2>&1; }

repo_arch() {
    local arch
    arch="$(uname -m)"
    case "$arch" in
        x86_64|aarch64) echo "$arch" ;;
        *) echo "x86_64" ;;
    esac
}

repo_db_url() {
    local arch server
    server="${1:-$REPO_SERVER}"
    arch="$(repo_arch)"
    server="${server//\$arch/$arch}"
    echo "${server%/}/hyalode.db"
}

repo_server_reachable() {
    local server db_url
    server="${1:-$REPO_SERVER}"
    db_url="$(repo_db_url "$server")"

    if have_cmd curl; then
        curl -fsL --max-time 12 -o /dev/null "$db_url"
        return $?
    fi

    if have_cmd wget; then
        wget -q --spider --timeout=12 "$db_url"
        return $?
    fi

    warn "Brak curl/wget, pomijam test dostępności repozytorium: $db_url"
    return 0
}

select_repo_server() {
    if repo_server_reachable "$REPO_SERVER"; then
        echo "$REPO_SERVER"
        return 0
    fi

    if repo_server_reachable "$REPO_SERVER_FALLBACK"; then
        echo "$REPO_SERVER_FALLBACK"
        return 0
    fi

    return 1
}

need_root() {
    if [ "$(id -u)" -ne 0 ]; then
        err "To wykonanie wymaga uprawnień root. Uruchom z sudo."
        exit 1
    fi
}

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "${ID:-unknown}"
    elif have_cmd pacman; then
        echo "arch"
    elif have_cmd apt-get; then
        echo "debian"
    elif have_cmd dnf; then
        echo "fedora"
    else
        echo "unknown"
    fi
}

# ── Banner ──
show_banner() {
    printf "${BOLD}${CYAN}"
    cat <<'BANNER'

    ██╗  ██╗██╗   ██╗ █████╗ ██╗      ██████╗ ██████╗ ███████╗
    ██║  ██║╚██╗ ██╔╝██╔══██╗██║     ██╔═══██╗██╔══██╗██╔════╝
    ███████║ ╚████╔╝ ███████║██║     ██║   ██║██║  ██║█████╗
    ██╔══██║  ╚██╔╝  ██╔══██║██║     ██║   ██║██║  ██║██╔══╝
    ██║  ██║   ██║   ██║  ██║███████╗╚██████╔╝██████╔╝███████╗
    ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═════╝ ╚══════╝

BANNER
    printf "${NC}"
    printf "    ${BOLD}Modern Wayland Desktop Environment${NC}\n"
    printf "    ${CYAN}%s${NC}\n\n" "$REPO_URL"
}

usage() {
    cat <<'EOF'
Użycie: ./install-hyalode.sh [opcje]

Opcje:
  --add-repo      Dodaj repozytorium HyaloDE do pacmana (Arch Linux)
  --remove-repo   Usuń repozytorium HyaloDE z pacmana
  --from-source   Zbuduj ze źródeł zamiast z repozytorium
  --uninstall     Całkowicie usuń HyaloDE
  --help          Pokaż tę pomoc
EOF
}

# ══════════════════════════════════════════════════════════════
# Pacman repository management
# ══════════════════════════════════════════════════════════════

add_pacman_repo() {
    need_root

    local selected_server db_url
    if ! selected_server="$(select_repo_server)"; then
        db_url="$(repo_db_url "$REPO_SERVER")"
        err "Repozytorium pacman jest niedostępne: ${db_url}"
        warn "Wygląda na to, że baza pacmana nie została opublikowana (HTTP 404)."
        warn "Użyj instalacji ze źródeł: sudo ./install-hyalode.sh --from-source"
        return 1
    fi

    if [ "$selected_server" != "$REPO_SERVER" ]; then
        warn "GitHub Pages niedostępne; używam fallback: $selected_server"
    fi

    if grep -q "^\[${REPO_NAME}\]" "$PACMAN_CONF" 2>/dev/null; then
        ok "Repozytorium [${REPO_NAME}] już istnieje w ${PACMAN_CONF}"
        return 0
    fi

    info "Dodawanie repozytorium [${REPO_NAME}] do ${PACMAN_CONF}..."

    cat >> "$PACMAN_CONF" <<EOF

[${REPO_NAME}]
SigLevel = Optional TrustAll
Server = ${selected_server}
EOF

    ok "Repozytorium dodane. Synchronizacja bazy pakietów..."
    if ! pacman -Sy; then
        err "Synchronizacja pacmana nie powiodła się. Cofam wpis repozytorium [${REPO_NAME}]..."
        sed -i "/^\[${REPO_NAME}\]/,/^$/d" "$PACMAN_CONF"
        return 1
    fi
    ok "Gotowe! Teraz możesz zainstalować: sudo pacman -S hyalode"
}

remove_pacman_repo() {
    need_root

    if ! grep -q "^\[${REPO_NAME}\]" "$PACMAN_CONF" 2>/dev/null; then
        warn "Repozytorium [${REPO_NAME}] nie znalezione w ${PACMAN_CONF}"
        return 0
    fi

    info "Usuwanie repozytorium [${REPO_NAME}] z ${PACMAN_CONF}..."

    # Remove the repo block (header + SigLevel + Server + blank lines)
    sed -i "/^\[${REPO_NAME}\]/,/^$/d" "$PACMAN_CONF"

    ok "Repozytorium usunięte."
    pacman -Sy
}

# ══════════════════════════════════════════════════════════════
# Dependency installation
# ══════════════════════════════════════════════════════════════

install_deps_arch() {
    info "Instalacja zależności (Arch Linux)..."
    pacman -Sy --needed --noconfirm \
        base-devel cmake meson ninja pkg-config git \
        gtkmm-4.0 gtk4-layer-shell \
        lightdm \
        vte4 gdk-pixbuf2 \
        wayland wayland-protocols wlr-protocols \
        nlohmann-json \
        swww mako dex \
        grim slurp wl-clipboard \
        ttf-font-awesome \
        || true
}

install_deps_debian() {
    info "Instalacja zależności (Debian/Ubuntu)..."
    apt-get update
    apt-get install -y \
        build-essential cmake meson ninja-build pkg-config git \
        libgtkmm-4.0-dev libgtk-4-layer-shell-dev \
        lightdm liblightdm-gobject-1-dev \
        libvte-2.91-gtk4-dev libgdk-pixbuf-2.0-dev \
        libwayland-dev wayland-protocols \
        nlohmann-json3-dev \
        swww mako-notifier dex \
        grim slurp wl-clipboard \
        fonts-font-awesome \
        || true
}

install_deps_fedora() {
    info "Instalacja zależności (Fedora)..."
    dnf install -y \
        @development-tools cmake meson ninja-build pkg-config git \
        gtkmm4.0-devel gtk4-layer-shell-devel \
        lightdm lightdm-gobject-1-devel \
        vte291-gtk4-devel gdk-pixbuf2-devel \
        wayland-devel wayland-protocols-devel \
        json-devel \
        swww mako dex \
        grim slurp wl-clipboard \
        fontawesome-fonts \
        || true
}

install_deps() {
    local distro
    distro=$(detect_distro)

    case "$distro" in
        arch|manjaro|endeavouros|garuda|cachyos)
            install_deps_arch ;;
        debian|ubuntu|linuxmint|pop)
            install_deps_debian ;;
        fedora)
            install_deps_fedora ;;
        *)
            warn "Nieznana dystrybucja: ${distro}"
            warn "Zainstaluj zależności ręcznie (patrz README.md)"
            ;;
    esac
}

# ══════════════════════════════════════════════════════════════
# Install from pacman repo (Arch only)
# ══════════════════════════════════════════════════════════════

install_from_repo() {
    need_root
    local distro
    distro=$(detect_distro)

    case "$distro" in
        arch|manjaro|endeavouros|garuda|cachyos)
            if ! add_pacman_repo; then
                warn "Instalacja z repozytorium nie jest obecnie możliwa."
                warn "Przechodzę do instalacji ze źródeł..."
                install_from_source
                return
            fi
            info "Instalacja HyaloDE z repozytorium..."
            if ! pacman -S --noconfirm hyalode; then
                warn "Instalacja pakietu hyalode nie powiodła się."
                warn "Przechodzę do instalacji ze źródeł..."
                install_from_source
                return
            fi
            ;;
        *)
            warn "Instalacja z repozytorium dostępna tylko dla Arch Linux."
            warn "Używam trybu budowania ze źródeł..."
            install_from_source
            return
            ;;
    esac

    configure_lightdm
    ok "HyaloDE zainstalowane pomyślnie!"
    post_install_message
}

# ══════════════════════════════════════════════════════════════
# Install from source
# ══════════════════════════════════════════════════════════════

install_from_source() {
    need_root
    install_deps

    local src_dir
    if [ -f "CMakeLists.txt" ] && grep -q "HyaloOS" "CMakeLists.txt" 2>/dev/null; then
        src_dir="$(pwd)"
        info "Używam istniejących źródeł w ${src_dir}"
    else
        src_dir="/tmp/hyalode-src"
        info "Klonowanie repozytorium HyaloDE..."
        rm -rf "$src_dir"
        git clone --depth 1 "$REPO_URL" "$src_dir"
    fi

    cd "$src_dir"

    info "Budowanie i instalacja pełnego stosu HyaloDE (aplikacje + compositor)..."
    bash ./install.sh \
        --system \
        --prefix /usr \
        --build-dir "$src_dir/build-install" \
        --compositor-build-dir "$src_dir/compositor/hyalo-compositor/build-install"

    configure_lightdm
    ok "HyaloDE zbudowane i zainstalowane pomyślnie!"
    post_install_message
}

# ══════════════════════════════════════════════════════════════
# LightDM configuration
# ══════════════════════════════════════════════════════════════

configure_lightdm() {
    if ! have_cmd lightdm; then
        warn "LightDM nie jest zainstalowany. Pomiń konfigurację greetera."
        return 0
    fi

    info "Konfiguracja LightDM dla hyalo-greeter..."

    # Greeter desktop entry
    if [ -f /usr/share/xgreeters/hyalo-greeter.desktop ] || [ -f /usr/bin/hyalo-greeter ]; then
        mkdir -p /etc/lightdm/lightdm.conf.d
        cat > /etc/lightdm/lightdm.conf.d/50-hyalo-greeter.conf <<EOF
# HyaloDE LightDM configuration — auto-generated
[Seat:*]
greeter-session=hyalo-greeter
user-session=hyalo
EOF
        ok "LightDM skonfigurowany do używania hyalo-greeter"

        # Ensure the greeter desktop file is in xgreeters
        if [ ! -f /usr/share/xgreeters/hyalo-greeter.desktop ] && [ -f /usr/share/applications/hyalo-greeter.desktop ]; then
            cp /usr/share/applications/hyalo-greeter.desktop /usr/share/xgreeters/hyalo-greeter.desktop
        fi
    else
        warn "hyalo-greeter nie znaleziony. LightDM użyje domyślnego greetera."
    fi
}

# ══════════════════════════════════════════════════════════════
# Uninstall
# ══════════════════════════════════════════════════════════════

uninstall_hyalode() {
    need_root
    local distro
    distro=$(detect_distro)

    info "Usuwanie HyaloDE..."

    case "$distro" in
        arch|manjaro|endeavouros|garuda|cachyos)
            if pacman -Qi hyalode >/dev/null 2>&1; then
                pacman -Rns --noconfirm hyalode || true
            fi
            if pacman -Qi hyalode-git >/dev/null 2>&1; then
                pacman -Rns --noconfirm hyalode-git || true
            fi
            ;;
    esac

    # Remove files that may have been installed from source
    local files_to_remove=(
        /usr/bin/hyalo-panel
        /usr/bin/hyalo-control-center
        /usr/bin/hyalo-terminal
        /usr/bin/hyalo-wallpaper
        /usr/bin/hyalo-wallpaperd
        /usr/bin/hyalo-screenshot
        /usr/bin/hyalo-driver-manager
        /usr/bin/hyalo-greeter
        /usr/bin/labwc
        /usr/bin/labnag
        /usr/bin/lab-sensible-terminal
        /usr/libexec/hyalo/hyalo-session
        /usr/lib/hyalo/libseat.so
        /usr/lib/hyalo/libseat.so.1
        /usr/lib/hyalo/libdisplay-info.so
        /usr/lib/hyalo/libdisplay-info.so.4
        /usr/lib/hyalo/libdisplay-info.so.0.4.0
        /usr/lib/hyalo/libliftoff.so
        /usr/lib/hyalo/libliftoff.so.0
        /usr/lib/hyalo/libliftoff.so.0.6.0
        /usr/share/wayland-sessions/hyalo.desktop
        /usr/share/xgreeters/hyalo-greeter.desktop
        /etc/lightdm/lightdm.conf.d/50-hyalo-greeter.conf
    )

    for f in "${files_to_remove[@]}"; do
        [ -f "$f" ] && rm -f "$f" && info "Usunięto: $f"
    done

    # Remove directories
    rm -rf /usr/share/hyalo 2>/dev/null || true
    rm -rf /usr/share/themes/HyaloOS 2>/dev/null || true
    rmdir /usr/libexec/hyalo 2>/dev/null || true

    # Remove pacman repo if present
    if grep -q "^\[${REPO_NAME}\]" "$PACMAN_CONF" 2>/dev/null; then
        remove_pacman_repo
    fi

    ok "HyaloDE zostało usunięte."
}

# ══════════════════════════════════════════════════════════════
# Post-install message
# ══════════════════════════════════════════════════════════════

post_install_message() {
    printf "\n"
    printf "${BOLD}${GREEN}╔══════════════════════════════════════════════╗${NC}\n"
    printf "${BOLD}${GREEN}║     HyaloDE zainstalowane pomyślnie!        ║${NC}\n"
    printf "${BOLD}${GREEN}╚══════════════════════════════════════════════╝${NC}\n"
    printf "\n"
    printf "  ${BOLD}Następne kroki:${NC}\n"
    printf "  1. Wyloguj się z bieżącej sesji\n"
    printf "  2. Na ekranie logowania LightDM wybierz sesję ${BOLD}HyaloDE${NC}\n"
    printf "  3. Zaloguj się jak zwykle\n"
    printf "\n"
    printf "  ${CYAN}Wskazówka:${NC} LightDM automatycznie używa hyalo-greeter\n"
    printf "  jako ekran logowania. Aby zmienić greeter:\n"
    printf "    sudo nano /etc/lightdm/lightdm.conf.d/50-hyalo-greeter.conf\n"
    printf "\n"
    printf "  ${CYAN}Skróty klawiszowe:${NC}\n"
    printf "    Super+Space  →  Launcher\n"
    printf "    Print        →  Zrzut ekranu\n"
    printf "    Shift+Print  →  Zaznacz obszar\n"
    printf "\n"
}

# ══════════════════════════════════════════════════════════════
# Interactive menu
# ══════════════════════════════════════════════════════════════

interactive_menu() {
    show_banner

    local distro
    distro=$(detect_distro)

    printf "  ${BOLD}Wykryto system:${NC} %s\n\n" "$distro"
    printf "  ${BOLD}Wybierz opcję:${NC}\n\n"
    printf "  ${CYAN}1)${NC} Zainstaluj HyaloDE (z repozytorium pacman)  ${GREEN}[zalecane dla Arch]${NC}\n"
    printf "  ${CYAN}2)${NC} Zainstaluj HyaloDE (budowanie ze źródeł)\n"
    printf "  ${CYAN}3)${NC} Dodaj repozytorium HyaloDE do pacmana\n"
    printf "  ${CYAN}4)${NC} Usuń repozytorium HyaloDE z pacmana\n"
    printf "  ${CYAN}5)${NC} Odinstaluj HyaloDE\n"
    printf "  ${CYAN}0)${NC} Wyjście\n"
    printf "\n"

    local choice
    printf "  Twój wybór [0-5]: "
    read -r choice

    case "$choice" in
        1) install_from_repo ;;
        2) install_from_source ;;
        3) need_root; add_pacman_repo ;;
        4) need_root; remove_pacman_repo ;;
        5) uninstall_hyalode ;;
        0) printf "Do zobaczenia!\n"; exit 0 ;;
        *)
            err "Nieprawidłowy wybór: $choice"
            exit 1
            ;;
    esac
}

# ══════════════════════════════════════════════════════════════
# Main
# ══════════════════════════════════════════════════════════════

main() {
    if [ "$#" -eq 0 ]; then
        interactive_menu
        return
    fi

    case "$1" in
        --add-repo)
            show_banner
            need_root
            add_pacman_repo
            ;;
        --remove-repo)
            show_banner
            need_root
            remove_pacman_repo
            ;;
        --from-source)
            show_banner
            install_from_source
            ;;
        --uninstall)
            show_banner
            uninstall_hyalode
            ;;
        --help|-h)
            show_banner
            usage
            ;;
        *)
            err "Nieznana opcja: $1"
            usage >&2
            exit 1
            ;;
    esac
}

main "$@"
