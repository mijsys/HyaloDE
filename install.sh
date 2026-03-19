#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./install.sh [options]

Build, install, or remove HyaloOS applications, HyaloWM compositor artifacts, and a display-manager-visible Wayland session.

Options:
  --system                 Install for the whole system. Requires root for default paths.
  --user                   Install into the current user's home for testing.
    --update                 Remove the current HyaloOS install in the target prefix first, then reinstall it.
    --remove                 Remove files previously installed by this script.
  --prefix PATH            Install binaries and shared data under PATH.
    --build-dir PATH         CMake build directory for HyaloOS apps.
  --compositor-build-dir PATH
                           Meson build directory for hyalo-compositor.
  --session-dir PATH       Directory for the installed Wayland session desktop entry.
    --libexec-dir PATH       Directory for the installed hyalo-session wrapper.
    --skip-compositor        Skip Meson build/install of HyaloWM.
  --help                   Show this help.

Notes:
  - In --system mode the default prefix is /usr/local and the session entry is installed to /usr/share/wayland-sessions.
  - In --user mode the default prefix is ~/.local and the session entry is installed to ~/.local/share/wayland-sessions.
    - Normal install and --update both refresh the existing install in the selected prefix/session location before writing new files, so stale HyaloOS programs, wrappers, and session entries are replaced instead of piling up.
  - SDDM/LightDM usually expect a system-visible session entry, so --system is the normal mode for real logins.
    - The generated session wrapper autostarts hyalo-panel and, if dex is installed, also runs XDG autostart entries for HyaloOS.
        - If `swww` is missing, install.sh attempts automatic installation during root/system installs.
    - The installer records a manifest under PREFIX/share/hyalo/install-manifest.txt for later removal.
EOF
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing required command: %s\n' "$1" >&2
        exit 1
    fi
}

have_command() {
    command -v "$1" >/dev/null 2>&1
}

install_packages() {
    if [ -n "${DESTDIR:-}" ]; then
        printf '==> DESTDIR is set; skipping host package installation.\n'
        return 0
    fi

    if [ "$(id -u)" -ne 0 ]; then
        printf '==> Not running as root; skipping automatic dependency installation.\n'
        printf '    Install missing packages manually (see README.md for the full list).\n'
        return 0
    fi

    printf '==> Installing build and runtime dependencies\n'

    if have_command pacman; then
        # ── Arch Linux / Manjaro ──
        pacman -Sy --needed --noconfirm \
            base-devel cmake meson ninja pkg-config \
            gtkmm-4.0 gtk4-layer-shell \
            lightdm liblightdm-gobject-1 \
            vte4 gdk-pixbuf2 \
            wayland wayland-protocols wlr-protocols \
            nlohmann-json \
            swww mako dex \
            grim slurp wl-clipboard \
            ttf-font-awesome otf-font-awesome \
            || true
    elif have_command apt-get; then
        # ── Debian / Ubuntu ──
        apt-get update
        apt-get install -y \
            build-essential cmake meson ninja-build pkg-config \
            libgtkmm-4.0-dev libgtk-4-layer-shell-dev \
            lightdm liblightdm-gobject-1-dev \
            libvte-2.91-gtk4-dev libgdk-pixbuf-2.0-dev \
            libwayland-dev wayland-protocols libgiounix-2.0-cil-dev \
            nlohmann-json3-dev \
            swww mako-notifier dex \
            grim slurp wl-clipboard \
            fonts-font-awesome \
            || true
    elif have_command dnf; then
        # ── Fedora ──
        dnf install -y \
            @development-tools cmake meson ninja-build pkg-config \
            gtkmm4.0-devel gtk4-layer-shell-devel \
            lightdm lightdm-gobject-1-devel \
            vte291-gtk4-devel gdk-pixbuf2-devel \
            wayland-devel wayland-protocols-devel \
            json-devel \
            swww mako dex \
            grim slurp wl-clipboard \
            fontawesome-fonts \
            || true
    elif have_command zypper; then
        # ── openSUSE ──
        zypper --non-interactive install \
            -t pattern devel_basis \
            cmake meson ninja pkg-config \
            gtkmm4-devel gtk4-layer-shell-devel \
            lightdm liblightdm-gobject-1-0 lightdm-devel \
            vte-devel gdk-pixbuf-devel \
            wayland-devel wayland-protocols-devel \
            nlohmann_json-devel \
            swww mako dex \
            grim slurp wl-clipboard \
            || true
    else
        printf '==> No supported package manager found (pacman/apt/dnf/zypper).\n'
        printf '    Please install dependencies manually.\n'
        return 0
    fi

    printf '==> Dependency installation finished\n'
}

install_swww_if_missing() {
    if have_command swww && have_command swww-daemon; then
        return 0
    fi

    if [ -n "${DESTDIR:-}" ]; then
        printf '==> swww is missing, but DESTDIR is set; skipping host package installation.\n'
        return 0
    fi

    if [ "$(id -u)" -ne 0 ]; then
        printf '==> swww is missing and install.sh is not running as root; skipping automatic package installation.\n'
        printf '    Install package `swww` manually to enable the wallpaper daemon backend.\n'
        return 0
    fi

    printf '==> swww is missing, attempting automatic installation\n'

    if have_command apt-get; then
        apt-get update
        apt-get install -y swww
    elif have_command dnf; then
        dnf install -y swww
    elif have_command pacman; then
        pacman -Sy --noconfirm swww
    elif have_command zypper; then
        zypper --non-interactive install swww
    elif have_command apk; then
        apk add swww
    else
        printf '==> Could not auto-install swww: no supported package manager found (apt/dnf/pacman/zypper/apk).\n'
        return 0
    fi

    if have_command swww && have_command swww-daemon; then
        printf '==> swww installed successfully\n'
    else
        printf '==> Installation command finished, but `swww` or `swww-daemon` is still missing in PATH.\n'
    fi
}

ensure_writable_build_dir() {
    build_dir=$1
    fallback_dir=$2

    if [ -e "$build_dir" ] && [ ! -w "$build_dir" ]; then
        printf 'Build directory %s is not writable; using %s instead.\n' "$build_dir" "$fallback_dir"
        printf '%s\n' "$fallback_dir"
        return
    fi

    build_parent=$(dirname "$build_dir")
    if [ ! -e "$build_dir" ] && [ ! -w "$build_parent" ]; then
        printf 'Cannot create build directory %s; using %s instead.\n' "$build_dir" "$fallback_dir"
        printf '%s\n' "$fallback_dir"
        return
    fi

    printf '%s\n' "$build_dir"
}

prepare_compositor_build_dir() {
    build_dir=$1
    cmd_line_path=$build_dir/meson-private/cmd_line.txt

    if [ -f "$cmd_line_path" ] && ! grep -q 'hyalo-session-integration' "$cmd_line_path"; then
        printf 'Meson build directory %s contains legacy pre-Hyalo options; recreating it for HyaloWM.\n' "$build_dir"
        rm -rf "$build_dir"
    fi
}

manifest_tmp=

cleanup() {
    if [ -n "${manifest_tmp:-}" ] && [ -f "$manifest_tmp" ]; then
        rm -f "$manifest_tmp"
    fi
}

trap cleanup EXIT

start_manifest() {
    manifest_tmp=$(mktemp)
}

record_manifest_path() {
    printf '%s\n' "$1" >> "$manifest_tmp"
}

append_manifest_file() {
    manifest_path=$1

    if [ -f "$manifest_path" ]; then
        sed '/^$/d' "$manifest_path" >> "$manifest_tmp"
    fi
}

append_meson_intro_installed() {
    intro_path=$1

    if [ -f "$intro_path" ]; then
        sed -n 's/^[[:space:]]*"[^"]*"[[:space:]]*:[[:space:]]*"\([^"]*\)"[[:space:]]*,\{0,1\}[[:space:]]*$/\1/p' "$intro_path" >> "$manifest_tmp"
    fi
}

install_tracked_file() {
    source_path=$1
    destination_path=$2
    file_mode=$3

    install -D -m "$file_mode" "$source_path" "$destination_path"
    record_manifest_path "$destination_path"
}

install_optional_tracked_file() {
    source_path=$1
    destination_path=$2
    file_mode=$3

    if [ -f "$source_path" ]; then
        install_tracked_file "$source_path" "$destination_path" "$file_mode"
    fi
}

install_tracked_symlink() {
    symlink_target=$1
    symlink_path=$2

    mkdir -p "$(dirname "$symlink_path")"
    ln -sfn "$symlink_target" "$symlink_path"
    record_manifest_path "$symlink_path"
}

write_manifest() {
    manifest_path=$1

    mkdir -p "$(dirname "$manifest_path")"
    sort -u "$manifest_tmp" > "$manifest_path"
}

remove_paths_from_manifest() {
    manifest_path=$1

    while IFS= read -r installed_path; do
        [ -n "$installed_path" ] || continue

        if [ -L "$installed_path" ] || [ -f "$installed_path" ]; then
            rm -f "$installed_path"
        fi
    done < "$manifest_path"
}

remove_dir_if_empty() {
    target_dir=$1

    if [ -d "$target_dir" ]; then
        rmdir "$target_dir" 2>/dev/null || true
    fi
}

append_manifest_file_to_list() {
    manifest_path=$1
    target_list=$2

    if [ -f "$manifest_path" ]; then
        sed '/^$/d' "$manifest_path" >> "$target_list"
    fi
}

append_meson_intro_installed_to_list() {
    intro_path=$1
    target_list=$2

    if [ -f "$intro_path" ]; then
        sed -n 's/^[[:space:]]*"[^"]*"[[:space:]]*:[[:space:]]*"\([^"]*\)"[[:space:]]*,\{0,1\}[[:space:]]*$/\1/p' "$intro_path" >> "$target_list"
    fi
}

record_path_to_list() {
    target_path=$1
    target_list=$2

    printf '%s\n' "$target_path" >> "$target_list"
}

append_current_install_fallback_paths() {
    target_list=$1

    record_path_to_list "$INSTALL_LIBEXEC_DIR/hyalo-session" "$target_list"
    record_path_to_list "$INSTALL_SESSION_DIR/hyalo.desktop" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/hyalo-panel" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/hyalo-control-center" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/hyalo-wallpaper" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/hyalo-wallpaperd" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/hyalo-screenshot" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/hyalo-terminal" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/labwc" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/labnag" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/bin/lab-sensible-terminal" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/share/xdg-desktop-portal/labwc-portals.conf" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/share/icons/hicolor/scalable/apps/labwc-symbolic.svg" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/share/icons/hicolor/scalable/apps/labwc.svg" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/share/themes/HyaloOS/openbox-3/themerc" "$target_list"
    record_path_to_list "$INSTALL_PREFIX_PATH/share/themes/HyaloOS/labwc" "$target_list"
    record_path_to_list "$INSTALL_LABWC_CONFIG_DIR/rc.xml" "$target_list"
    record_path_to_list "$INSTALL_MAKO_CONFIG_DIR/config" "$target_list"
}

remove_existing_install_artifacts() {
    target_manifest=$(mktemp)

    if [ -f "$MANIFEST_PATH" ]; then
        printf '==> Refreshing existing HyaloOS install from manifest %s\n' "$MANIFEST_PATH"
        append_manifest_file_to_list "$MANIFEST_PATH" "$target_manifest"
    else
        printf '==> No install manifest found at %s; removing known HyaloOS target files before reinstall\n' "$MANIFEST_PATH"
        append_manifest_file_to_list "$BUILD_DIR/install_manifest.txt" "$target_manifest"
        append_meson_intro_installed_to_list "$COMPOSITOR_BUILD_DIR/meson-info/intro-installed.json" "$target_manifest"
        append_current_install_fallback_paths "$target_manifest"
    fi

    sort -u "$target_manifest" -o "$target_manifest"
    remove_paths_from_manifest "$target_manifest"
    rm -f "$MANIFEST_PATH"
    rm -f "$target_manifest"

    remove_dir_if_empty "$INSTALL_STATE_DIR"
    remove_dir_if_empty "$INSTALL_LABWC_CONFIG_DIR"
    remove_dir_if_empty "$INSTALL_RUNTIME_LIB_DIR"
    remove_dir_if_empty "$INSTALL_LIBEXEC_DIR"
    remove_dir_if_empty "$(dirname "$INSTALL_LIBEXEC_DIR")"
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$SCRIPT_DIR

MODE=system
ACTION=install
PREFIX=/usr/local
BUILD_DIR=$PROJECT_ROOT/build
COMPOSITOR_BUILD_DIR=$PROJECT_ROOT/compositor/hyalo-compositor/build
SESSION_DIR=
LIBEXEC_DIR=
SKIP_COMPOSITOR=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --system)
            MODE=system
            ;;
        --user)
            MODE=user
            PREFIX=${HOME}/.local
            ;;
        --remove)
            ACTION=remove
            ;;
        --update)
            ACTION=update
            ;;
        --prefix)
            shift
            PREFIX=$1
            ;;
        --build-dir)
            shift
            BUILD_DIR=$1
            ;;
        --compositor-build-dir)
            shift
            COMPOSITOR_BUILD_DIR=$1
            ;;
        --session-dir)
            shift
            SESSION_DIR=$1
            ;;
        --libexec-dir)
            shift
            LIBEXEC_DIR=$1
            ;;
        --skip-compositor)
            SKIP_COMPOSITOR=1
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
        SESSION_DIR=${HOME}/.local/share/wayland-sessions
    fi
fi

if [ -z "$LIBEXEC_DIR" ]; then
    LIBEXEC_DIR=${PREFIX}/libexec/hyalo
fi

STATE_DIR=${PREFIX}/share/hyalo
RUNTIME_LIB_DIR=${PREFIX}/lib/hyalo
LABWC_CONFIG_DIR=${STATE_DIR}/labwc
MAKO_CONFIG_DIR=${STATE_DIR}/mako

DESTDIR_PREFIX=${DESTDIR:-}
BUILD_DIR=$(ensure_writable_build_dir "$BUILD_DIR" "$PROJECT_ROOT/build-user")
COMPOSITOR_BUILD_DIR=$(ensure_writable_build_dir "$COMPOSITOR_BUILD_DIR" "$PROJECT_ROOT/compositor/hyalo-compositor/build-user")
prepare_compositor_build_dir "$COMPOSITOR_BUILD_DIR"

INSTALL_PREFIX_PATH=${DESTDIR_PREFIX}${PREFIX}
INSTALL_SESSION_DIR=${DESTDIR_PREFIX}${SESSION_DIR}
INSTALL_LIBEXEC_DIR=${DESTDIR_PREFIX}${LIBEXEC_DIR}
INSTALL_STATE_DIR=${DESTDIR_PREFIX}${STATE_DIR}
INSTALL_RUNTIME_LIB_DIR=${DESTDIR_PREFIX}${RUNTIME_LIB_DIR}
INSTALL_LABWC_CONFIG_DIR=${DESTDIR_PREFIX}${LABWC_CONFIG_DIR}
INSTALL_MAKO_CONFIG_DIR=${DESTDIR_PREFIX}${MAKO_CONFIG_DIR}
MANIFEST_PATH=${INSTALL_STATE_DIR}/install-manifest.txt

LOG_FILE=$PROJECT_ROOT/install.log

if ! touch "$LOG_FILE" 2>/dev/null; then
    LOG_FILE=${TMPDIR:-/tmp}/hyalo-install-$(id -un).log
    : > "$LOG_FILE"
fi

exec > >(tee -a "$LOG_FILE") 2>&1

printf '=== HyaloOS install started at %s ===\n' "$(date '+%Y-%m-%d %H:%M:%S %z')"

if [ "$MODE" = system ] && [ "$(id -u)" -ne 0 ] && [ -z "$DESTDIR_PREFIX" ]; then
    printf 'System install needs root privileges for %s and %s.\n' "$SESSION_DIR" "$LIBEXEC_DIR" >&2
    printf 'Run with sudo, or use --user for a home-directory install.\n' >&2
    exit 1
fi

if [ "$ACTION" = remove ]; then
    start_manifest

    if [ -f "$MANIFEST_PATH" ]; then
        printf '==> Removing HyaloOS files from manifest %s\n' "$MANIFEST_PATH"
        append_manifest_file "$MANIFEST_PATH"
    else
        printf '==> No install manifest found at %s; falling back to current build metadata\n' "$MANIFEST_PATH"
        append_manifest_file "$BUILD_DIR/install_manifest.txt"
        append_meson_intro_installed "$COMPOSITOR_BUILD_DIR/meson-info/intro-installed.json"
        record_manifest_path "$INSTALL_LIBEXEC_DIR/hyalo-session"
        record_manifest_path "$INSTALL_SESSION_DIR/hyalo.desktop"
    fi

    sort -u "$manifest_tmp" -o "$manifest_tmp"
    remove_paths_from_manifest "$manifest_tmp"
    rm -f "$MANIFEST_PATH"

    remove_dir_if_empty "$INSTALL_STATE_DIR"
    remove_dir_if_empty "$INSTALL_LABWC_CONFIG_DIR"
    remove_dir_if_empty "$INSTALL_RUNTIME_LIB_DIR"
    remove_dir_if_empty "$INSTALL_LIBEXEC_DIR"
    remove_dir_if_empty "$(dirname "$INSTALL_LIBEXEC_DIR")"
    remove_dir_if_empty "$INSTALL_SESSION_DIR"

    printf '=== HyaloOS removal finished at %s ===\n' "$(date '+%Y-%m-%d %H:%M:%S %z')"
    printf 'Log written to %s\n' "$LOG_FILE"
    exit 0
fi

install_packages
require_command cmake
require_command meson
install_swww_if_missing

start_manifest

printf '==> Building HyaloOS applications with CMake\n'
if [ -f "$PROJECT_ROOT/tools/regenerate_hyalo_icons.sh" ]; then
    printf '==> Regenerating icon set (bash generator)\n'
    bash "$PROJECT_ROOT/tools/regenerate_hyalo_icons.sh"
fi
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR"

if [ "$SKIP_COMPOSITOR" -eq 0 ]; then
    printf '==> Building HyaloWM with Meson\n'
    if [ -d "$COMPOSITOR_BUILD_DIR" ]; then
        meson setup "$COMPOSITOR_BUILD_DIR" "$PROJECT_ROOT/compositor/hyalo-compositor" \
            --prefix "$PREFIX" \
            -Dhyalo-session-integration=false \
            --reconfigure
    else
        meson setup "$COMPOSITOR_BUILD_DIR" "$PROJECT_ROOT/compositor/hyalo-compositor" \
            --prefix "$PREFIX" \
            -Dhyalo-session-integration=false
    fi
    meson compile -C "$COMPOSITOR_BUILD_DIR"
fi

if [ "$ACTION" = update ] || [ "$ACTION" = install ]; then
    remove_existing_install_artifacts
fi

cmake --install "$BUILD_DIR"
append_manifest_file "$BUILD_DIR/install_manifest.txt"

if [ "$SKIP_COMPOSITOR" -eq 0 ]; then
    printf '==> Installing selected HyaloWM artifacts\n'
    install_tracked_file "$COMPOSITOR_BUILD_DIR/labwc" "$INSTALL_PREFIX_PATH/bin/labwc" 755
    mkdir -p "$INSTALL_RUNTIME_LIB_DIR"
    install_tracked_file \
        "$COMPOSITOR_BUILD_DIR/subprojects/seatd/libseat.so.1" \
        "$INSTALL_RUNTIME_LIB_DIR/libseat.so.1" \
        755
    install_optional_tracked_file \
        "$COMPOSITOR_BUILD_DIR/subprojects/libdisplay-info/libdisplay-info.so.0.4.0" \
        "$INSTALL_RUNTIME_LIB_DIR/libdisplay-info.so.0.4.0" \
        755
    install_optional_tracked_file \
        "$COMPOSITOR_BUILD_DIR/subprojects/libliftoff/libliftoff.so.0.6.0" \
        "$INSTALL_RUNTIME_LIB_DIR/libliftoff.so.0.6.0" \
        755
    install_tracked_symlink "libseat.so.1" "$INSTALL_RUNTIME_LIB_DIR/libseat.so"
    if [ -f "$COMPOSITOR_BUILD_DIR/subprojects/libdisplay-info/libdisplay-info.so.0.4.0" ]; then
        install_tracked_symlink "libdisplay-info.so.0.4.0" "$INSTALL_RUNTIME_LIB_DIR/libdisplay-info.so.4"
        install_tracked_symlink "libdisplay-info.so.4" "$INSTALL_RUNTIME_LIB_DIR/libdisplay-info.so"
    fi
    if [ -f "$COMPOSITOR_BUILD_DIR/subprojects/libliftoff/libliftoff.so.0.6.0" ]; then
        install_tracked_symlink "libliftoff.so.0.6.0" "$INSTALL_RUNTIME_LIB_DIR/libliftoff.so.0"
        install_tracked_symlink "libliftoff.so.0" "$INSTALL_RUNTIME_LIB_DIR/libliftoff.so"
    fi
    install_optional_tracked_file "$COMPOSITOR_BUILD_DIR/clients/labnag" "$INSTALL_PREFIX_PATH/bin/labnag" 755
    install_tracked_file "$PROJECT_ROOT/compositor/hyalo-compositor/clients/lab-sensible-terminal" "$INSTALL_PREFIX_PATH/bin/lab-sensible-terminal" 755
    install_tracked_file "$PROJECT_ROOT/compositor/hyalo-compositor/data/labwc-portals.conf" "$INSTALL_PREFIX_PATH/share/xdg-desktop-portal/labwc-portals.conf" 644
    install_tracked_file "$PROJECT_ROOT/compositor/hyalo-compositor/data/labwc-symbolic.svg" "$INSTALL_PREFIX_PATH/share/icons/hicolor/scalable/apps/labwc-symbolic.svg" 644
    install_tracked_file "$PROJECT_ROOT/compositor/hyalo-compositor/data/labwc.svg" "$INSTALL_PREFIX_PATH/share/icons/hicolor/scalable/apps/labwc.svg" 644

    for doc_name in autostart environment menu.xml README shutdown themerc rc.xml rc.xml.all; do
        install_tracked_file \
            "$PROJECT_ROOT/compositor/hyalo-compositor/docs/$doc_name" \
            "$INSTALL_PREFIX_PATH/share/doc/labwc/$doc_name" \
            644
    done

    for manpage in "$COMPOSITOR_BUILD_DIR"/docs/*.1 "$COMPOSITOR_BUILD_DIR"/docs/*.5; do
        if [ -f "$manpage" ]; then
            man_section=${manpage##*.}
            install_tracked_file "$manpage" "$INSTALL_PREFIX_PATH/share/man/man$man_section/$(basename "$manpage")" 644
        fi
    done

    for locale_file in "$COMPOSITOR_BUILD_DIR"/po/*/LC_MESSAGES/labwc.mo; do
        if [ -f "$locale_file" ]; then
            locale_name=$(basename "$(dirname "$(dirname "$locale_file")")")
            install_tracked_file \
                "$locale_file" \
                "$INSTALL_PREFIX_PATH/share/locale/$locale_name/LC_MESSAGES/labwc.mo" \
                644
        fi
    done
fi

printf '==> Installing HyaloOS session wrapper and desktop entry\n'
mkdir -p "$INSTALL_LIBEXEC_DIR" "$INSTALL_SESSION_DIR"
install_tracked_file \
    "$PROJECT_ROOT/config/labwc/themes/HyaloOS/openbox-3/themerc" \
    "$INSTALL_PREFIX_PATH/share/themes/HyaloOS/openbox-3/themerc" \
    644
# Install SVG decoration button assets for the labwc theme path
mkdir -p "$INSTALL_PREFIX_PATH/share/themes/HyaloOS/labwc"
for svg_file in "$PROJECT_ROOT"/config/labwc/themes/HyaloOS/labwc/*.svg; do
    [ -f "$svg_file" ] || continue
    install_tracked_file \
        "$svg_file" \
        "$INSTALL_PREFIX_PATH/share/themes/HyaloOS/labwc/$(basename "$svg_file")" \
        644
done
install_tracked_file \
    "$PROJECT_ROOT/config/labwc/hyalo-screenshot" \
    "$INSTALL_PREFIX_PATH/bin/hyalo-screenshot" \
    755
install_tracked_file \
    "$PROJECT_ROOT/config/labwc/hyalo-wallpaperd" \
    "$INSTALL_PREFIX_PATH/bin/hyalo-wallpaperd" \
    755
install_tracked_file \
    "$PROJECT_ROOT/config/labwc/hyalo-driver-manager" \
    "$INSTALL_PREFIX_PATH/bin/hyalo-driver-manager" \
    755
install_tracked_file \
    "$PROJECT_ROOT/config/labwc/rc.xml" \
    "$INSTALL_LABWC_CONFIG_DIR/rc.xml" \
    644
install_tracked_file \
    "$PROJECT_ROOT/config/mako/config" \
    "$INSTALL_MAKO_CONFIG_DIR/config" \
    644

cat >"$INSTALL_LIBEXEC_DIR/hyalo-session" <<'EOF'
#!/bin/sh

set -eu

PREFIX="__HYALO_PREFIX__"
PANEL_BIN="$PREFIX/bin/hyalo-panel"
WALLPAPER_DAEMON_BIN="$PREFIX/bin/hyalo-wallpaperd"
COMPOSITOR_BIN="$PREFIX/bin/labwc"
RUNTIME_LIB_DIR="$PREFIX/lib/hyalo"
LABWC_CONFIG_DIR="$PREFIX/share/hyalo/labwc"
MAKO_DEFAULT_CONFIG="$PREFIX/share/hyalo/mako/config"
SESSION_STARTUP='PANEL_LOG_TARGET="/dev/null"; if [ "${HYALO_PANEL_LOG:-0}" = "1" ]; then PANEL_LOG_DIR="${XDG_RUNTIME_DIR:-/tmp}/hyalo"; mkdir -p "$PANEL_LOG_DIR"; PANEL_LOG_FILE="$PANEL_LOG_DIR/panel.log"; PANEL_LOG_TARGET="$PANEL_LOG_FILE"; fi; PANEL_CMD=""; if [ -x "$PANEL_BIN" ]; then PANEL_CMD="$PANEL_BIN"; elif command -v hyalo-panel >/dev/null 2>&1; then PANEL_CMD="hyalo-panel"; fi; if [ -n "$PANEL_CMD" ]; then ( delay=1; while :; do "$PANEL_CMD" >>"$PANEL_LOG_TARGET" 2>&1; exit_code=$?; printf "[%s] hyalo-panel exited with code %s, restarting in %ss\n" "$(date +"%F %T")" "$exit_code" "$delay" >>"$PANEL_LOG_TARGET"; sleep "$delay"; if [ "$delay" -lt 8 ]; then delay=$((delay * 2)); fi; done ) & fi; if command -v mako >/dev/null 2>&1 && ! pgrep -xu "$(id -u)" mako >/dev/null 2>&1; then MAKO_CONFIG_PATH="${XDG_CONFIG_HOME:-$HOME/.config}/mako/config"; if [ ! -f "$MAKO_CONFIG_PATH" ]; then MAKO_CONFIG_PATH="$MAKO_DEFAULT_CONFIG"; fi; if [ -f "$MAKO_CONFIG_PATH" ]; then mako --config "$MAKO_CONFIG_PATH" >/dev/null 2>&1 & else mako >/dev/null 2>&1 & fi; fi; if command -v swww-daemon >/dev/null 2>&1 && ! swww query >/dev/null 2>&1; then swww-daemon >/dev/null 2>&1 & fi; if [ -x "$WALLPAPER_DAEMON_BIN" ]; then HYALO_WALLPAPER_BACKEND="${HYALO_WALLPAPER_BACKEND:-auto}" "$WALLPAPER_DAEMON_BIN" --daemon >/dev/null 2>&1 & elif command -v hyalo-wallpaperd >/dev/null 2>&1; then HYALO_WALLPAPER_BACKEND="${HYALO_WALLPAPER_BACKEND:-auto}" hyalo-wallpaperd --daemon >/dev/null 2>&1 & fi; if command -v dex >/dev/null 2>&1; then dex -a -e HyaloOS >/dev/null 2>&1 & fi'

prepend_prefix_path() {
    candidate_dir=$1

    [ -d "$candidate_dir" ] || return 0

    if [ -n "${PATH:-}" ]; then
        export PATH="$candidate_dir:${PATH}"
    else
        export PATH="$candidate_dir"
    fi
}

prepend_data_dir() {
    candidate_dir=$1

    [ -d "$candidate_dir" ] || return 0

    if [ -n "${XDG_DATA_DIRS:-}" ]; then
        export XDG_DATA_DIRS="$candidate_dir:${XDG_DATA_DIRS}"
    else
        export XDG_DATA_DIRS="$candidate_dir:/usr/local/share:/usr/share"
    fi
}

add_prefix_library_path() {
    candidate_dir=$1

    [ -d "$candidate_dir" ] || return 0

    if [ -n "${LD_LIBRARY_PATH:-}" ]; then
        export LD_LIBRARY_PATH="$candidate_dir:${LD_LIBRARY_PATH}"
    else
        export LD_LIBRARY_PATH="$candidate_dir"
    fi
}

add_prefix_library_path "$RUNTIME_LIB_DIR"
add_prefix_library_path "$PREFIX/lib"
add_prefix_library_path "$PREFIX/lib64"
prepend_prefix_path "$PREFIX/bin"
prepend_data_dir "$PREFIX/share"

if [ -z "${GTK_ICON_THEME:-}" ]; then
    export GTK_ICON_THEME="${HYALO_ICON_THEME:-hyalo-icons}"
fi

if [ -z "${GSK_RENDERER:-}" ]; then
    export GSK_RENDERER="gl"
fi

if [ -z "${GDK_DISABLE:-}" ]; then
    export GDK_DISABLE="vulkan"
fi

if [ -n "${XDG_CURRENT_DESKTOP:-}" ]; then
    export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP}:HyaloOS"
else
    export XDG_CURRENT_DESKTOP="HyaloOS"
fi

export XDG_SESSION_DESKTOP="HyaloOS"

if [ -x "$COMPOSITOR_BIN" ]; then
    exec "$COMPOSITOR_BIN" -C "$LABWC_CONFIG_DIR" -m -s "sh -lc '$SESSION_STARTUP'" "$@"
fi

exec labwc -C "$LABWC_CONFIG_DIR" -m -s "sh -lc '$SESSION_STARTUP'" "$@"
EOF
sed -i "s|__HYALO_PREFIX__|$PREFIX|g" "$INSTALL_LIBEXEC_DIR/hyalo-session"
chmod 755 "$INSTALL_LIBEXEC_DIR/hyalo-session"
record_manifest_path "$INSTALL_LIBEXEC_DIR/hyalo-session"

cat >"$INSTALL_SESSION_DIR/hyalo.desktop" <<EOF
[Desktop Entry]
Name=HyaloOS
Comment=HyaloOS Wayland session powered by HyaloWM
Exec=${LIBEXEC_DIR}/hyalo-session
Icon=hyalo
Type=Application
DesktopNames=HyaloOS;HyaloWM;wlroots
EOF
record_manifest_path "$INSTALL_SESSION_DIR/hyalo.desktop"

# ── LightDM greeter configuration ──
if [ -d /etc/lightdm ] && [ "$MODE" = system ]; then
    LIGHTDM_CONF_DIR="/etc/lightdm/lightdm.conf.d"
    mkdir -p "$LIGHTDM_CONF_DIR"
    cp "$PROJECT_ROOT/apps/hyalo-greeter/data/lightdm-hyalo-greeter.conf" \
       "$LIGHTDM_CONF_DIR/50-hyalo-greeter.conf" 2>/dev/null || true
    record_manifest_path "$LIGHTDM_CONF_DIR/50-hyalo-greeter.conf"
    printf 'LightDM greeter config installed to %s\n' "$LIGHTDM_CONF_DIR/50-hyalo-greeter.conf"

    # LightDM expects greeters in /usr/share/xgreeters
    SYSTEM_XGREETERS="/usr/share/xgreeters"
    if [ -d "$SYSTEM_XGREETERS" ] && [ ! -f "$SYSTEM_XGREETERS/hyalo-greeter.desktop" ]; then
        cp "$PROJECT_ROOT/apps/hyalo-greeter/data/hyalo-greeter.desktop" \
           "$SYSTEM_XGREETERS/hyalo-greeter.desktop" 2>/dev/null || true
        record_manifest_path "$SYSTEM_XGREETERS/hyalo-greeter.desktop"
        printf 'Greeter desktop entry also installed to %s\n' "$SYSTEM_XGREETERS"
    fi
fi

write_manifest "$MANIFEST_PATH"

printf '\nInstalled HyaloOS to %s\n' "$INSTALL_PREFIX_PATH"
printf 'Session entry installed to %s/hyalo.desktop\n' "$INSTALL_SESSION_DIR"
printf 'Session wrapper installed to %s/hyalo-session\n' "$INSTALL_LIBEXEC_DIR"
printf 'Install manifest written to %s\n' "$MANIFEST_PATH"

if [ "$MODE" = user ]; then
    printf '\nNote: user-local session entries are useful for testing, but SDDM/LightDM may only expose system-wide sessions.\n'
else
    printf '\nHyaloOS should now be available as a Wayland session in SDDM/LightDM after logout.\n'
fi

printf '=== HyaloOS install finished at %s ===\n' "$(date '+%Y-%m-%d %H:%M:%S %z')"
printf 'Log written to %s\n' "$LOG_FILE"