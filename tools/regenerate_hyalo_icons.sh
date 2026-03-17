#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────
# HyaloOS Icon Generator
# Colorful line-art SVG icons — no backgrounds, just colored strokes
# on transparent canvas, matching the window-decoration button style.
# Only hyalo-icons theme, no hicolor, no symbolic variants.
# ──────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THEME_APPS="$ROOT/assets/icons/hyalo-icons/scalable/apps"

# ─── SVG wrapper ─────────────────────────────────────────────────────

app_svg() {
    local body="$1" color="$2"
    cat <<EOF
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256" fill="none" stroke="$color" stroke-width="12" stroke-linecap="round" stroke-linejoin="round">$body</svg>
EOF
}

# ─── App icon bodies (256×256) ───────────────────────────────────────

body_hyalo='<path d="M88 86 128 110v72l-28-20Z"/><path d="M168 86 128 110v72l28-20Z"/><path d="M102 84 128 68l26 16-26 16Z"/>'
body_terminal='<rect x="60" y="72" width="136" height="112" rx="16"/><path d="M88 116 112 140 88 164"/><line x1="128" y1="164" x2="168" y2="164"/>'
body_settings='<circle cx="128" cy="128" r="32"/><line x1="128" y1="68" x2="128" y2="88"/><line x1="128" y1="168" x2="128" y2="188"/><line x1="68" y1="128" x2="88" y2="128"/><line x1="168" y1="128" x2="188" y2="128"/><line x1="86" y1="86" x2="100" y2="100"/><line x1="156" y1="156" x2="170" y2="170"/><line x1="170" y1="86" x2="156" y2="100"/><line x1="100" y1="156" x2="86" y2="170"/>'
body_store='<path d="M80 96h96l-4 72H84Z"/><path d="M104 96a24 24 0 0 1 48 0"/><line x1="128" y1="118" x2="128" y2="146"/><line x1="114" y1="132" x2="142" y2="132"/>'
body_refresh='<path d="M172 104V80h-24"/><path d="M172 84a56 56 0 1 0 16 48"/>'
body_folder='<path d="M64 100h48l14-16h64c8 0 14 6 14 14v68c0 8-6 14-14 14H72c-8 0-14-6-14-14Z"/>'
body_browser='<circle cx="128" cy="128" r="60"/><line x1="72" y1="108" x2="184" y2="108"/><line x1="78" y1="148" x2="178" y2="148"/><path d="M128 68c16 16 26 38 26 60s-10 44-26 60"/><path d="M128 68c-16 16-26 38-26 60s10 44 26 60"/>'
body_calendar='<rect x="64" y="72" width="128" height="116" rx="16"/><line x1="96" y1="60" x2="96" y2="84"/><line x1="160" y1="60" x2="160" y2="84"/><line x1="64" y1="104" x2="192" y2="104"/><rect x="104" y="120" width="48" height="44" rx="10" stroke="none"/>'
body_notes='<path d="M86 68h80l24 24v84c0 8-6 14-14 14H86c-8 0-14-6-14-14V82c0-8 6-14 14-14Z"/><path d="M152 68v28h28"/><line x1="96" y1="124" x2="160" y2="124"/><line x1="96" y1="150" x2="144" y2="150"/>'
body_monitor='<rect x="60" y="72" width="136" height="88" rx="14"/><line x1="112" y1="184" x2="144" y2="184"/><line x1="128" y1="160" x2="128" y2="184"/>'
body_power='<line x1="128" y1="76" x2="128" y2="124"/><path d="M164 92a48 48 0 1 1-72 0"/>'
body_wifi='<path d="M80 128a68 68 0 0 1 96 0"/><path d="M96 148a40 40 0 0 1 64 0"/><path d="M112 168a16 16 0 0 1 32 0"/><circle cx="128" cy="184" r="6" stroke="none"/>'
body_shield='<path d="M128 68 176 84v36c0 32-16 56-48 72-32-16-48-40-48-72V84Z"/><path d="M108 130 120 142 148 114"/>'
body_grid='<rect x="72" y="72" width="44" height="44" rx="10"/><rect x="140" y="72" width="44" height="44" rx="10"/><rect x="72" y="140" width="44" height="44" rx="10"/><rect x="140" y="140" width="44" height="44" rx="10"/>'
body_image='<rect x="60" y="68" width="136" height="120" rx="16"/><circle cx="98" cy="106" r="10" stroke="none"/><path d="M76 172 106 138l22 20 24-30 28 44"/>'
body_play='<circle cx="128" cy="128" r="60"/><path d="M112 98 164 128 112 158Z" stroke="none"/>'
body_camera='<path d="M88 88h20l10-14h20l10 14h20c8 0 14 6 14 14v60c0 8-6 14-14 14H88c-8 0-14-6-14-14v-60c0-8 6-14 14-14Z"/><circle cx="128" cy="130" r="26"/>'
body_archive='<rect x="64" y="80" width="128" height="100" rx="14"/><line x1="64" y1="108" x2="192" y2="108"/><rect x="112" y="96" width="32" height="24" rx="6"/>'
body_wallpaper='<rect x="60" y="68" width="136" height="120" rx="16"/><path d="M60 156 100 120l36 36 24-20 36 32"/><circle cx="100" cy="104" r="12" stroke="none"/>'
body_bluetooth='<path d="M118 76 152 108l-28 20 28 20-34 32V76Z"/><line x1="96" y1="108" x2="124" y2="128"/><line x1="96" y1="148" x2="124" y2="128"/>'
body_volume='<path d="M80 108h16l24-20v80l-24-20H80Z"/><path d="M148 104a28 28 0 0 1 0 48"/><path d="M160 88a52 52 0 0 1 0 80"/>'
body_microphone='<rect x="104" y="64" width="48" height="64" rx="24"/><path d="M84 132a44 44 0 0 0 88 0"/><line x1="128" y1="176" x2="128" y2="196"/><line x1="108" y1="196" x2="148" y2="196"/>'
body_keyboard='<rect x="48" y="84" width="160" height="88" rx="16"/><line x1="76" y1="112" x2="92" y2="112"/><line x1="108" y1="112" x2="124" y2="112"/><line x1="140" y1="112" x2="156" y2="112"/><line x1="164" y1="112" x2="180" y2="112"/><line x1="92" y1="144" x2="164" y2="144"/>'
body_mouse='<rect x="96" y="56" width="64" height="144" rx="32"/><line x1="128" y1="56" x2="128" y2="108"/>'
body_printer='<path d="M88 88V64h80v24"/><rect x="64" y="88" width="128" height="68" rx="12"/><rect x="88" y="136" width="80" height="44" rx="6"/><circle cx="168" cy="108" r="6" stroke="none"/>'
body_user='<circle cx="128" cy="100" r="28"/><path d="M80 188c0-28 22-48 48-48s48 20 48 48"/>'
body_notification='<path d="M104 68a24 24 0 0 1 48 0v8c12 4 20 16 20 32v24l12 16H72l12-16v-24c0-16 8-28 20-32Z"/><path d="M112 180a16 16 0 0 0 32 0"/>'
body_clock='<circle cx="128" cy="128" r="56"/><line x1="128" y1="88" x2="128" y2="128"/><line x1="128" y1="128" x2="156" y2="148"/>'
body_language='<circle cx="128" cy="128" r="56"/><ellipse cx="128" cy="128" rx="28" ry="56"/><line x1="72" y1="128" x2="184" y2="128"/><line x1="80" y1="100" x2="176" y2="100"/><line x1="80" y1="156" x2="176" y2="156"/>'
body_palette='<circle cx="128" cy="128" r="60"/><circle cx="108" cy="100" r="10" stroke="none"/><circle cx="148" cy="100" r="10" stroke="none"/><circle cx="96" cy="136" r="10" stroke="none"/><circle cx="136" cy="156" r="10" stroke="none"/>'
body_privacy='<path d="M128 68 176 84v36c0 32-16 56-48 72-32-16-48-40-48-72V84Z"/><circle cx="128" cy="118" r="12"/><line x1="128" y1="130" x2="128" y2="148"/>'
body_info='<circle cx="128" cy="128" r="56"/><line x1="128" y1="120" x2="128" y2="168"/><circle cx="128" cy="100" r="6" stroke="none"/>'
body_download='<line x1="128" y1="68" x2="128" y2="152"/><path d="M100 128 128 156 156 128"/><line x1="76" y1="180" x2="180" y2="180"/>'
body_search='<circle cx="112" cy="112" r="40"/><line x1="140" y1="140" x2="180" y2="180"/>'
body_trash='<line x1="84" y1="88" x2="172" y2="88"/><rect x="88" y="88" width="80" height="92" rx="10"/><line x1="108" y1="72" x2="148" y2="72"/><line x1="108" y1="72" x2="108" y2="88"/><line x1="148" y1="72" x2="148" y2="88"/><line x1="112" y1="112" x2="112" y2="156"/><line x1="128" y1="112" x2="128" y2="156"/><line x1="144" y1="112" x2="144" y2="156"/>'
body_text_editor='<rect x="64" y="64" width="128" height="128" rx="16"/><line x1="92" y1="100" x2="164" y2="100"/><line x1="92" y1="128" x2="164" y2="128"/><line x1="92" y1="156" x2="140" y2="156"/>'
body_calculator='<rect x="72" y="56" width="112" height="144" rx="16"/><line x1="72" y1="104" x2="184" y2="104"/><circle cx="96" cy="128" r="4" stroke="none"/><circle cx="128" cy="128" r="4" stroke="none"/><circle cx="160" cy="128" r="4" stroke="none"/><circle cx="96" cy="160" r="4" stroke="none"/><circle cx="128" cy="160" r="4" stroke="none"/><circle cx="160" cy="160" r="4" stroke="none"/><line x1="92" y1="80" x2="164" y2="80"/>'
body_music='<circle cx="96" cy="168" r="20"/><circle cx="168" cy="152" r="20"/><line x1="116" y1="168" x2="116" y2="76"/><line x1="188" y1="152" x2="188" y2="68"/><line x1="116" y1="76" x2="188" y2="68"/>'
body_video='<rect x="56" y="80" width="112" height="96" rx="14"/><path d="M168 108 200 88v80l-32-20Z"/>'
body_mail='<rect x="60" y="80" width="136" height="96" rx="14"/><path d="M60 94l68 46 68-46"/>'
body_chat='<path d="M68 76h120c8 0 14 6 14 14v64c0 8-6 14-14 14H116l-28 24v-24H68c-8 0-14-6-14-14V90c0-8 6-14 14-14Z"/><line x1="96" y1="116" x2="160" y2="116"/><line x1="96" y1="136" x2="144" y2="136"/>'
body_map='<path d="M72 72 112 84v100l-40-12Z"/><path d="M112 84l40-12v100l-40 12Z"/><path d="M152 72l40 12v100l-40-12Z"/>'
body_weather='<circle cx="116" cy="112" r="28"/><line x1="116" y1="68" x2="116" y2="80"/><line x1="116" y1="144" x2="116" y2="156"/><line x1="72" y1="112" x2="84" y2="112"/><line x1="148" y1="112" x2="160" y2="112"/><line x1="84" y1="80" x2="93" y2="89"/><line x1="139" y1="135" x2="148" y2="144"/><line x1="148" y1="80" x2="139" y2="89"/><line x1="93" y1="135" x2="84" y2="144"/>'
body_battery='<rect x="64" y="96" width="108" height="64" rx="14"/><line x1="172" y1="116" x2="172" y2="140"/><rect x="78" y="108" width="80" height="40" rx="6" stroke="none"/>'
body_accessibility='<circle cx="128" cy="80" r="16"/><line x1="128" y1="96" x2="128" y2="148"/><line x1="96" y1="116" x2="160" y2="116"/><line x1="128" y1="148" x2="104" y2="192"/><line x1="128" y1="148" x2="152" y2="192"/>'
body_home='<path d="M68 132 128 80l60 52"/><path d="M84 124v52c0 4 4 8 8 8h28v-32h16v32h28c4 0 8-4 8-8v-52"/>'
body_compass='<circle cx="128" cy="128" r="56"/><path d="M108 148 120 120l28-12-12 28Z"/>'
body_lock='<rect x="80" y="120" width="96" height="68" rx="14"/><path d="M100 120v-20a28 28 0 0 1 56 0v20"/><circle cx="128" cy="152" r="8" stroke="none"/>'
body_eye='<path d="M68 128c20-36 52-52 60-52s40 16 60 52c-20 36-52 52-60 52s-40-16-60-52Z"/><circle cx="128" cy="128" r="20"/><circle cx="128" cy="128" r="8" stroke="none"/>'
body_bookmark='<path d="M80 68h96v120l-48-28-48 28Z"/>'
body_clipboard='<rect x="72" y="76" width="112" height="120" rx="14"/><rect x="100" y="64" width="56" height="24" rx="10"/><line x1="96" y1="120" x2="160" y2="120"/><line x1="96" y1="148" x2="144" y2="148"/>'
body_link='<path d="M108 148a28 28 0 0 1 0-40l16-16a28 28 0 0 1 40 0l0 0a28 28 0 0 1 0 40l-8 8"/><path d="M148 108a28 28 0 0 1 0 40l-16 16a28 28 0 0 1-40 0l0 0a28 28 0 0 1 0-40l8-8"/>'
body_pin='<circle cx="128" cy="100" r="24"/><line x1="128" y1="124" x2="128" y2="188"/><line x1="96" y1="152" x2="160" y2="152"/>'
body_share='<circle cx="168" cy="88" r="16"/><circle cx="88" cy="128" r="16"/><circle cx="168" cy="168" r="16"/><line x1="102" y1="120" x2="154" y2="96"/><line x1="102" y1="136" x2="154" y2="160"/>'
body_star='<path d="M128 68l16 36h40l-32 24 12 40-36-24-36 24 12-40-32-24h40Z"/>'
body_heart='<path d="M128 100c-12-24-40-32-52-16s-4 40 52 76c56-36 64-60 52-76s-40-8-52 16Z"/>'
body_cloud='<path d="M80 164h96c20 0 36-16 36-36s-14-34-32-36c0-24-20-44-44-44-20 0-36 12-42 30-18 2-30 16-30 34s14 32 16 52Z"/>'
body_database='<ellipse cx="128" cy="88" rx="52" ry="20"/><path d="M76 88v80c0 12 24 20 52 20s52-8 52-20V88"/><path d="M76 128c0 12 24 20 52 20s52-8 52-20"/>'
body_plug='<line x1="108" y1="68" x2="108" y2="108"/><line x1="148" y1="68" x2="148" y2="108"/><rect x="84" y="108" width="88" height="32" rx="8"/><line x1="128" y1="140" x2="128" y2="168"/><path d="M104 168h48c8 0 12 8 12 16v4H92v-4c0-8 4-16 12-16Z"/>'
body_cpu='<rect x="80" y="80" width="96" height="96" rx="12"/><rect x="100" y="100" width="56" height="56" rx="6"/><line x1="104" y1="68" x2="104" y2="80"/><line x1="128" y1="68" x2="128" y2="80"/><line x1="152" y1="68" x2="152" y2="80"/><line x1="104" y1="176" x2="104" y2="188"/><line x1="128" y1="176" x2="128" y2="188"/><line x1="152" y1="176" x2="152" y2="188"/><line x1="68" y1="104" x2="80" y2="104"/><line x1="68" y1="128" x2="80" y2="128"/><line x1="68" y1="152" x2="80" y2="152"/><line x1="176" y1="104" x2="188" y2="104"/><line x1="176" y1="128" x2="188" y2="128"/><line x1="176" y1="152" x2="188" y2="152"/>'
body_gamepad='<rect x="52" y="88" width="152" height="80" rx="20"/><circle cx="108" cy="128" r="6" stroke="none"/><circle cx="148" cy="128" r="6" stroke="none"/><line x1="80" y1="116" x2="80" y2="140"/><line x1="68" y1="128" x2="92" y2="128"/><line x1="172" y1="116" x2="172" y2="128"/><line x1="164" y1="118" x2="180" y2="118"/>'
body_headphones='<path d="M76 132V116a52 52 0 0 1 104 0v16"/><rect x="68" y="132" width="24" height="40" rx="10"/><rect x="164" y="132" width="24" height="40" rx="10"/>'
body_film='<rect x="60" y="72" width="136" height="112" rx="14"/><line x1="60" y1="100" x2="196" y2="100"/><line x1="60" y1="156" x2="196" y2="156"/><line x1="92" y1="72" x2="92" y2="100"/><line x1="164" y1="72" x2="164" y2="100"/><line x1="92" y1="156" x2="92" y2="184"/><line x1="164" y1="156" x2="164" y2="184"/>'
body_book='<path d="M128 76c-16-8-32-12-48-8v100c16-4 32 0 48 8 16-8 32-12 48-8V68c-16-4-32 0-48 8Z"/><line x1="128" y1="76" x2="128" y2="176"/>'
body_coffee='<path d="M68 88h104"/><path d="M80 88v56c0 16 12 28 28 28h24c16 0 28-12 28-28V88"/><path d="M160 112h12c10 0 18 8 18 18s-8 18-18 18h-12"/><line x1="96" y1="68" x2="96" y2="76"/><line x1="112" y1="64" x2="112" y2="76"/><line x1="128" y1="68" x2="128" y2="76"/>'
body_flag='<path d="M80 68v120"/><path d="M80 68h88c8 0 12 6 8 12l-16 24 16 24c4 6 0 12-8 12H80"/>'
body_scissors='<circle cx="100" cy="160" r="16"/><circle cx="156" cy="160" r="16"/><line x1="112" y1="148" x2="152" y2="88"/><line x1="144" y1="148" x2="104" y2="88"/>'
body_wrench='<path d="M160 76a36 36 0 0 0-48 24l44 44a36 36 0 0 0 24-48l-16 16-12-4-4-12Z"/><path d="M112 136l-36 36a12 12 0 0 0 0 17l3 3a12 12 0 0 0 17 0l36-36"/>'
body_brush='<path d="M148 68l-44 76h48l-44 76"/>'
body_gift='<rect x="68" y="112" width="120" height="68" rx="12"/><line x1="128" y1="112" x2="128" y2="180"/><line x1="68" y1="140" x2="188" y2="140"/><path d="M96 112c0-16 16-28 32-28"/><path d="M160 112c0-16-16-28-32-28"/>'
body_truck='<rect x="52" y="88" width="100" height="72" rx="10"/><path d="M152 108h32l20 28v24h-52Z"/><circle cx="100" cy="172" r="14"/><circle cx="180" cy="172" r="14"/>'

# Third-party app bodies (256×256)
body_code='<path d="M100 92 72 128 100 164"/><path d="M156 92 184 128 156 164"/><line x1="144" y1="76" x2="112" y2="180"/>'
body_gimp='<circle cx="128" cy="128" r="56"/><path d="M100 148 128 84 156 148"/><line x1="108" y1="136" x2="148" y2="136"/>'
body_libreoffice='<path d="M86 68h56l28 28v92c0 8-6 14-14 14H86c-8 0-14-6-14-14V82c0-8 6-14 14-14Z"/><path d="M142 68v28h28"/><line x1="96" y1="116" x2="160" y2="116"/><line x1="96" y1="140" x2="148" y2="140"/><line x1="96" y1="164" x2="136" y2="164"/>'
body_steam='<circle cx="128" cy="112" r="20"/><circle cx="128" cy="112" r="8" stroke="none"/><path d="M108 132 84 172h88l-24-40"/>'
body_discord='<rect x="72" y="80" width="112" height="88" rx="20"/><circle cx="108" cy="124" r="10" stroke="none"/><circle cx="148" cy="124" r="10" stroke="none"/><path d="M92 172l-4 16"/><path d="M164 172l4 16"/>'
body_blender='<path d="M76 128 128 84 180 128 128 172Z"/><circle cx="128" cy="128" r="20"/>'
body_inkscape='<line x1="128" y1="76" x2="128" y2="152"/><path d="M116 88 128 76l12 12"/><line x1="80" y1="164" x2="176" y2="164"/><line x1="92" y1="180" x2="164" y2="180"/>'
body_thunderbird='<rect x="68" y="84" width="120" height="88" rx="14"/><path d="M68 98l60 42 60-42"/>'
body_obs='<circle cx="128" cy="128" r="52"/><circle cx="128" cy="128" r="20"/><circle cx="128" cy="128" r="6" stroke="none"/>'
body_telegram='<path d="M72 132 180 80 148 180 120 136Z"/><line x1="120" y1="136" x2="148" y2="180"/>'
body_signal='<path d="M80 148V100c0-26 22-48 48-48s48 22 48 48v4c0 26-22 48-48 48H80Z"/><line x1="104" y1="112" x2="152" y2="112"/><line x1="104" y1="132" x2="140" y2="132"/>'
body_vlc='<line x1="92" y1="172" x2="164" y2="172"/><path d="M104 172 116 76h24l12 96"/>'
body_spotify='<circle cx="128" cy="128" r="56"/><path d="M84 108c20-8 48-8 68 4"/><path d="M92 132c16-6 40-6 56 4"/><path d="M100 156c12-6 32-6 48 4"/>'
body_firefox='<circle cx="128" cy="128" r="56"/><path d="M84 104c8-16 24-24 44-24 28 0 48 20 48 48 0 18-8 32-20 40"/><path d="M92 132c4-20 20-32 36-32"/>'
body_chromium='<circle cx="128" cy="128" r="56"/><circle cx="128" cy="128" r="20"/><line x1="128" y1="72" x2="128" y2="108"/><line x1="80" y1="156" x2="110" y2="138"/><line x1="176" y1="156" x2="146" y2="138"/>'

# ─── App stroke color ────────────────────────────────────────────────

app_color_for() {
    local name="$1"
    case "$name" in
        *update*)                echo "#3b7ddb" ;;
        *software-store*|*store*) echo "#1ca8c4" ;;
        *software*)              echo "#1ca8c4" ;;
        *terminal*)              echo "#10b0a0" ;;
        *control-center*)        echo "#6b8fd4" ;;
        *panel*|*launcher*|*workspaces*|*quick-panel*) echo "#1aaf96" ;;
        *browser*)               echo "#3498db" ;;
        *power*)                 echo "#e08850" ;;
        *security*|*privacy*)    echo "#1ec088" ;;
        *files*)                 echo "#d4a24c" ;;
        *archive*)               echo "#c89848" ;;
        *calendar*)              echo "#e06858" ;;
        *camera*)                echo "#a870d8" ;;
        *image*|*photo*)         echo "#9868c0" ;;
        *wallpaper*)             echo "#7a92d4" ;;
        *notes*|*text*)          echo "#c8a050" ;;
        *display*|*system-monitor*|*screen*) echo "#5898e0" ;;
        *media*|*music*)         echo "#3890d8" ;;
        *network*|*wifi*)        echo "#3498db" ;;
        *bluetooth*)             echo "#5090e8" ;;
        *audio*|*volume*|*speaker*) echo "#5878d0" ;;
        *microphone*)            echo "#d06888" ;;
        *keyboard*)              echo "#70a8c8" ;;
        *mouse*)                 echo "#80b0c0" ;;
        *printer*)               echo "#6090b0" ;;
        *user*|*about*)          echo "#58a0c8" ;;
        *notification*)          echo "#e8a040" ;;
        *clock*|*time*|*date*)   echo "#c87858" ;;
        *language*|*locale*)     echo "#6898c8" ;;
        *palette*|*appearance*|*theme*) echo "#c878a8" ;;
        *download*)              echo "#48b870" ;;
        *search*)                echo "#7888c0" ;;
        *trash*)                 echo "#a87860" ;;
        *calculator*)            echo "#68a8a0" ;;
        *video*|*film*)          echo "#d07050" ;;
        *mail*)                  echo "#4898d8" ;;
        *chat*|*message*)        echo "#58b888" ;;
        *map*|*compass*)         echo "#48a868" ;;
        *weather*)               echo "#e8b040" ;;
        *battery*)               echo "#58c068" ;;
        *accessibility*)         echo "#5090c8" ;;
        *info*)                  echo "#6888b8" ;;
        *home*)                  echo "#d89848" ;;
        *lock*)                  echo "#58a088" ;;
        *eye*|*view*)            echo "#7890c8" ;;
        *bookmark*)              echo "#c87880" ;;
        *clipboard*)             echo "#88a0b8" ;;
        *link*)                  echo "#5890d0" ;;
        *pin*)                   echo "#d87858" ;;
        *share*)                 echo "#58a8c0" ;;
        *star*|*favorite*)       echo "#e8b838" ;;
        *heart*|*love*)          echo "#e06878" ;;
        *cloud*)                 echo "#78a8d0" ;;
        *database*)              echo "#6898a8" ;;
        *plug*|*energy*)         echo "#88b868" ;;
        *cpu*|*processor*)       echo "#68a0b8" ;;
        *gamepad*|*game*)        echo "#9870c8" ;;
        *headphone*)             echo "#8878c0" ;;
        *book*|*reader*)         echo "#b89060" ;;
        *coffee*)                echo "#c09868" ;;
        *flag*)                  echo "#d87058" ;;
        *scissors*|*cut*)        echo "#a08890" ;;
        *wrench*|*tool*)         echo "#8098b0" ;;
        *brush*|*lightning*)     echo "#e8c040" ;;
        *gift*)                  echo "#d878a0" ;;
        *truck*|*delivery*)      echo "#7898b0" ;;
        # Third-party
        firefox)                 echo "#e86828" ;;
        chromium|google-chrome)  echo "#4a90e8" ;;
        code|visual-studio-code) echo "#3898e0" ;;
        gimp)                    echo "#a09050" ;;
        libreoffice*)            echo "#38a850" ;;
        vlc)                     echo "#e87028" ;;
        spotify)                 echo "#1db954" ;;
        steam)                   echo "#5098d8" ;;
        discord)                 echo "#5865f2" ;;
        blender)                 echo "#e88020" ;;
        inkscape)                echo "#4880b8" ;;
        thunderbird)             echo "#2088f8" ;;
        obs|obs-studio)          echo "#8068b8" ;;
        telegram*)               echo "#30a8e8" ;;
        signal*)                 echo "#4880f0" ;;
        *)                       echo "#3098a0" ;;
    esac
}

# ─── App body mapping ────────────────────────────────────────────────

app_body_for() {
    local name="$1"
    case "$name" in
        *update*)            echo "$body_refresh" ;;
        *terminal*)          echo "$body_terminal" ;;
        *control-center*)    echo "$body_settings" ;;
        *software-store*|*store*) echo "$body_store" ;;
        *software*)          echo "$body_store" ;;
        *archive*)           echo "$body_archive" ;;
        *files*)             echo "$body_folder" ;;
        *browser*)           echo "$body_browser" ;;
        *calendar*)          echo "$body_calendar" ;;
        *camera*)            echo "$body_camera" ;;
        *system-monitor*|*cpu*) echo "$body_cpu" ;;
        *display*|*screen*)  echo "$body_monitor" ;;
        *power*)             echo "$body_power" ;;
        *network*|*wifi*)    echo "$body_wifi" ;;
        *security*)          echo "$body_shield" ;;
        *privacy*)           echo "$body_privacy" ;;
        *launcher*|*workspaces*|*panel*|*quick-panel*) echo "$body_grid" ;;
        *notes*)             echo "$body_notes" ;;
        *image-viewer*|*photo*) echo "$body_image" ;;
        *wallpaper*)         echo "$body_wallpaper" ;;
        *media-player*)      echo "$body_play" ;;
        *bluetooth*)         echo "$body_bluetooth" ;;
        *audio*|*volume*|*speaker*) echo "$body_volume" ;;
        *microphone*)        echo "$body_microphone" ;;
        *keyboard*)          echo "$body_keyboard" ;;
        *mouse*)             echo "$body_mouse" ;;
        *printer*)           echo "$body_printer" ;;
        *user*|*about*)      echo "$body_user" ;;
        *notification*)      echo "$body_notification" ;;
        *clock*|*time*|*date*) echo "$body_clock" ;;
        *language*|*locale*) echo "$body_language" ;;
        *palette*|*appearance*|*theme*) echo "$body_palette" ;;
        *download*)          echo "$body_download" ;;
        *search*)            echo "$body_search" ;;
        *trash*)             echo "$body_trash" ;;
        *text-editor*)       echo "$body_text_editor" ;;
        *calculator*)        echo "$body_calculator" ;;
        *music*)             echo "$body_music" ;;
        *video*|*film*)      echo "$body_video" ;;
        *mail*)              echo "$body_mail" ;;
        *chat*|*message*)    echo "$body_chat" ;;
        *map*)               echo "$body_map" ;;
        *weather*)           echo "$body_weather" ;;
        *battery*)           echo "$body_battery" ;;
        *accessibility*)     echo "$body_accessibility" ;;
        *info*)              echo "$body_info" ;;
        *home*)              echo "$body_home" ;;
        *compass*)           echo "$body_compass" ;;
        *lock*)              echo "$body_lock" ;;
        *eye*|*view*)        echo "$body_eye" ;;
        *bookmark*)          echo "$body_bookmark" ;;
        *clipboard*)         echo "$body_clipboard" ;;
        *link*)              echo "$body_link" ;;
        *pin*)               echo "$body_pin" ;;
        *share*)             echo "$body_share" ;;
        *star*|*favorite*)   echo "$body_star" ;;
        *heart*|*love*)      echo "$body_heart" ;;
        *cloud*)             echo "$body_cloud" ;;
        *database*)          echo "$body_database" ;;
        *plug*|*energy*)     echo "$body_plug" ;;
        *gamepad*|*game*)    echo "$body_gamepad" ;;
        *headphone*)         echo "$body_headphones" ;;
        *book*|*reader*)     echo "$body_book" ;;
        *coffee*)            echo "$body_coffee" ;;
        *flag*)              echo "$body_flag" ;;
        *scissors*|*cut*)    echo "$body_scissors" ;;
        *wrench*|*tool*)     echo "$body_wrench" ;;
        *brush*|*lightning*) echo "$body_brush" ;;
        *gift*)              echo "$body_gift" ;;
        *truck*|*delivery*)  echo "$body_truck" ;;
        *hyalowm*|hyalo)     echo "$body_hyalo" ;;
        # Third-party
        firefox)             echo "$body_firefox" ;;
        chromium|google-chrome) echo "$body_chromium" ;;
        code|visual-studio-code) echo "$body_code" ;;
        gimp)                echo "$body_gimp" ;;
        libreoffice*)        echo "$body_libreoffice" ;;
        vlc)                 echo "$body_vlc" ;;
        spotify)             echo "$body_spotify" ;;
        steam)               echo "$body_steam" ;;
        discord)             echo "$body_discord" ;;
        blender)             echo "$body_blender" ;;
        inkscape)            echo "$body_inkscape" ;;
        thunderbird)         echo "$body_thunderbird" ;;
        obs|obs-studio)      echo "$body_obs" ;;
        telegram*)           echo "$body_telegram" ;;
        signal*)             echo "$body_signal" ;;
        *)                   echo "$body_grid" ;;
    esac
}

# ─── Icon manifest ───────────────────────────────────────────────────

ICONS=(
    # HyaloOS system
    hyalo
    hyalowm
    hyalo-terminal
    hyalo-control-center
    hyalo-panel
    hyalo-software-store
    hyalo-update-center
    hyalo-launcher
    hyalo-files
    hyalo-browser
    hyalo-calendar
    hyalo-notes
    hyalo-system-monitor
    hyalo-software
    hyalo-media-player
    hyalo-camera
    hyalo-archive
    hyalo-network
    hyalo-power
    hyalo-security
    hyalo-display
    hyalo-workspaces
    hyalo-image-viewer
    hyalo-quick-panel
    hyalo-store
    hyalo-wallpaper
    hyalo-bluetooth
    hyalo-audio
    hyalo-microphone
    hyalo-keyboard
    hyalo-mouse
    hyalo-printer
    hyalo-user
    hyalo-notification
    hyalo-clock
    hyalo-language
    hyalo-appearance
    hyalo-download
    hyalo-search
    hyalo-trash
    hyalo-text-editor
    hyalo-calculator
    hyalo-music
    hyalo-video
    hyalo-mail
    hyalo-chat
    hyalo-map
    hyalo-weather
    hyalo-battery
    hyalo-accessibility
    hyalo-info
    hyalo-privacy
    hyalo-home
    hyalo-compass
    hyalo-lock
    hyalo-eye
    hyalo-bookmark
    hyalo-clipboard
    hyalo-link
    hyalo-pin
    hyalo-share
    hyalo-star
    hyalo-heart
    hyalo-cloud
    hyalo-database
    hyalo-plug
    hyalo-cpu
    hyalo-gamepad
    hyalo-headphones
    hyalo-film
    hyalo-book
    hyalo-coffee
    hyalo-flag
    hyalo-scissors
    hyalo-wrench
    hyalo-brush
    hyalo-gift
    hyalo-truck
    # Third-party
    firefox
    chromium
    google-chrome
    code
    visual-studio-code
    gimp
    libreoffice-startcenter
    vlc
    spotify
    steam
    discord
    blender
    inkscape
    thunderbird
    obs-studio
    telegram-desktop
    signal-desktop
)

# ─── Generation ──────────────────────────────────────────────────────

generate_icon() {
    local name="$1"
    local body color colored_body
    body="$(app_body_for "$name")"
    color="$(app_color_for "$name")"
    # Inject fill color into elements with stroke="none" (filled shapes)
    colored_body="${body//stroke=\"none\"/stroke=\"none\" fill=\"$color\"}"
    app_svg "$colored_body" "$color" > "$THEME_APPS/${name}.svg"
}

# ─── Clean ───────────────────────────────────────────────────────────

printf '==> Cleaning old icon files\n'
rm -f "$THEME_APPS"/*.svg 2>/dev/null || true
mkdir -p "$THEME_APPS"

# ─── Generate ────────────────────────────────────────────────────────

printf '==> Generating %d icons\n' "${#ICONS[@]}"
for name in "${ICONS[@]}"; do
    generate_icon "$name"
done

# ─── Summary ─────────────────────────────────────────────────────────

total=$(find "$THEME_APPS" -name '*.svg' | wc -l)
printf '==> Done: %d icons in hyalo-icons/scalable/apps/\n' "$total"