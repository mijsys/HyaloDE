#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./deinstall.sh [options]

Remove LuminaDE/HyaloOS installation and optional dependencies.

Options:
  --system                 Remove a system install (default).
  --user                   Remove a user-local install (~/.local by default).
  --prefix PATH            Prefix that was used during install.
  --session-dir PATH       Session directory that was used during install.
  --libexec-dir PATH       Libexec directory that was used during install.
  --purge-deps             Also uninstall known Hyalo/LuminaDE dependencies.
  --yes                    Skip interactive confirmation.
  --help                   Show this help.

Examples:
  sudo ./deinstall.sh --system --purge-deps --yes
  ./deinstall.sh --user --yes
EOF
}

have_command() {
    command -v "$1" >/dev/null 2>&1
}

remove_installed_pacman_packages() {
    installed_pkgs=()

    for pkg in "$@"; do
        if pacman -Q "$pkg" >/dev/null 2>&1; then
            installed_pkgs+=("$pkg")
        fi
    done

    if [ "${#installed_pkgs[@]}" -eq 0 ]; then
        printf '==> No matching pacman dependencies are currently installed.\n'
        return 0
    fi

    pacman -Rns --noconfirm "${installed_pkgs[@]}"
}

MODE=system
PREFIX=/usr/local
SESSION_DIR=
LIBEXEC_DIR=
PURGE_DEPS=0
ASSUME_YES=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --system)
            MODE=system
            PREFIX=/usr/local
            ;;
        --user)
            MODE=user
            PREFIX="${HOME}/.local"
            ;;
        --prefix)
            shift
            PREFIX=$1
            ;;
        --session-dir)
            shift
            SESSION_DIR=$1
            ;;
        --libexec-dir)
            shift
            LIBEXEC_DIR=$1
            ;;
        --purge-deps)
            PURGE_DEPS=1
            ;;
        --yes)
            ASSUME_YES=1
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n\n' "$1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

if [ -z "$SESSION_DIR" ]; then
    if [ "$MODE" = system ]; then
        SESSION_DIR=/usr/share/wayland-sessions
    else
        SESSION_DIR="${HOME}/.local/share/wayland-sessions"
    fi
fi

if [ -z "$LIBEXEC_DIR" ]; then
    LIBEXEC_DIR="${PREFIX}/libexec/hyalo"
fi

if [ "$MODE" = system ] && [ "$(id -u)" -ne 0 ]; then
    printf 'System deinstallation requires root privileges. Run with sudo.\n' >&2
    exit 1
fi

if [ "$ASSUME_YES" -ne 1 ]; then
    printf 'About to remove LuminaDE/HyaloOS from prefix: %s\n' "$PREFIX"
    printf 'Session dir: %s\n' "$SESSION_DIR"
    printf 'Libexec dir: %s\n' "$LIBEXEC_DIR"
    if [ "$PURGE_DEPS" -eq 1 ]; then
        printf 'Dependency purge is ENABLED and can remove shared packages.\n'
    fi
    printf 'Continue? [y/N]: '
    read -r answer
    case "$answer" in
        y|Y|yes|YES)
            ;;
        *)
            printf 'Aborted.\n'
            exit 0
            ;;
    esac
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTALLER_SCRIPT="$SCRIPT_DIR/install.sh"

if [ ! -x "$INSTALLER_SCRIPT" ]; then
    printf 'Expected installer script not found or not executable: %s\n' "$INSTALLER_SCRIPT" >&2
    exit 1
fi

printf '==> Removing installed LuminaDE/HyaloOS files via install manifest\n'
"$INSTALLER_SCRIPT" \
    "--$MODE" \
    --remove \
    --prefix "$PREFIX" \
    --session-dir "$SESSION_DIR" \
    --libexec-dir "$LIBEXEC_DIR"

printf '==> Cleaning stale icon/theme leftovers\n'
rm -rf \
    "$PREFIX/share/icons/hyalo-icons" \
    "$PREFIX/share/hyalo/assets/icons/hyalo-icons" \
    "$PREFIX/share/themes/HyaloOS" \
    "$PREFIX/share/hyalo" \
    2>/dev/null || true

if [ "$MODE" = system ]; then
    rm -f /etc/lightdm/lightdm.conf.d/50-hyalo-greeter.conf 2>/dev/null || true
    rm -f /usr/share/xgreeters/hyalo-greeter.desktop 2>/dev/null || true
fi

if have_command gtk-update-icon-cache; then
    for icon_root in "$PREFIX/share/icons/hicolor" "$PREFIX/share/icons/hyalo-icons"; do
        if [ -d "$icon_root" ]; then
            gtk-update-icon-cache -f "$icon_root" >/dev/null 2>&1 || true
        fi
    done
fi

if have_command update-desktop-database && [ -d "$PREFIX/share/applications" ]; then
    update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
fi

purge_dependencies() {
    printf '==> Purging known LuminaDE/HyaloOS dependencies\n'

    if have_command pacman; then
        remove_installed_pacman_packages \
            cmake meson ninja pkg-config \
            gtkmm-4.0 gtk4-layer-shell \
            lightdm liblightdm-gobject-1 \
            vte4 gdk-pixbuf2 \
            wayland wayland-protocols wlr-protocols \
            nlohmann-json \
            swww mako dex \
            grim slurp wl-clipboard \
            ttf-font-awesome otf-font-awesome \
            ttf-roboto ttf-liberation
        return
    fi

    if have_command apt-get; then
        apt-get purge -y \
            cmake meson ninja-build pkg-config \
            libgtkmm-4.0-dev libgtk-4-layer-shell-dev \
            lightdm liblightdm-gobject-1-dev \
            libvte-2.91-gtk4-dev libgdk-pixbuf-2.0-dev \
            libwayland-dev wayland-protocols libgiounix-2.0-cil-dev \
            nlohmann-json3-dev \
            swww mako-notifier dex \
            grim slurp wl-clipboard \
            fonts-font-awesome \
            fonts-roboto fonts-liberation2 \
            || true
        apt-get autoremove -y || true
        return
    fi

    if have_command dnf; then
        dnf remove -y \
            cmake meson ninja-build pkg-config \
            gtkmm4.0-devel gtk4-layer-shell-devel \
            lightdm lightdm-gobject-1-devel \
            vte291-gtk4-devel gdk-pixbuf2-devel \
            wayland-devel wayland-protocols-devel \
            json-devel \
            swww mako dex \
            grim slurp wl-clipboard \
            fontawesome-fonts \
            google-roboto-fonts liberation-sans-fonts \
            || true
        return
    fi

    if have_command zypper; then
        zypper --non-interactive rm -u \
            cmake meson ninja pkg-config \
            gtkmm4-devel gtk4-layer-shell-devel \
            lightdm liblightdm-gobject-1-0 lightdm-devel \
            vte-devel gdk-pixbuf-devel \
            wayland-devel wayland-protocols-devel \
            nlohmann_json-devel \
            swww mako dex \
            grim slurp wl-clipboard \
            google-roboto-fonts liberation-fonts \
            || true
        return
    fi

    printf '==> No supported package manager found for dependency purge.\n'
}

if [ "$PURGE_DEPS" -eq 1 ]; then
    if [ "$(id -u)" -ne 0 ]; then
        printf 'Dependency purge requires root privileges. Re-run with sudo.\n' >&2
        exit 1
    fi
    purge_dependencies
fi

printf '==> Deinstallation finished successfully\n'