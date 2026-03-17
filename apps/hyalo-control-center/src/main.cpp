#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/utsname.h>
#include <unistd.h>

#include <glib.h>
#include <gio/gdesktopappinfo.h>
#include <glibmm/main.h>

#include <gtkmm/application.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/dialog.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechoosernative.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/fixed.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/gesturedrag.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/picture.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/scale.h>
#include <gtkmm/separator.h>
#include <gtkmm/settings.h>
#include <gtkmm/stack.h>
#include <gtkmm/switch.h>
#include <gtkmm/window.h>

#include "hyalo-core/config_manager.hpp"
#include "hyalo-core/localization.hpp"
#include "hyalo-core/style_manager.hpp"

#ifndef HYALO_DE_VERSION
#define HYALO_DE_VERSION "dev"
#endif

#ifndef HYALO_HAS_TERMINAL
#define HYALO_HAS_TERMINAL 0
#endif

namespace {

void configure_stable_gsk_renderer() {
    const auto* gsk_renderer = std::getenv("GSK_RENDERER");
    if (!gsk_renderer || gsk_renderer[0] == '\0') {
        g_setenv("GSK_RENDERER", "gl", TRUE);
    }

    const auto* gdk_disable = std::getenv("GDK_DISABLE");
    if (!gdk_disable || gdk_disable[0] == '\0') {
        g_setenv("GDK_DISABLE", "vulkan", TRUE);
    }
}

std::string format_decimal(double value, int precision = 2) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string strip_wrapping_quotes(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2) {
        const auto first = value.front();
        const auto last = value.back();
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
            return value.substr(1, value.size() - 2);
        }
    }

    return value;
}

std::string humanize_identifier(std::string value) {
    for (auto& character : value) {
        if (character == '-' || character == '_') {
            character = ' ';
        }
    }

    value = trim(std::move(value));
    if (!value.empty()) {
        value.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(value.front())));
    }
    return value;
}

std::string normalize_standard_theme(std::string value) {
    if (value == "hyalo" || value == "hyalo-modern" || value == "modern") {
        return "hyalo";
    }
    if (value == "hyalo-glass" || value == "glass") {
        return "glass";
    }
    if (value == "forest-mist" || value == "white" || value == "pastel") {
        return "pastel";
    }
    if (value == "graphite") {
        return "graphite";
    }
    return "hyalo";
}

std::string normalize_color_mode(std::string value) {
    value = lowercase(std::move(value));
    if (value == "dark" || value == "light" || value == "auto") {
        return value;
    }
    return "auto";
}

struct RgbColor {
    int red = 0;
    int green = 0;
    int blue = 0;
};

bool parse_hex_color(const std::string& value, RgbColor& color) {
    if (value.size() != 7 || value.front() != '#') {
        return false;
    }

    try {
        color.red = std::stoi(value.substr(1, 2), nullptr, 16);
        color.green = std::stoi(value.substr(3, 2), nullptr, 16);
        color.blue = std::stoi(value.substr(5, 2), nullptr, 16);
        return true;
    } catch (...) {
        return false;
    }
}

std::string hex_color(const RgbColor& color) {
    std::ostringstream stream;
    stream << '#'
           << std::hex << std::setfill('0') << std::nouppercase
           << std::setw(2) << std::clamp(color.red, 0, 255)
           << std::setw(2) << std::clamp(color.green, 0, 255)
           << std::setw(2) << std::clamp(color.blue, 0, 255);
    return lowercase(stream.str());
}

std::string mix_hex(const std::string& first, const std::string& second, double amount) {
    auto left = RgbColor{};
    auto right = RgbColor{};
    if (!parse_hex_color(first, left) || !parse_hex_color(second, right)) {
        return first;
    }

    const auto clamped = std::clamp(amount, 0.0, 1.0);
    const auto mix_channel = [clamped](int lhs, int rhs) {
        return static_cast<int>(std::lround((static_cast<double>(lhs) * (1.0 - clamped)) + (static_cast<double>(rhs) * clamped)));
    };

    return hex_color(RgbColor{
        .red = mix_channel(left.red, right.red),
        .green = mix_channel(left.green, right.green),
        .blue = mix_channel(left.blue, right.blue),
    });
}

std::string alpha_expr(const std::string& color, double opacity) {
    return "alpha(" + color + ", " + format_decimal(std::clamp(opacity, 0.0, 1.0), 2) + ")";
}

std::string accent_contrast_color(const std::string& accent) {
    auto rgb = RgbColor{};
    if (!parse_hex_color(accent, rgb)) {
        return "#18212e";
    }

    const auto brightness = (rgb.red * 299) + (rgb.green * 587) + (rgb.blue * 114);
    return brightness >= 150000 ? "#18212e" : "#f7fafc";
}

struct ThemeTokens {
    std::string bg_desktop;
    std::string bg_window;
    std::string bg_panel;
    std::string bg_card;
    std::string bg_card_alt;
    std::string bg_raised;
    std::string bg_hover;
    std::string bg_pressed;
    std::string bg_input;
    std::string accent;
    std::string accent_strong;
    std::string accent_contrast;
    std::string text_main;
    std::string text_secondary;
    std::string text_muted;
    std::string border;
    std::string border_strong;
    std::string panel_border;
    std::string focus_ring;
    std::string success;
    std::string success_text;
    std::string danger;
    std::string danger_text;
    std::string shadow_panel;
    std::string shadow_menu;
    std::string shadow_modal;
    std::string overlay_subtle;
    std::string overlay_soft;
    std::string overlay_strong;
};

ThemeTokens build_theme_tokens(const std::string& theme, const std::string& effective_mode, const std::string& accent, double transparency) {
    const auto dark = effective_mode == "dark";
    const auto surface_alpha = dark
        ? std::clamp(0.70 + (transparency * 0.22), 0.70, 0.92)
        : std::clamp(0.78 + (transparency * 0.18), 0.78, 0.96);
    const auto panel_alpha = dark
        ? std::clamp(0.74 + (transparency * 0.18), 0.74, 0.90)
        : std::clamp(0.72 + (transparency * 0.20), 0.72, 0.94);

    auto tokens = ThemeTokens{};

    if (theme == "graphite" || theme == "hyalo") {
        if (dark) {
            tokens.bg_desktop = "#10141b";
            tokens.bg_window = "#171c25";
            tokens.bg_panel = alpha_expr("#1d2430", panel_alpha);
            tokens.bg_card = alpha_expr("#1d232d", surface_alpha);
            tokens.bg_card_alt = alpha_expr("#232b36", std::min(0.98, surface_alpha + 0.04));
            tokens.bg_raised = alpha_expr("#2a3440", std::min(0.99, surface_alpha + 0.06));
            tokens.bg_hover = alpha_expr("#2f3947", std::min(0.99, surface_alpha + 0.08));
            tokens.bg_pressed = alpha_expr("#384353", std::min(1.0, surface_alpha + 0.10));
            tokens.bg_input = alpha_expr("#202733", std::min(0.99, surface_alpha + 0.03));
            tokens.text_main = "#f3f6fb";
            tokens.text_secondary = "#b3bdcb";
            tokens.text_muted = "#909cad";
            tokens.border = alpha_expr("#ffffff", 0.08);
            tokens.border_strong = alpha_expr("#ffffff", 0.14);
            tokens.panel_border = alpha_expr("#ffffff", 0.10);
            tokens.success = mix_hex(accent, "#ffffff", 0.18);
            tokens.success_text = "#18212e";
            tokens.danger = "#6d3e40";
            tokens.danger_text = "#ffe7e0";
            tokens.shadow_panel = alpha_expr("#05070c", 0.22);
            tokens.shadow_menu = alpha_expr("#05070c", 0.24);
            tokens.shadow_modal = alpha_expr("#04060a", 0.26);
            tokens.overlay_subtle = alpha_expr("#ffffff", 0.03);
            tokens.overlay_soft = alpha_expr("#ffffff", 0.05);
            tokens.overlay_strong = alpha_expr("#ffffff", 0.08);
        } else {
            tokens.bg_desktop = "#eef1f5";
            tokens.bg_window = "#fbfcfe";
            tokens.bg_panel = alpha_expr("#e8edf3", panel_alpha);
            tokens.bg_card = alpha_expr("#f8fafc", surface_alpha);
            tokens.bg_card_alt = alpha_expr("#eef2f6", std::min(0.99, surface_alpha + 0.03));
            tokens.bg_raised = alpha_expr("#ffffff", std::min(1.0, surface_alpha + 0.06));
            tokens.bg_hover = alpha_expr("#e6ebf1", std::min(1.0, surface_alpha + 0.08));
            tokens.bg_pressed = alpha_expr("#dce3eb", std::min(1.0, surface_alpha + 0.10));
            tokens.bg_input = alpha_expr("#ffffff", std::min(1.0, surface_alpha + 0.08));
            tokens.text_main = "#1f2933";
            tokens.text_secondary = "#5c6978";
            tokens.text_muted = "#7a8795";
            tokens.border = alpha_expr("#24313a", 0.10);
            tokens.border_strong = alpha_expr("#24313a", 0.16);
            tokens.panel_border = alpha_expr("#24313a", 0.10);
            tokens.success = mix_hex(accent, "#ffffff", 0.64);
            tokens.success_text = "#1f2933";
            tokens.danger = "#f2d9d5";
            tokens.danger_text = "#7a433c";
            tokens.shadow_panel = alpha_expr("#7d8792", 0.14);
            tokens.shadow_menu = alpha_expr("#6f7b88", 0.16);
            tokens.shadow_modal = alpha_expr("#65717f", 0.18);
            tokens.overlay_subtle = alpha_expr("#ffffff", 0.44);
            tokens.overlay_soft = alpha_expr("#ffffff", 0.66);
            tokens.overlay_strong = alpha_expr("#ffffff", 0.84);
        }
    } else if (theme == "glass") {
        if (dark) {
            tokens.bg_desktop = "#0f151b";
            tokens.bg_window = "#151d25";
            tokens.bg_panel = alpha_expr("#1b2831", panel_alpha);
            tokens.bg_card = alpha_expr("#1e2a33", surface_alpha);
            tokens.bg_card_alt = alpha_expr("#23323d", std::min(0.99, surface_alpha + 0.03));
            tokens.bg_raised = alpha_expr("#283843", std::min(1.0, surface_alpha + 0.06));
            tokens.bg_hover = alpha_expr("#30414d", std::min(1.0, surface_alpha + 0.08));
            tokens.bg_pressed = alpha_expr("#3a4d5b", std::min(1.0, surface_alpha + 0.10));
            tokens.bg_input = alpha_expr("#22313b", std::min(1.0, surface_alpha + 0.04));
            tokens.text_main = "#eef7fb";
            tokens.text_secondary = "#b7cad3";
            tokens.text_muted = "#90a4ae";
            tokens.border = alpha_expr("#ffffff", 0.10);
            tokens.border_strong = alpha_expr("#ffffff", 0.16);
            tokens.panel_border = alpha_expr("#ffffff", 0.12);
            tokens.success = mix_hex(accent, "#ffffff", 0.22);
            tokens.success_text = "#182633";
            tokens.danger = "#704443";
            tokens.danger_text = "#fff0ec";
            tokens.shadow_panel = alpha_expr("#071018", 0.18);
            tokens.shadow_menu = alpha_expr("#071018", 0.20);
            tokens.shadow_modal = alpha_expr("#071018", 0.22);
            tokens.overlay_subtle = alpha_expr("#ffffff", 0.08);
            tokens.overlay_soft = alpha_expr("#ffffff", 0.12);
            tokens.overlay_strong = alpha_expr("#ffffff", 0.18);
        } else {
            tokens.bg_desktop = "#eef2f7";
            tokens.bg_window = "#ffffff";
            tokens.bg_panel = alpha_expr("#edf2f8", panel_alpha);
            tokens.bg_card = alpha_expr("#ffffff", surface_alpha);
            tokens.bg_card_alt = alpha_expr("#f4f6f9", std::min(0.99, surface_alpha - 0.04));
            tokens.bg_raised = alpha_expr("#ffffff", std::min(1.0, surface_alpha + 0.04));
            tokens.bg_hover = alpha_expr("#f6f7fa", std::min(1.0, surface_alpha + 0.06));
            tokens.bg_pressed = alpha_expr("#e8ebf0", std::min(1.0, surface_alpha + 0.08));
            tokens.bg_input = alpha_expr("#ffffff", std::min(1.0, surface_alpha + 0.08));
            tokens.text_main = "#13212b";
            tokens.text_secondary = "#546570";
            tokens.text_muted = "#76848d";
            tokens.border = alpha_expr("#102030", 0.08);
            tokens.border_strong = alpha_expr("#102030", 0.12);
            tokens.panel_border = alpha_expr("#102030", 0.08);
            tokens.success = mix_hex(accent, "#ffffff", 0.70);
            tokens.success_text = "#182633";
            tokens.danger = "#f8dfdc";
            tokens.danger_text = "#7a433c";
            tokens.shadow_panel = alpha_expr("#6a7683", 0.12);
            tokens.shadow_menu = alpha_expr("#798797", 0.14);
            tokens.shadow_modal = alpha_expr("#6d7a88", 0.16);
            tokens.overlay_subtle = alpha_expr("#ffffff", 0.54);
            tokens.overlay_soft = alpha_expr("#ffffff", 0.82);
            tokens.overlay_strong = alpha_expr("#ffffff", 0.94);
        }
    } else {
        if (dark) {
            tokens.bg_desktop = "#19181d";
            tokens.bg_window = "#211f26";
            tokens.bg_panel = alpha_expr("#262631", panel_alpha);
            tokens.bg_card = alpha_expr("#292731", surface_alpha);
            tokens.bg_card_alt = alpha_expr("#312f39", std::min(0.99, surface_alpha + 0.03));
            tokens.bg_raised = alpha_expr("#36333d", std::min(1.0, surface_alpha + 0.06));
            tokens.bg_hover = alpha_expr("#403c47", std::min(1.0, surface_alpha + 0.08));
            tokens.bg_pressed = alpha_expr("#494550", std::min(1.0, surface_alpha + 0.10));
            tokens.bg_input = alpha_expr("#2e2b34", std::min(1.0, surface_alpha + 0.03));
            tokens.text_main = "#f4f0ee";
            tokens.text_secondary = "#c8bec2";
            tokens.text_muted = "#9d9297";
            tokens.border = alpha_expr("#ffffff", 0.08);
            tokens.border_strong = alpha_expr("#ffffff", 0.14);
            tokens.panel_border = alpha_expr("#ffffff", 0.10);
            tokens.success = mix_hex(accent, "#ffffff", 0.18);
            tokens.success_text = "#1c2624";
            tokens.danger = "#5a353b";
            tokens.danger_text = "#ffe9e6";
            tokens.shadow_panel = alpha_expr("#09090d", 0.20);
            tokens.shadow_menu = alpha_expr("#09090d", 0.22);
            tokens.shadow_modal = alpha_expr("#09090d", 0.24);
            tokens.overlay_subtle = alpha_expr("#ffffff", 0.04);
            tokens.overlay_soft = alpha_expr("#ffffff", 0.06);
            tokens.overlay_strong = alpha_expr("#ffffff", 0.10);
        } else {
            tokens.bg_desktop = "#f3f1ef";
            tokens.bg_window = "#fffaf7";
            tokens.bg_panel = alpha_expr("#e7f2ef", panel_alpha);
            tokens.bg_card = alpha_expr("#fdf9f7", surface_alpha);
            tokens.bg_card_alt = alpha_expr("#f3ecea", std::min(1.0, surface_alpha - 0.04));
            tokens.bg_raised = alpha_expr("#f6f0ee", std::min(1.0, surface_alpha + 0.02));
            tokens.bg_hover = alpha_expr("#efe6eb", std::min(1.0, surface_alpha + 0.06));
            tokens.bg_pressed = alpha_expr("#e2d8df", std::min(1.0, surface_alpha + 0.08));
            tokens.bg_input = alpha_expr("#fffefe", std::min(1.0, surface_alpha + 0.08));
            tokens.text_main = "#24313a";
            tokens.text_secondary = "#6a7b80";
            tokens.text_muted = "#7f8f94";
            tokens.border = alpha_expr("#d6e4e0", 1.0);
            tokens.border_strong = alpha_expr("#bfd2cc", 1.0);
            tokens.panel_border = alpha_expr("#d1e2dd", 1.0);
            tokens.success = mix_hex(accent, "#ffffff", 0.62);
            tokens.success_text = "#213530";
            tokens.danger = "#f3deda";
            tokens.danger_text = "#7a433c";
            tokens.shadow_panel = alpha_expr("#978d89", 0.16);
            tokens.shadow_menu = alpha_expr("#8f8682", 0.18);
            tokens.shadow_modal = alpha_expr("#807874", 0.20);
            tokens.overlay_subtle = alpha_expr("#ffffff", 0.55);
            tokens.overlay_soft = alpha_expr("#ffffff", 0.72);
            tokens.overlay_strong = alpha_expr("#ffffff", 0.88);
        }
    }

    tokens.accent = accent;
    tokens.accent_strong = dark
        ? mix_hex(accent, "#ffffff", 0.18)
        : mix_hex(accent, "#1a2430", 0.18);
    tokens.accent_contrast = accent_contrast_color(accent);
    tokens.focus_ring = alpha_expr(accent, dark ? 0.34 : 0.26);

    return tokens;
}

bool has_image_extension(const std::filesystem::path& path) {
    const auto extension = lowercase(path.extension().string());
    return extension == ".jpg"
        || extension == ".jpeg"
        || extension == ".png"
        || extension == ".webp"
        || extension == ".jxl"
        || extension == ".bmp";
}

std::string file_uri_to_path(const std::string& value) {
    const auto unquoted = strip_wrapping_quotes(value);
    if (unquoted.rfind("file://", 0) != 0) {
        return unquoted;
    }

    GError* error = nullptr;
    char* path = g_filename_from_uri(unquoted.c_str(), nullptr, &error);
    if (path == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return {};
    }

    std::string resolved{path};
    g_free(path);
    return resolved;
}

std::string path_to_file_uri(const std::filesystem::path& path) {
    GError* error = nullptr;
    char* uri = g_filename_to_uri(path.string().c_str(), nullptr, &error);
    if (uri == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return {};
    }

    std::string resolved{uri};
    g_free(uri);
    return resolved;
}

std::string shell_quote(const std::string& value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');
    for (const auto character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::string shell_double_quote(const std::string& value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (const auto character : value) {
        if (character == '\\' || character == '"' || character == '$' || character == '`') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

std::string xml_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const auto character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '\"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

std::string replace_managed_block(
    const std::string& text,
    const std::string& begin_marker,
    const std::string& end_marker,
    const std::string& replacement
) {
    const auto begin = text.find(begin_marker);
    if (begin == std::string::npos) {
        return text + replacement;
    }

    const auto end = text.find(end_marker, begin);
    if (end == std::string::npos) {
        return text.substr(0, begin) + replacement;
    }

    return text.substr(0, begin) + replacement + text.substr(end + end_marker.size());
}

std::string current_session_type() {
    if (const char* session_type = std::getenv("XDG_SESSION_TYPE")) {
        return session_type;
    }

    return "unknown";
}

std::string current_desktop_name() {
    if (const char* desktop = std::getenv("XDG_CURRENT_DESKTOP")) {
        return desktop;
    }

    return "HyaloOS";
}

std::string current_host_name() {
    struct utsname system_info {};
    if (uname(&system_info) == 0) {
        return system_info.nodename;
    }

    if (const char* host = std::getenv("HOSTNAME")) {
        return host;
    }

    return "unknown";
}

std::string current_kernel_version() {
    struct utsname system_info {};
    if (uname(&system_info) == 0) {
        return system_info.release;
    }

    return "unknown";
}

GdkMonitor* select_primary_monitor(GdkDisplay* display) {
    if (!display) {
        return nullptr;
    }

    auto* monitors = gdk_display_get_monitors(display);
    if (!monitors) {
        return nullptr;
    }

    const auto monitor_count = g_list_model_get_n_items(G_LIST_MODEL(monitors));
    GdkMonitor* fallback_monitor = nullptr;

    for (guint index = 0; index < monitor_count; ++index) {
        auto* monitor = GDK_MONITOR(g_list_model_get_item(G_LIST_MODEL(monitors), index));
        if (!monitor) {
            continue;
        }

        if (!fallback_monitor) {
            fallback_monitor = monitor;
        }

        GdkRectangle geometry{};
        gdk_monitor_get_geometry(monitor, &geometry);
        if (geometry.x <= 0 && 0 < geometry.x + geometry.width
            && geometry.y <= 0 && 0 < geometry.y + geometry.height) {
            if (fallback_monitor != monitor) {
                g_object_unref(fallback_monitor);
            }
            return monitor;
        }

        g_object_unref(monitor);
    }

    return fallback_monitor;
}

struct DesktopAppEntry {
    std::string app_id;
    std::string name;
    std::string icon_name;
};

std::vector<DesktopAppEntry> discover_desktop_apps() {
    std::vector<DesktopAppEntry> result;
    std::set<std::string> seen_ids;

    auto* app_list = g_app_info_get_all();
    for (auto* iter = app_list; iter; iter = iter->next) {
        auto* app_info = G_APP_INFO(iter->data);
        if (!G_IS_DESKTOP_APP_INFO(app_info)) {
            g_object_unref(app_info);
            continue;
        }

        auto* desktop_info = G_DESKTOP_APP_INFO(app_info);
        if (g_desktop_app_info_get_nodisplay(desktop_info)
            || !g_app_info_should_show(app_info)) {
            g_object_unref(app_info);
            continue;
        }

        const auto* raw_id = g_app_info_get_id(app_info);
        if (!raw_id) {
            g_object_unref(app_info);
            continue;
        }

        auto id = std::string{raw_id};
        if (id.size() > 8 && id.substr(id.size() - 8) == ".desktop") {
            id = id.substr(0, id.size() - 8);
        }

        if (seen_ids.count(id)) {
            g_object_unref(app_info);
            continue;
        }
        seen_ids.insert(id);

        const auto* raw_name = g_app_info_get_display_name(app_info);
        auto* icon = g_app_info_get_icon(app_info);
        auto icon_name = std::string{"application-x-executable"};
        if (icon) {
            auto* icon_str = g_icon_to_string(icon);
            if (icon_str) {
                icon_name = icon_str;
                g_free(icon_str);
            }
        }

        result.push_back({id, raw_name ? std::string{raw_name} : id, icon_name});
        g_object_unref(app_info);
    }

    g_list_free(app_list);

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return result;
}

Gtk::Label* make_label(
    const Glib::ustring& text,
    const char* css_class,
    bool wrap = false,
    Gtk::Align align = Gtk::Align::START
) {
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_text(text);
    label->set_halign(align);
    label->set_xalign(align == Gtk::Align::CENTER ? 0.5f : 0.0f);
    label->set_wrap(wrap);
    label->add_css_class(css_class);
    return label;
}

class ControlCenterWindow : public Gtk::Window {
public:
    ControlCenterWindow(
        hyalo::core::ConfigManager& config_manager,
        hyalo::core::Localization& localization
    )
        : config_manager_(config_manager)
        , localization_(localization) {
        sync_from_config();

        set_title(tr("settings"));
        add_css_class("hyalo-control-center");

        configure_window_size();
        configure_shell();
        populate_pages();
        sync_widgets_from_config();
        select_page("appearance");
        refresh_runtime_status();
    }

private:
    struct NavItem {
        std::string page_name;
        std::string title;
        std::string subtitle;
        std::string keywords;
        Gtk::Button* button = nullptr;
    };

    struct AccentOption {
        const char* label_key;
        const char* color;
        const char* css_class;
    };

    struct BoundSwitch {
        Gtk::Switch* widget = nullptr;
        std::vector<std::string> path;
    };

    struct BoundCombo {
        Gtk::ComboBoxText* widget = nullptr;
        std::vector<std::string> path;
        std::vector<std::string> values;
    };

    struct BoundScale {
        Gtk::Scale* widget = nullptr;
        std::vector<std::string> path;
    };

    struct OutputWallpaperBinding {
        std::string output_name;
        Gtk::ComboBoxText* combo = nullptr;
    };

    struct DisplayOutputPreview {
        std::string name;
        int width = 1920;
        int height = 1080;
        int x = 0;
        int y = 0;
        double scale = 1.0;
        int dpi = 0;
        std::string refresh_rate;
    };
    struct DisplayCapabilities {
        std::vector<std::string> resolution_labels;
        std::vector<std::string> resolution_values;
        std::vector<std::string> refresh_labels;
        std::vector<std::string> refresh_values;
        std::map<std::string, std::vector<std::string>> refresh_per_resolution;
    };

    std::string tr(const std::string& key) const {
        return localization_.translate(key);
    }

    std::vector<std::string> translated_options(std::initializer_list<const char*> keys) const {
        std::vector<std::string> options;
        options.reserve(keys.size());
        for (const auto* key : keys) {
            options.push_back(tr(key));
        }
        return options;
    }

    static int index_for_value(const std::vector<std::string>& values, const std::string& wanted) {
        const auto iterator = std::find(values.begin(), values.end(), wanted);
        if (iterator == values.end()) {
            return 0;
        }

        return static_cast<int>(std::distance(values.begin(), iterator));
    }

    const nlohmann::json* find_config_value(const std::vector<std::string>& path) const {
        if (path.empty()) {
            return nullptr;
        }

        auto* cursor = &config_manager_.raw();
        for (const auto& segment : path) {
            if (!cursor->is_object() || !cursor->contains(segment)) {
                return nullptr;
            }
            cursor = &(*cursor)[segment];
        }

        return cursor;
    }

    std::string config_string(const std::vector<std::string>& path, const std::string& fallback) const {
        if (const auto* value = find_config_value(path); value && value->is_string()) {
            return value->get<std::string>();
        }
        return fallback;
    }

    bool config_bool(const std::vector<std::string>& path, bool fallback) const {
        if (const auto* value = find_config_value(path); value && value->is_boolean()) {
            return value->get<bool>();
        }
        return fallback;
    }

    double config_number(const std::vector<std::string>& path, double fallback) const {
        if (const auto* value = find_config_value(path); value && value->is_number()) {
            return value->get<double>();
        }
        return fallback;
    }

    int config_int(const std::vector<std::string>& path, int fallback) const {
        if (const auto* value = find_config_value(path); value && value->is_number_integer()) {
            return value->get<int>();
        }
        if (const auto* value = find_config_value(path); value && value->is_number()) {
            return static_cast<int>(value->get<double>());
        }
        return fallback;
    }

    std::string read_command_output(const std::string& command) const {
        std::array<char, 512> buffer{};
        std::string output;

        FILE* pipe = popen(command.c_str(), "r");
        if (pipe == nullptr) {
            return {};
        }

        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        pclose(pipe);
        return trim(output);
    }

    std::vector<std::string> split_nonempty_lines(const std::string& text) const {
        auto values = std::vector<std::string>{};
        auto stream = std::istringstream(text);
        auto line = std::string{};
        while (std::getline(stream, line)) {
            line = trim(line);
            if (!line.empty()) {
                values.push_back(line);
            }
        }
        return values;
    }

    static std::string strip_utf8_suffix(std::string locale_value) {
        const auto lowered = lowercase(locale_value);
        const auto utf8_pos = lowered.find(".utf-8");
        if (utf8_pos != std::string::npos) {
            return locale_value.substr(0, utf8_pos);
        }

        const auto utf8_short_pos = lowered.find(".utf8");
        if (utf8_short_pos != std::string::npos) {
            return locale_value.substr(0, utf8_short_pos);
        }

        return locale_value;
    }

    std::vector<std::string> discover_system_region_locales() const {
        auto locales = std::vector<std::string>{};
        const auto output = read_command_output(
            "sh -lc 'locale -a 2>/dev/null | grep -Ei \"utf-?8|\\.utf8\" | head -n 160'"
        );
        const auto lines = split_nonempty_lines(output);

        auto unique = std::set<std::string>{};
        for (auto locale_value : lines) {
            locale_value = strip_utf8_suffix(trim(std::move(locale_value)));
            if (!locale_value.empty()) {
                unique.insert(locale_value);
            }
        }

        locales.assign(unique.begin(), unique.end());
        if (locales.empty()) {
            locales = {"pl_PL", "en_US", "de_DE"};
        }

        return locales;
    }

    std::vector<std::string> discover_system_keyboard_layouts() const {
        auto layouts = std::vector<std::string>{};
        const auto output = read_command_output(
            "sh -lc 'command -v localectl >/dev/null 2>&1 && localectl list-x11-keymap-layouts 2>/dev/null | head -n 200'"
        );

        auto unique = std::set<std::string>{};
        for (auto layout : split_nonempty_lines(output)) {
            layout = lowercase(trim(std::move(layout)));
            if (!layout.empty()) {
                unique.insert(layout);
            }
        }

        layouts.assign(unique.begin(), unique.end());
        return layouts;
    }

    std::vector<std::string> discover_de_language_codes() const {
        auto unique = std::set<std::string>{};

        auto collect_from_dir = [&unique](const std::filesystem::path& root) {
            std::error_code error;
            if (!std::filesystem::exists(root, error) || error) {
                return;
            }

            for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
                if (error || !entry.is_regular_file()) {
                    continue;
                }

                const auto path = entry.path();
                if (lowercase(path.extension().string()) != ".json") {
                    continue;
                }

                auto code = lowercase(path.stem().string());
                code = trim(std::move(code));
                if (!code.empty()) {
                    unique.insert(code);
                }
            }
        };

        collect_from_dir(config_manager_.paths().locales_root);
        collect_from_dir(config_manager_.paths().user_config_root / "locales");

        auto codes = std::vector<std::string>(unique.begin(), unique.end());
        if (codes.empty()) {
            codes = {"pl", "en"};
        }
        return codes;
    }

    static std::string language_label_for_code(const std::string& code) {
        if (code == "pl") {
            return "Polski";
        }
        if (code == "en") {
            return "English";
        }
        if (code == "de") {
            return "Deutsch";
        }
        if (code == "fr") {
            return "Francais";
        }
        if (code == "es") {
            return "Espanol";
        }
        if (code == "it") {
            return "Italiano";
        }
        return humanize_identifier(code);
    }

    void refresh_language_region_keyboard_options() {
        language_values_ = discover_de_language_codes();
        language_labels_.clear();
        language_labels_.reserve(language_values_.size());
        for (const auto& code : language_values_) {
            language_labels_.push_back(language_label_for_code(code));
        }

        if (std::find(language_values_.begin(), language_values_.end(), language_code_) == language_values_.end()) {
            language_values_.push_back(language_code_);
            language_labels_.push_back(language_label_for_code(language_code_));
        }

        region_locale_values_ = discover_system_region_locales();
        const auto selected_region = config_string({"region", "locale"}, "pl_PL");
        if (std::find(region_locale_values_.begin(), region_locale_values_.end(), selected_region) == region_locale_values_.end()) {
            region_locale_values_.push_back(selected_region);
        }
        region_locale_labels_ = region_locale_values_;

        keyboard_layout_values_.clear();
        keyboard_layout_labels_.clear();

        auto add_kb = [&](const std::string& value, const std::string& tr_key) {
            keyboard_layout_values_.push_back(value);
            keyboard_layout_labels_.push_back(tr(tr_key));
        };

        add_kb("pl-programmer", "cc_option_keyboard_pl");
        add_kb("pl:qwertz",     "cc_option_keyboard_pl_typewriter");
        add_kb("us",            "cc_option_keyboard_us");
        add_kb("us-intl",       "cc_option_keyboard_us_intl");
        add_kb("gb",            "cc_option_keyboard_gb");
        add_kb("de",            "cc_option_keyboard_de");
        add_kb("de:nodeadkeys", "cc_option_keyboard_de_nodeadkeys");
        add_kb("fr",            "cc_option_keyboard_fr");
        add_kb("es",            "cc_option_keyboard_es");
        add_kb("it",            "cc_option_keyboard_it");
        add_kb("pt",            "cc_option_keyboard_pt");
        add_kb("br",            "cc_option_keyboard_br");
        add_kb("cz",            "cc_option_keyboard_cz");
        add_kb("sk",            "cc_option_keyboard_sk");
        add_kb("hu",            "cc_option_keyboard_hu");
        add_kb("ro",            "cc_option_keyboard_ro");
        add_kb("tr",            "cc_option_keyboard_tr");
        add_kb("se",            "cc_option_keyboard_se");
        add_kb("no",            "cc_option_keyboard_no");
        add_kb("dk",            "cc_option_keyboard_dk");
        add_kb("fi",            "cc_option_keyboard_fi");
        add_kb("nl",            "cc_option_keyboard_nl");
        add_kb("be",            "cc_option_keyboard_be");
        add_kb("ch",            "cc_option_keyboard_ch");
        add_kb("ru",            "cc_option_keyboard_ru");
        add_kb("ua",            "cc_option_keyboard_ua");
        add_kb("jp",            "cc_option_keyboard_jp");
        add_kb("kr",            "cc_option_keyboard_kr");

        const auto system_layouts = discover_system_keyboard_layouts();
        for (const auto& layout : system_layouts) {
            if (std::find(keyboard_layout_values_.begin(), keyboard_layout_values_.end(), layout) != keyboard_layout_values_.end()) {
                continue;
            }
            keyboard_layout_values_.push_back(layout);
            keyboard_layout_labels_.push_back(layout + " (system)");
        }

        const auto configured_layout = config_string({"input", "keyboard_layout"}, "pl-programmer");
        if (std::find(keyboard_layout_values_.begin(), keyboard_layout_values_.end(), configured_layout) == keyboard_layout_values_.end()) {
            keyboard_layout_values_.push_back(configured_layout);
            keyboard_layout_labels_.push_back(configured_layout + " (custom)");
        }
    }

    std::string read_gsettings_string(const std::string& schema, const std::string& key) const {
        return strip_wrapping_quotes(read_command_output(
            "sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings get "
            + schema
            + " "
            + key
            + " 2>/dev/null'"
        ));
    }

    bool system_prefers_dark() const {
        return read_gsettings_string("org.gnome.desktop.interface", "color-scheme") == "prefer-dark";
    }

    std::string effective_color_mode() const {
        const auto configured = normalize_color_mode(
            config_string({"appearance", "color_mode"}, selected_color_mode_.empty() ? std::string{"auto"} : selected_color_mode_)
        );
        if (configured == "auto") {
            return system_prefers_dark() ? "dark" : "light";
        }
        return configured;
    }

    std::string gtk_setting_string(const char* property_name, const std::string& fallback = {}) const {
        const auto settings = Gtk::Settings::get_default();
        if (!settings) {
            return fallback;
        }

        char* value = nullptr;
        g_object_get(settings->gobj(), property_name, &value, nullptr);
        if (value == nullptr) {
            return fallback;
        }

        std::string resolved{value};
        g_free(value);
        return resolved.empty() ? fallback : resolved;
    }

    std::pair<std::string, int> current_system_font() const {
        const auto font_name = gtk_setting_string("gtk-font-name", config_string({"appearance", "font_family"}, "Sans") + " 11");
        const auto last_space = font_name.find_last_of(' ');
        if (last_space == std::string::npos) {
            return {font_name, 11};
        }

        const auto family = trim(font_name.substr(0, last_space));
        const auto size_text = trim(font_name.substr(last_space + 1));
        try {
            return {family.empty() ? std::string{"Sans"} : family, std::max(1, std::stoi(size_text))};
        } catch (...) {
            return {font_name, 11};
        }
    }

    std::string current_system_wallpaper() const {
        auto uri = read_gsettings_string("org.gnome.desktop.background", "picture-uri");
        if (uri.empty()) {
            uri = read_gsettings_string("org.gnome.desktop.background", "picture-uri-dark");
        }
        return file_uri_to_path(uri);
    }

    std::string current_labwc_window_theme() const {
        const auto rc_path = resolve_labwc_rc_path();
        auto rc_text = read_text_file(rc_path);
        if (rc_text.empty()) {
            rc_text = read_text_file(config_manager_.paths().defaults_root.parent_path() / "labwc" / "rc.xml");
        }

        const auto theme_begin = rc_text.find("<theme>");
        const auto name_begin = rc_text.find("<name>", theme_begin);
        const auto name_end = rc_text.find("</name>", name_begin);
        if (theme_begin == std::string::npos || name_begin == std::string::npos || name_end == std::string::npos) {
            return {};
        }

        return trim(rc_text.substr(name_begin + 6, name_end - (name_begin + 6)));
    }

    static void ensure_option(std::vector<std::string>& labels, std::vector<std::string>& values, const std::string& value, const std::string& label = {}) {
        if (value.empty() || std::find(values.begin(), values.end(), value) != values.end()) {
            return;
        }

        values.insert(values.begin(), value);
        labels.insert(labels.begin(), label.empty() ? humanize_identifier(value) : label);
    }

    std::vector<std::string> scan_named_directories(const std::vector<std::filesystem::path>& roots) const {
        std::set<std::string> items;
        for (const auto& root : roots) {
            std::error_code error;
            if (!std::filesystem::exists(root, error) || !std::filesystem::is_directory(root, error)) {
                continue;
            }

            for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
                if (error || !entry.is_directory()) {
                    continue;
                }
                items.insert(entry.path().filename().string());
            }
        }

        return {items.begin(), items.end()};
    }

    std::vector<std::string> scan_cursor_themes() const {
        std::set<std::string> items;
        const auto icon_roots = std::vector<std::filesystem::path>{
            resolve_xdg_config_home().parent_path() / ".icons",
            std::filesystem::path{std::getenv("HOME") ? std::getenv("HOME") : ""} / ".local/share/icons",
            "/usr/share/icons",
            "/usr/local/share/icons"
        };

        for (const auto& root : icon_roots) {
            std::error_code error;
            if (!std::filesystem::exists(root, error) || !std::filesystem::is_directory(root, error)) {
                continue;
            }

            for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
                if (error || !entry.is_directory()) {
                    continue;
                }
                if (std::filesystem::exists(entry.path() / "cursors", error)) {
                    items.insert(entry.path().filename().string());
                }
            }
        }

        return {items.begin(), items.end()};
    }

    std::vector<std::string> scan_font_families() const {
        std::set<std::string> items;
        const auto output = read_command_output("sh -lc 'command -v fc-list >/dev/null 2>&1 && fc-list : family 2>/dev/null | head -n 300'");
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            std::stringstream family_stream(line);
            std::string family;
            while (std::getline(family_stream, family, ',')) {
                family = trim(family);
                if (!family.empty()) {
                    items.insert(family);
                }
            }
        }

        return {items.begin(), items.end()};
    }

    std::vector<std::string> scan_wallpapers() const {
        std::vector<std::filesystem::path> roots;
        if (const char* home = std::getenv("HOME")) {
            roots.emplace_back(std::filesystem::path(home) / "Pictures/Wallpapers");
            roots.emplace_back(std::filesystem::path(home) / ".local/share/backgrounds");
            roots.emplace_back(std::filesystem::path(home) / "Pictures");
        }
        roots.emplace_back(config_manager_.paths().user_config_root / "wallpapers");
        roots.emplace_back("/usr/share/backgrounds");
        roots.emplace_back("/usr/share/wallpapers");

        std::set<std::string> items;
        for (const auto& root : roots) {
            std::error_code error;
            if (!std::filesystem::exists(root, error) || !std::filesystem::is_directory(root, error)) {
                continue;
            }

            auto iterator = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
            for (const auto& entry : iterator) {
                if (error || !entry.is_regular_file()) {
                    continue;
                }
                if (!has_image_extension(entry.path())) {
                    continue;
                }
                if (entry.path().string().find("/Screenshots/") != std::string::npos) {
                    continue;
                }
                items.insert(entry.path().string());
                if (items.size() >= 96) {
                    return {items.begin(), items.end()};
                }
            }
        }

        return {items.begin(), items.end()};
    }

    void refresh_appearance_catalogs() {
        theme_values_ = {"hyalo", "pastel", "graphite", "glass"};
        theme_labels_ = {
            tr("cc_option_theme_hyalo"),
            tr("cc_option_theme_forest_mist"),
            tr("cc_option_theme_graphite"),
            tr("cc_option_theme_hyalo_glass")
        };

        icon_pack_values_ = scan_named_directories({
            std::filesystem::path{std::getenv("HOME") ? std::getenv("HOME") : ""} / ".icons",
            std::filesystem::path{std::getenv("HOME") ? std::getenv("HOME") : ""} / ".local/share/icons",
            "/usr/share/icons",
            "/usr/local/share/icons",
            config_manager_.paths().assets_root.parent_path() / "icons"
        });
        icon_pack_labels_ = icon_pack_values_;

        cursor_theme_values_ = scan_cursor_themes();
        cursor_theme_labels_ = cursor_theme_values_;

        window_theme_values_ = scan_named_directories({
            resolve_labwc_config_dir() / "themes",
            config_manager_.paths().defaults_root.parent_path() / "labwc" / "themes"
        });
        window_theme_labels_ = window_theme_values_;

        font_values_ = scan_font_families();
        font_labels_ = font_values_;

        wallpaper_values_ = scan_wallpapers();
        wallpaper_labels_.clear();
        wallpaper_labels_.reserve(wallpaper_values_.size());
        for (const auto& wallpaper : wallpaper_values_) {
            wallpaper_labels_.push_back(std::filesystem::path(wallpaper).filename().string());
        }

        ensure_option(icon_pack_labels_, icon_pack_values_, selected_icon_pack_);
        ensure_option(cursor_theme_labels_, cursor_theme_values_, selected_cursor_theme_);
        ensure_option(window_theme_labels_, window_theme_values_, selected_window_theme_);
        ensure_option(font_labels_, font_values_, config_string({"appearance", "font_family"}, "Sans"));
        ensure_option(wallpaper_labels_, wallpaper_values_, config_string({"appearance", "wallpaper"}, current_system_wallpaper()), std::filesystem::path(config_string({"appearance", "wallpaper"}, current_system_wallpaper())).filename().string());
    }

    std::filesystem::path user_wallpaper_library_path() const {
        return config_manager_.paths().user_config_root / "wallpapers";
    }

    void refresh_wallpaper_options(const std::string& selected_wallpaper = {}) {
        refresh_appearance_catalogs();

        auto selected = selected_wallpaper;
        if (selected.empty()) {
            selected = config_string({"appearance", "wallpaper"}, current_system_wallpaper());
        }

        for (auto& binding : bound_combos_) {
            if (binding.widget == wallpaper_combo_) {
                binding.values = wallpaper_values_;
                continue;
            }

            for (const auto& output_binding : output_wallpaper_combos_) {
                if (binding.widget == output_binding.combo) {
                    binding.values = wallpaper_values_;
                }
            }
        }

        if (wallpaper_combo_ == nullptr) {
            return;
        }

        updating_widgets_ = true;
        wallpaper_combo_->remove_all();
        for (const auto& label : wallpaper_labels_) {
            wallpaper_combo_->append(label);
        }
        wallpaper_combo_->set_active(index_for_value(wallpaper_values_, selected));

        for (const auto& output_binding : output_wallpaper_combos_) {
            if (output_binding.combo == nullptr) {
                continue;
            }

            output_binding.combo->remove_all();
            for (const auto& label : wallpaper_labels_) {
                output_binding.combo->append(label);
            }
            output_binding.combo->set_active(index_for_value(wallpaper_values_, output_wallpaper_value(output_binding.output_name)));
        }

        update_wallpaper_preview(selected);
        updating_widgets_ = false;
    }

    std::filesystem::path unique_wallpaper_destination(const std::filesystem::path& source_path) const {
        const auto destination_root = user_wallpaper_library_path();
        const auto extension = lowercase(source_path.extension().string());
        const auto base_name = source_path.stem().string();

        auto candidate = destination_root / source_path.filename();
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }

        for (int index = 2; index < 1000; ++index) {
            candidate = destination_root / (base_name + "-" + std::to_string(index) + extension);
            if (!std::filesystem::exists(candidate)) {
                return candidate;
            }
        }

        return destination_root / (base_name + "-imported" + extension);
    }

    void import_wallpaper_from_path(const std::filesystem::path& source_path) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(source_path, error) || !has_image_extension(source_path)) {
            set_status_caption(tr("cc_status_wallpaper_import_failed"));
            return;
        }

        const auto source = std::filesystem::weakly_canonical(source_path, error);
        if (error) {
            set_status_caption(tr("cc_status_wallpaper_import_failed"));
            return;
        }

        const auto destination_root = user_wallpaper_library_path();
        std::filesystem::create_directories(destination_root, error);
        if (error) {
            set_status_caption(tr("cc_status_wallpaper_import_failed"));
            return;
        }

        auto destination = unique_wallpaper_destination(source_path);
        if (source.parent_path() != destination_root) {
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                set_status_caption(tr("cc_status_wallpaper_import_failed"));
                return;
            }
        } else {
            destination = source;
        }

        config_manager_.set_value({"appearance", "wallpaper"}, destination.string());
        config_manager_.set_value({"appearance", "daily_wallpaper"}, false);
        config_manager_.set_value({"appearance", "wallpaper_slideshow"}, false);
        refresh_wallpaper_options(destination.string());
        persist_config("cc_status_wallpaper_imported", [this]() {
            apply_wallpaper_runtime();
            clear_wallpaper_changes_pending();
        });
    }

    void open_wallpaper_import_dialog() {
        auto dialog = Gtk::FileChooserNative::create(
            tr("cc_dialog_wallpaper_import_title"),
            *this,
            Gtk::FileChooser::Action::OPEN,
            tr("cc_action_import_wallpaper"),
            tr("cc_action_cancel")
        );
        dialog->set_modal(true);

        const auto image_filter = Gtk::FileFilter::create();
        image_filter->set_name(tr("cc_filter_images"));
        image_filter->add_mime_type("image/*");
        image_filter->add_pattern("*.jpg");
        image_filter->add_pattern("*.jpeg");
        image_filter->add_pattern("*.png");
        image_filter->add_pattern("*.webp");
        image_filter->add_pattern("*.jxl");
        image_filter->add_pattern("*.bmp");
        dialog->add_filter(image_filter);
        dialog->set_filter(image_filter);

        wallpaper_import_dialog_ = dialog;
        dialog->signal_response().connect([this, dialog](int response_id) {
            if (response_id == static_cast<int>(Gtk::ResponseType::ACCEPT)) {
                const auto file = dialog->get_file();
                if (file) {
                    import_wallpaper_from_path(file->get_path());
                } else {
                    set_status_caption(tr("cc_status_wallpaper_import_failed"));
                }
            }
            wallpaper_import_dialog_.reset();
        });
        dialog->show();
    }

    void sync_appearance_from_system() {
        selected_theme_ = normalize_standard_theme(config_string({"appearance", "theme"}, selected_theme_.empty() ? std::string{"hyalo"} : selected_theme_));
        selected_icon_pack_ = gtk_setting_string("gtk-icon-theme-name", config_string({"appearance", "icon_pack"}, "Adwaita"));
        selected_cursor_theme_ = gtk_setting_string("gtk-cursor-theme-name", config_string({"appearance", "cursor_theme"}, "Adwaita"));

        const auto current_window_theme = current_labwc_window_theme();
        selected_window_theme_ = current_window_theme.empty() ? config_string({"appearance", "window_theme"}, "HyaloOS") : current_window_theme;

        selected_color_mode_ = normalize_color_mode(config_string({"appearance", "color_mode"}, "auto"));

        const auto [font_family, font_size] = current_system_font();
        config_manager_.set_value({"appearance", "theme"}, normalize_standard_theme(selected_theme_));
        config_manager_.set_value({"appearance", "icon_pack"}, selected_icon_pack_);
        config_manager_.set_value({"appearance", "cursor_theme"}, selected_cursor_theme_);
        config_manager_.set_value({"appearance", "window_theme"}, selected_window_theme_);
        config_manager_.set_value({"appearance", "color_mode"}, selected_color_mode_);
        config_manager_.set_value({"appearance", "font_family"}, font_family);
        config_manager_.set_value({"appearance", "font_size"}, font_size);

        const auto wallpaper = current_system_wallpaper();
        if (!wallpaper.empty()) {
            config_manager_.set_value({"appearance", "wallpaper"}, wallpaper);
        }
    }

    void sync_from_config() {
        appearance_config_ = config_manager_.appearance();
        panel_config_ = config_manager_.panel();
        language_code_ = config_manager_.language();

        const auto appearance_json = config_manager_.raw().value("appearance", nlohmann::json::object());
        selected_theme_ = normalize_standard_theme(appearance_json.value("theme", std::string{"hyalo"}));
        selected_icon_pack_ = appearance_json.value("icon_pack", std::string{"hyalo-icons"});
        selected_cursor_theme_ = appearance_json.value("cursor_theme", std::string{"hyalo-cursor"});
        selected_window_theme_ = appearance_json.value("window_theme", std::string{"hyaloos"});
        selected_color_mode_ = appearance_json.value("color_mode", std::string{"dark"});

        sync_appearance_from_system();
        refresh_appearance_catalogs();
        refresh_language_region_keyboard_options();
        clear_appearance_changes_pending();
        clear_wallpaper_changes_pending();
    }

    void configure_shell() {
        root_.set_margin_start(22);
        root_.set_margin_end(22);
        root_.set_margin_top(22);
        root_.set_margin_bottom(22);
        root_.add_css_class("settings-root");
        root_.set_halign(Gtk::Align::FILL);
        root_.set_valign(Gtk::Align::START);

        sidebar_.set_size_request(320, -1);
        sidebar_.add_css_class("settings-sidebar");

        auto* brand = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        brand->add_css_class("settings-brand-block");
        brand->append(*make_label(tr("cc_brand_title"), "settings-brand-title"));
        brand->append(*make_label(tr("cc_brand_subtitle"), "settings-brand-subtitle", true));

        search_entry_.set_placeholder_text(tr("cc_search_placeholder"));
        search_entry_.add_css_class("settings-search");
        search_entry_.signal_search_changed().connect(sigc::mem_fun(*this, &ControlCenterWindow::on_search_changed));

        nav_list_.add_css_class("settings-nav-list");

        auto* session_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        session_card->add_css_class("settings-status-card");
        status_title_.set_text(tr("cc_session_title"));
        status_title_.add_css_class("settings-status-title");
        status_title_.set_halign(Gtk::Align::START);
        status_title_.set_xalign(0.0f);
        status_value_.add_css_class("settings-status-value");
        status_value_.set_halign(Gtk::Align::START);
        status_value_.set_xalign(0.0f);
        status_caption_.add_css_class("settings-status-caption");
        status_caption_.set_halign(Gtk::Align::START);
        status_caption_.set_xalign(0.0f);
        status_caption_.set_wrap(true);
        session_card->append(status_title_);
        session_card->append(status_value_);
        session_card->append(status_caption_);

        sidebar_.append(*brand);
        sidebar_.append(search_entry_);
        sidebar_.append(nav_list_);
        sidebar_.append(*session_card);

        content_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        content_scroll_.set_child(content_box_);
        content_scroll_.add_css_class("settings-content-scroll");
        content_scroll_.set_hexpand(true);
        content_scroll_.set_vexpand(true);

        content_box_.set_margin_start(8);
        content_box_.set_margin_end(8);
        content_box_.set_margin_top(8);
        content_box_.set_margin_bottom(8);
        content_box_.add_css_class("settings-content-box");

        header_.add_css_class("settings-hero");
        header_text_.set_hexpand(true);
        section_badge_.set_text(tr("cc_badge"));
        section_badge_.add_css_class("settings-hero-badge");
        section_badge_.set_halign(Gtk::Align::START);
        section_title_.add_css_class("settings-hero-title");
        section_title_.set_halign(Gtk::Align::START);
        section_title_.set_xalign(0.0f);
        section_subtitle_.add_css_class("settings-hero-subtitle");
        section_subtitle_.set_halign(Gtk::Align::START);
        section_subtitle_.set_xalign(0.0f);
        section_subtitle_.set_wrap(true);

        header_text_.append(section_badge_);
        header_text_.append(section_title_);
        header_text_.append(section_subtitle_);

        header_.append(header_text_);

        content_stack_.add_css_class("settings-stack");
        content_stack_.set_hexpand(true);
        content_stack_.set_vexpand(true);

        content_box_.append(header_);
        content_box_.append(content_stack_);

        root_.append(sidebar_);
        root_.append(content_scroll_);

        window_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        window_scroll_.set_child(root_);
        window_scroll_.add_css_class("settings-window-scroll");
        set_child(window_scroll_);
    }

    void configure_window_size() {
        auto* display = gdk_display_get_default();
        if (auto* monitor = select_primary_monitor(display)) {
            GdkRectangle geometry{};
            gdk_monitor_get_geometry(monitor, &geometry);

            const auto width = std::min(1600, std::max(720, geometry.width - 56));
            const auto height = std::min(1100, std::max(620, geometry.height - 56));
            set_default_size(width, height);

            g_object_unref(monitor);
            return;
        }

        set_default_size(1480, 920);
    }

    void populate_pages() {
        register_page("appearance", tr("cc_page_appearance_title"), tr("cc_page_appearance_nav_subtitle"), "motywy themes ciemny jasny akcent tapeta wallpaper blur fonts czcionki personalization", *build_appearance_page());
        register_page("display", tr("cc_page_display_title"), tr("cc_page_display_nav_subtitle"), "display ekran monitory dpi scaling refresh vrr hdr night light icc grafika", *build_display_page());
        register_page("input", tr("cc_page_input_title"), tr("cc_page_input_nav_subtitle"), "input mysz mouse touchpad keyboard klawiatura tablet gamepad controller", *build_input_page());
        register_page("workspace", tr("cc_page_workspace_title"), tr("cc_page_workspace_nav_subtitle"), "workspace windows pulpit okna panel taskbar notifications powiadomienia", *build_workspace_page());
        register_page("connectivity", tr("cc_page_connectivity_title"), tr("cc_page_connectivity_nav_subtitle"), "wifi ethernet bluetooth vpn proxy connectivity siec lacznosc", *build_connectivity_page());
        register_page("system", tr("cc_page_system_title"), tr("cc_page_system_nav_subtitle"), "system hardware audio sound power storage disk informacje sprzet", *build_system_page());
        register_page("privacy", tr("cc_page_privacy_title"), tr("cc_page_privacy_nav_subtitle"), "privacy security accounts kamera mikrofon firewall lock screen prywatnosc bezpieczenstwo", *build_privacy_page());
        register_page("users", tr("cc_page_users_title"), tr("cc_page_users_nav_subtitle"), "users aplikacje defaults autostart region language jezyk user apps", *build_users_page());
    }

    void register_page(
        const std::string& page_name,
        const std::string& title,
        const std::string& subtitle,
        const std::string& keywords,
        Gtk::Widget& page
    ) {
        auto* nav_button = create_nav_button(title, subtitle, page_name);
        nav_list_.append(*nav_button);
        content_stack_.add(page, page_name);
        nav_items_.push_back(NavItem{page_name, title, subtitle, keywords, nav_button});
    }

    Gtk::Button* create_nav_button(
        const std::string& title,
        const std::string& subtitle,
        const std::string& page_name
    ) {
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("settings-nav-button");

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
        content->set_halign(Gtk::Align::CENTER);
        content->set_valign(Gtk::Align::CENTER);
        content->append(*make_label(title, "settings-nav-title", false, Gtk::Align::CENTER));
        content->append(*make_label(subtitle, "settings-nav-subtitle", true, Gtk::Align::CENTER));
        button->set_child(*content);
        button->signal_clicked().connect([this, page_name]() { select_page(page_name); });
        return button;
    }

    void select_page(const std::string& page_name) {
        content_stack_.set_visible_child(page_name);

        for (const auto& item : nav_items_) {
            if (item.button == nullptr) {
                continue;
            }

            if (item.page_name == page_name) {
                item.button->add_css_class("active-nav");
                section_title_.set_text(item.title);
                section_subtitle_.set_text(item.subtitle);
            } else {
                item.button->remove_css_class("active-nav");
            }
        }
    }

    void on_search_changed() {
        const auto query = lowercase(search_entry_.get_text());
        if (query.empty()) {
            return;
        }

        for (const auto& item : nav_items_) {
            const auto haystack = lowercase(item.title + " " + item.subtitle + " " + item.keywords);
            if (haystack.find(query) != std::string::npos) {
                select_page(item.page_name);
                return;
            }
        }
    }

    Gtk::Box* create_page_shell(const std::string& kicker_key, const std::string& summary_key) {
        auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 18);
        page->add_css_class("settings-page");

        auto* intro = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        intro->add_css_class("settings-page-intro");
        intro->append(*make_label(tr(kicker_key), "settings-page-kicker"));
        intro->append(*make_label(tr(summary_key), "settings-page-summary", true));
        page->append(*intro);
        return page;
    }

    Gtk::Box* create_card(const std::string& title_key, const std::string& subtitle_key, const std::vector<Gtk::Widget*>& rows) {
        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        card->add_css_class("settings-card");

        auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        header->add_css_class("settings-card-header");
        header->append(*make_label(tr(title_key), "settings-card-title"));
        header->append(*make_label(tr(subtitle_key), "settings-card-subtitle", true));
        card->append(*header);

        for (std::size_t index = 0; index < rows.size(); ++index) {
            card->append(*rows[index]);
            if (index + 1 < rows.size()) {
                auto* divider = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
                divider->add_css_class("settings-divider");
                card->append(*divider);
            }
        }

        return card;
    }

    Gtk::Box* create_row(const Glib::ustring& title, const Glib::ustring& subtitle, Gtk::Widget& control) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        row->add_css_class("settings-row");

        auto* text_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
        text_box->set_hexpand(true);
        text_box->append(*make_label(title, "settings-row-title"));
        text_box->append(*make_label(subtitle, "settings-row-subtitle", true));

        control.set_valign(Gtk::Align::CENTER);
        row->append(*text_box);
        row->append(control);
        return row;
    }

    Gtk::Box* create_info_row(const Glib::ustring& title, const Glib::ustring& value) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        row->add_css_class("settings-info-row");

        auto* title_label = make_label(title, "settings-info-title");
        title_label->set_hexpand(true);
        auto* value_label = make_label(value, "settings-info-value");
        value_label->set_selectable(true);

        row->append(*title_label);
        row->append(*value_label);
        return row;
    }

    Gtk::Widget* create_action_row(
        const std::string& title_key,
        const std::string& subtitle_key,
        const std::string& button_key,
        bool accent = false,
        std::function<void()> action = {}
    ) {
        auto* button = Gtk::make_managed<Gtk::Button>(tr(button_key));
        button->add_css_class("hyalo-button");
        if (accent) {
            button->add_css_class("hyalo-button-primary");
            button->add_css_class("accent");
        } else {
            button->add_css_class("hyalo-button-secondary");
            button->add_css_class("settings-secondary-action");
        }

        if (action) {
            button->signal_clicked().connect([action = std::move(action)]() { action(); });
        }

        return create_row(tr(title_key), tr(subtitle_key), *button);
    }

    bool is_appearance_path(const std::vector<std::string>& path) const {
        return !path.empty() && path.front() == "appearance";
    }

    Gtk::Widget* create_keyboard_preview_widget() {
        keyboard_preview_ = Gtk::make_managed<Gtk::DrawingArea>();
        keyboard_preview_->set_content_width(460);
        keyboard_preview_->set_content_height(160);
        keyboard_preview_->set_margin_start(12);
        keyboard_preview_->set_margin_end(12);
        keyboard_preview_->set_margin_top(4);
        keyboard_preview_->set_margin_bottom(4);
        keyboard_preview_->set_draw_func(
            [this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
                draw_keyboard_preview(cr, w, h);
            }
        );
        return keyboard_preview_;
    }

    bool is_display_per_output_setting_path(const std::vector<std::string>& path) const {
        if (path.size() < 2 || path.front() != "display") {
            return false;
        }

        const auto& key = path[1];
        return key == "resolution"
            || key == "refresh_rate"
            || key == "scaling"
            || key == "orientation"
            || key == "vrr";
    }

    std::string active_display_output_name() const {
        if (!primary_output_name_.empty()) {
            return primary_output_name_;
        }
        if (!display_outputs_preview_.empty()) {
            return display_outputs_preview_.front().name;
        }
        return {};
    }

    static std::string normalize_output_identifier(std::string value) {
        value = lowercase(trim(value));
        if (value.find("output ") == 0) {
            value = trim(value.substr(7));
        }
        if (!value.empty() && value.back() == ':') {
            value.pop_back();
        }
        return trim(value);
    }

    static int parse_display_alias_index(const std::string& value) {
        std::smatch match;
        static const auto alias_pattern = std::regex(R"(^display-([0-9]+)$)", std::regex::icase);
        const auto normalized = normalize_output_identifier(value);
        if (!std::regex_match(normalized, match, alias_pattern) || match.size() < 2) {
            return -1;
        }

        const auto parsed_index = std::stoi(match[1].str()) - 1;
        return parsed_index >= 0 ? parsed_index : -1;
    }

    std::string resolve_output_in_list(const std::string& output_name, const std::vector<std::string>& connected, std::size_t fallback_index = static_cast<std::size_t>(-1)) const {
        if (connected.empty()) {
            return output_name;
        }

        const auto requested = normalize_output_identifier(output_name);
        for (const auto& runtime_name : connected) {
            if (normalize_output_identifier(runtime_name) == requested) {
                return runtime_name;
            }
        }

        const auto alias_index = parse_display_alias_index(output_name);
        if (alias_index >= 0 && static_cast<std::size_t>(alias_index) < connected.size()) {
            return connected[static_cast<std::size_t>(alias_index)];
        }

        if (fallback_index != static_cast<std::size_t>(-1)
            && fallback_index < connected.size()) {
            return connected[fallback_index];
        }

        return output_name;
    }

    std::string resolve_runtime_output_name(const std::string& output_name, std::size_t fallback_index = static_cast<std::size_t>(-1)) const {
        return resolve_output_in_list(output_name, detect_connected_outputs(), fallback_index);
    }

    bool display_auto_apply_enabled() const {
        return config_bool({"display", "auto_apply"}, true);
    }

    template <typename T>
    void mirror_display_setting_to_active_output(const std::vector<std::string>& path, T&& value) {
        if (!is_display_per_output_setting_path(path)) {
            return;
        }

        const auto output_name = active_display_output_name();
        if (output_name.empty()) {
            return;
        }

        config_manager_.set_value({"display", "outputs", output_name, path[1]}, std::forward<T>(value));
    }

    bool should_preview_appearance_change(const std::vector<std::string>& path) const {
        if (path.size() < 2 || !is_appearance_path(path)) {
            return false;
        }

        const auto& key = path[1];
        return key == "theme"
            || key == "color_mode"
            || key == "accent_color"
            || key == "border_radius"
            || key == "transparency"
            || key == "enable_blur";
    }

    void mark_appearance_changes_pending() {
        appearance_changes_pending_ = true;
        set_status_caption(tr("cc_status_apply_pending"));
    }

    void clear_appearance_changes_pending() {
        appearance_changes_pending_ = false;
    }

    void mark_wallpaper_changes_pending() {
        wallpaper_changes_pending_ = true;
        set_status_caption(tr("cc_status_apply_pending"));
    }

    void clear_wallpaper_changes_pending() {
        wallpaper_changes_pending_ = false;
    }

    std::vector<std::string> detect_connected_outputs() const {
        std::vector<std::string> outputs;

        const auto json_output = read_command_output("sh -lc 'command -v wlr-randr >/dev/null 2>&1 && wlr-randr --json 2>/dev/null'");
        if (!json_output.empty()) {
            const auto parsed = nlohmann::json::parse(json_output, nullptr, false);
            auto seen = std::set<std::string>{};

            const auto parse_enabled_flag = [&](const nlohmann::json& output) {
                auto enabled = true;
                if (output.contains("enabled")) {
                    const auto& value = output["enabled"];
                    if (value.is_boolean()) {
                        enabled = value.get<bool>();
                    } else if (value.is_string()) {
                        const auto lowered = lowercase(value.get<std::string>());
                        enabled = lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
                    }
                } else if (output.contains("active") && output["active"].is_boolean()) {
                    enabled = output["active"].get<bool>();
                }
                return enabled;
            };

            const auto maybe_add_output = [&](const nlohmann::json& output, const std::string& fallback_name = std::string{}) {
                if (!output.is_object()) {
                    return;
                }

                auto name = output.value("name", std::string{});
                if (name.empty()) {
                    name = fallback_name;
                }
                if (name.empty()) {
                    return;
                }

                if (parse_enabled_flag(output) && seen.insert(name).second) {
                    outputs.push_back(name);
                }
            };

            if (parsed.is_array()) {
                for (const auto& output : parsed) {
                    maybe_add_output(output);
                }
            } else if (parsed.is_object()) {
                if (parsed.contains("outputs") && parsed["outputs"].is_array()) {
                    for (const auto& output : parsed["outputs"]) {
                        maybe_add_output(output);
                    }
                } else if (parsed.contains("outputs") && parsed["outputs"].is_object()) {
                    for (const auto& [name, output] : parsed["outputs"].items()) {
                        maybe_add_output(output, name);
                    }
                } else {
                    for (const auto& [name, output] : parsed.items()) {
                        maybe_add_output(output, name);
                    }
                }
            }
        }

        if (!outputs.empty()) {
            return outputs;
        }

        const auto text_output = read_command_output("sh -lc 'command -v wlr-randr >/dev/null 2>&1 && wlr-randr 2>/dev/null'");
        std::istringstream stream(text_output);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty() || std::isspace(static_cast<unsigned char>(line.front()))) {
                continue;
            }

            auto normalized_line = trim(line);
            if (normalized_line.empty()) {
                continue;
            }

            if (lowercase(normalized_line).find("output ") == 0) {
                normalized_line = trim(normalized_line.substr(7));
            }

            const auto first_space = normalized_line.find(' ');
            auto output_name = trim(normalized_line.substr(0, first_space));
            if (!output_name.empty() && output_name.back() == ':') {
                output_name.pop_back();
            }
            if (!output_name.empty()) {
                outputs.push_back(output_name);
            }
        }

        if (outputs.empty()) {
            if (auto* display = gdk_display_get_default()) {
                if (auto* monitors = gdk_display_get_monitors(display)) {
                    const auto monitor_count = g_list_model_get_n_items(G_LIST_MODEL(monitors));
                    for (guint index = 0; index < monitor_count; ++index) {
                        auto* monitor = GDK_MONITOR(g_list_model_get_item(G_LIST_MODEL(monitors), index));
                        if (!monitor) {
                            continue;
                        }

                        std::string name;
                        if (const auto* connector = gdk_monitor_get_connector(monitor); connector && connector[0] != '\0') {
                            name = connector;
                        } else {
                            name = "Display-" + std::to_string(static_cast<int>(index) + 1);
                        }
                        outputs.push_back(name);
                        g_object_unref(monitor);
                    }
                }
            }
        }

        return outputs;
    }

    std::string output_wallpaper_value(const std::string& output_name) const {
        const auto fallback = config_string({"appearance", "wallpaper"}, current_system_wallpaper());
        if (output_name.empty()) {
            return fallback;
        }

        return config_string({"appearance", "wallpaper_per_output", output_name}, fallback);
    }

    std::vector<DisplayOutputPreview> detect_display_outputs_preview() {
        std::vector<DisplayOutputPreview> outputs;
        const auto saved_primary = config_string({"display", "primary_output"}, "");
        const auto resolution_regex = std::regex(R"((\d+)x(\d+))", std::regex::icase);
        const auto mode_with_refresh_regex = std::regex(R"((\d+)\s*x\s*(\d+)(?:[^0-9]+([0-9]+(?:\.[0-9]+)?))?)", std::regex::icase);
        const auto normalize_refresh_hz = [](double hz) {
            if (hz > 1000.0) {
                hz /= 1000.0;
            }
            if (hz <= 1.0) {
                return std::string{};
            }

            auto value = format_decimal(hz, 2);
            while (!value.empty() && value.back() == '0') {
                value.pop_back();
            }
            if (!value.empty() && value.back() == '.') {
                value.pop_back();
            }
            return value;
        };

        const auto json_output = read_command_output("sh -lc 'command -v wlr-randr >/dev/null 2>&1 && wlr-randr --json 2>/dev/null'");
        if (!json_output.empty()) {
            const auto parsed = nlohmann::json::parse(json_output, nullptr, false);
            auto seen = std::set<std::string>{};

            const auto parse_enabled_flag = [&](const nlohmann::json& output) {
                auto enabled = true;
                if (output.contains("enabled")) {
                    const auto& value = output["enabled"];
                    if (value.is_boolean()) {
                        enabled = value.get<bool>();
                    } else if (value.is_string()) {
                        const auto lowered = lowercase(value.get<std::string>());
                        enabled = lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
                    }
                } else if (output.contains("active") && output["active"].is_boolean()) {
                    enabled = output["active"].get<bool>();
                }
                return enabled;
            };

            const auto extract_dimension = [](const nlohmann::json& object, const char* primary_key, const char* secondary_key, int fallback_value) {
                auto value = fallback_value;
                if (object.contains(primary_key) && object[primary_key].is_number_integer()) {
                    value = object[primary_key].get<int>();
                } else if (object.contains(secondary_key) && object[secondary_key].is_number_integer()) {
                    value = object[secondary_key].get<int>();
                }
                return value;
            };

            const auto extract_scale = [](const nlohmann::json& output) {
                if (output.contains("scale") && output["scale"].is_number()) {
                    return std::max(0.5, output["scale"].get<double>());
                }
                if (output.contains("transform_scale") && output["transform_scale"].is_number()) {
                    return std::max(0.5, output["transform_scale"].get<double>());
                }
                return 1.0;
            };

            const auto extract_refresh = [&](const nlohmann::json& mode) {
                if (!mode.is_object()) {
                    return std::string{};
                }

                const auto parse_refresh_field = [&](const nlohmann::json& field) {
                    if (field.is_number()) {
                        return normalize_refresh_hz(field.get<double>());
                    }
                    if (field.is_string()) {
                        const auto text = trim(field.get<std::string>());
                        if (text.empty()) {
                            return std::string{};
                        }

                        std::smatch match;
                        static const auto numeric_pattern = std::regex(R"(([0-9]+(?:\.[0-9]+)?))", std::regex::icase);
                        if (std::regex_search(text, match, numeric_pattern) && match.size() >= 2) {
                            return normalize_refresh_hz(std::stod(match[1].str()));
                        }
                    }
                    return std::string{};
                };

                static const auto refresh_keys = std::array<const char*, 8>{
                    "refresh",
                    "refresh_rate",
                    "hz",
                    "refresh_hz",
                    "refreshHz",
                    "refresh_mhz",
                    "refresh_milli_hz",
                    "mHz"
                };

                for (const auto* key : refresh_keys) {
                    if (!mode.contains(key)) {
                        continue;
                    }
                    const auto parsed = parse_refresh_field(mode[key]);
                    if (!parsed.empty()) {
                        return parsed;
                    }
                }
                return std::string{};
            };

            const auto maybe_add_preview = [&](const nlohmann::json& output, const std::string& fallback_name = std::string{}) {
                if (!output.is_object() || !parse_enabled_flag(output)) {
                    return;
                }

                DisplayOutputPreview preview;
                preview.name = output.value("name", std::string{});
                if (preview.name.empty()) {
                    preview.name = fallback_name;
                }
                if (preview.name.empty() || !seen.insert(preview.name).second) {
                    return;
                }

                preview.x = extract_dimension(output, "x", "pos_x", 0);
                preview.y = extract_dimension(output, "y", "pos_y", 0);
                preview.scale = extract_scale(output);

                if (output.contains("position") && output["position"].is_object()) {
                    const auto& position = output["position"];
                    preview.x = extract_dimension(position, "x", "pos_x", preview.x);
                    preview.y = extract_dimension(position, "y", "pos_y", preview.y);
                }

                if (output.contains("current_mode") && output["current_mode"].is_object()) {
                    const auto& mode = output["current_mode"];
                    preview.width = std::max(320, extract_dimension(mode, "width", "w", 1920));
                    preview.height = std::max(200, extract_dimension(mode, "height", "h", 1080));
                    preview.refresh_rate = extract_refresh(mode);
                } else if (output.contains("current_mode") && output["current_mode"].is_string()) {
                    std::smatch mode_match;
                    const auto mode_text = output["current_mode"].get<std::string>();
                    if (std::regex_search(mode_text, mode_match, mode_with_refresh_regex) && mode_match.size() >= 3) {
                        preview.width = std::max(320, std::stoi(mode_match[1].str()));
                        preview.height = std::max(200, std::stoi(mode_match[2].str()));
                        if (mode_match.size() >= 4 && mode_match[3].matched) {
                            preview.refresh_rate = normalize_refresh_hz(std::stod(mode_match[3].str()));
                        }
                    }
                } else if (output.contains("currentMode") && output["currentMode"].is_object()) {
                    const auto& mode = output["currentMode"];
                    preview.width = std::max(320, extract_dimension(mode, "width", "w", 1920));
                    preview.height = std::max(200, extract_dimension(mode, "height", "h", 1080));
                    preview.refresh_rate = extract_refresh(mode);
                } else if (output.contains("currentMode") && output["currentMode"].is_string()) {
                    std::smatch mode_match;
                    const auto mode_text = output["currentMode"].get<std::string>();
                    if (std::regex_search(mode_text, mode_match, mode_with_refresh_regex) && mode_match.size() >= 3) {
                        preview.width = std::max(320, std::stoi(mode_match[1].str()));
                        preview.height = std::max(200, std::stoi(mode_match[2].str()));
                        if (mode_match.size() >= 4 && mode_match[3].matched) {
                            preview.refresh_rate = normalize_refresh_hz(std::stod(mode_match[3].str()));
                        }
                    }
                } else if (output.contains("mode") && output["mode"].is_object()) {
                    const auto& mode = output["mode"];
                    preview.width = std::max(320, extract_dimension(mode, "width", "w", 1920));
                    preview.height = std::max(200, extract_dimension(mode, "height", "h", 1080));
                    preview.refresh_rate = extract_refresh(mode);
                } else if (output.contains("mode") && output["mode"].is_string()) {
                    std::smatch mode_match;
                    const auto mode_text = output["mode"].get<std::string>();
                    if (std::regex_search(mode_text, mode_match, mode_with_refresh_regex) && mode_match.size() >= 3) {
                        preview.width = std::max(320, std::stoi(mode_match[1].str()));
                        preview.height = std::max(200, std::stoi(mode_match[2].str()));
                        if (mode_match.size() >= 4 && mode_match[3].matched) {
                            preview.refresh_rate = normalize_refresh_hz(std::stod(mode_match[3].str()));
                        }
                    }
                } else if (output.contains("modes") && output["modes"].is_array()) {
                    for (const auto& mode : output["modes"]) {
                        if (!mode.is_object() || !mode.value("current", false)) {
                            continue;
                        }

                        preview.width = std::max(320, extract_dimension(mode, "width", "w", 1920));
                        preview.height = std::max(200, extract_dimension(mode, "height", "h", 1080));
                        preview.refresh_rate = extract_refresh(mode);
                        break;
                    }
                }

                if (output.contains("width") && output["width"].is_number_integer()) {
                    preview.width = std::max(320, output["width"].get<int>());
                }
                if (output.contains("height") && output["height"].is_number_integer()) {
                    preview.height = std::max(200, output["height"].get<int>());
                }

                int mm_width = 0;
                int mm_height = 0;
                if (output.contains("physical_size") && output["physical_size"].is_object()) {
                    const auto& physical = output["physical_size"];
                    mm_width = extract_dimension(physical, "width", "w", 0);
                    mm_height = extract_dimension(physical, "height", "h", 0);
                } else {
                    mm_width = output.value("phys_width", output.value("physical_width", 0));
                    mm_height = output.value("phys_height", output.value("physical_height", 0));
                }

                if (mm_width > 0) {
                    preview.dpi = std::max(0, static_cast<int>(std::lround((preview.width * 25.4) / static_cast<double>(mm_width))));
                }

                outputs.push_back(preview);
            };

            if (parsed.is_array()) {
                for (const auto& output : parsed) {
                    maybe_add_preview(output);
                }
            } else if (parsed.is_object()) {
                if (parsed.contains("outputs") && parsed["outputs"].is_array()) {
                    for (const auto& output : parsed["outputs"]) {
                        maybe_add_preview(output);
                    }
                } else if (parsed.contains("outputs") && parsed["outputs"].is_object()) {
                    for (const auto& [name, output] : parsed["outputs"].items()) {
                        maybe_add_preview(output, name);
                    }
                } else {
                    for (const auto& [name, output] : parsed.items()) {
                        maybe_add_preview(output, name);
                    }
                }
            }
        }

        if (outputs.empty()) {
            const auto text_output = read_command_output("sh -lc 'command -v wlr-randr >/dev/null 2>&1 && wlr-randr 2>/dev/null'");
            if (!text_output.empty()) {
                std::istringstream stream(text_output);
                std::string line;
                DisplayOutputPreview current;
                bool has_current = false;
                bool current_enabled = true;

                const std::regex resolution_pattern(R"((\d+)x(\d+))");
                const std::regex mode_pattern(R"((\d+)\s*x\s*(\d+)(?:[^0-9]+([0-9]+(?:\.[0-9]+)?))?)", std::regex::icase);
                const std::regex position_pattern(R"((?:Position:|pos(?:ition)?\s*=?)\s*(-?\d+)\s*,\s*(-?\d+))", std::regex::icase);
                const std::regex scale_pattern(R"((?:Scale:|scale\s*=?)\s*([0-9]+(?:\.[0-9]+)?))", std::regex::icase);

                auto flush_current = [&]() {
                    if (has_current && current_enabled && !current.name.empty()) {
                        outputs.push_back(current);
                    }
                    current = DisplayOutputPreview{};
                    has_current = false;
                    current_enabled = true;
                };

                while (std::getline(stream, line)) {
                    if (line.empty()) {
                        continue;
                    }

                    const bool is_header = !std::isspace(static_cast<unsigned char>(line.front()));
                    if (is_header) {
                        flush_current();

                        auto normalized_line = trim(line);
                        if (lowercase(normalized_line).find("output ") == 0) {
                            normalized_line = trim(normalized_line.substr(7));
                        }

                        const auto first_space = normalized_line.find(' ');
                        current.name = trim(normalized_line.substr(0, first_space));
                        if (!current.name.empty() && current.name.back() == ':') {
                            current.name.pop_back();
                        }
                        has_current = !current.name.empty();
                        current_enabled = true;

                        const auto lowered_header = lowercase(normalized_line);
                        if (lowered_header.find("disconnected") != std::string::npos
                            || lowered_header.find("disabled") != std::string::npos
                            || lowered_header.find("off") != std::string::npos) {
                            current_enabled = false;
                        }
                        continue;
                    }

                    if (!has_current) {
                        continue;
                    }

                    const auto lowered_line = lowercase(trim(line));
                    if (lowered_line.find("enabled:") == 0) {
                        current_enabled = lowered_line.find("yes") != std::string::npos
                            || lowered_line.find("true") != std::string::npos
                            || lowered_line.find("on") != std::string::npos;
                        continue;
                    }

                    std::smatch match;
                    if (std::regex_search(line, match, position_pattern) && match.size() >= 3) {
                        current.x = std::stoi(match[1].str());
                        current.y = std::stoi(match[2].str());
                        continue;
                    }

                    if (std::regex_search(line, match, scale_pattern) && match.size() >= 2) {
                        current.scale = std::max(0.5, std::stod(match[1].str()));
                        continue;
                    }

                    if (line.find("current") != std::string::npos && std::regex_search(line, match, mode_pattern) && match.size() >= 3) {
                        current.width = std::max(320, std::stoi(match[1].str()));
                        current.height = std::max(200, std::stoi(match[2].str()));
                        if (match.size() >= 4 && match[3].matched) {
                            current.refresh_rate = normalize_refresh_hz(std::stod(match[3].str()));
                        }
                        continue;
                    }

                    if ((current.width <= 0 || current.height <= 0) && std::regex_search(line, match, resolution_pattern) && match.size() >= 3) {
                        current.width = std::max(320, std::stoi(match[1].str()));
                        current.height = std::max(200, std::stoi(match[2].str()));
                    }
                }

                flush_current();
            }
        }

        if (outputs.empty()) {
            if (auto* display = gdk_display_get_default()) {
                if (auto* monitors = gdk_display_get_monitors(display)) {
                    const auto monitor_count = g_list_model_get_n_items(G_LIST_MODEL(monitors));
                    for (guint index = 0; index < monitor_count; ++index) {
                        auto* monitor = GDK_MONITOR(g_list_model_get_item(G_LIST_MODEL(monitors), index));
                        if (!monitor) {
                            continue;
                        }

                        GdkRectangle geometry{};
                        gdk_monitor_get_geometry(monitor, &geometry);

                        DisplayOutputPreview preview;
                        if (const auto* connector = gdk_monitor_get_connector(monitor); connector && connector[0] != '\0') {
                            preview.name = connector;
                        } else {
                            preview.name = "Display-" + std::to_string(static_cast<int>(index) + 1);
                        }

                        preview.x = geometry.x;
                        preview.y = geometry.y;
                        preview.width = std::max(320, geometry.width);
                        preview.height = std::max(200, geometry.height);
                        preview.scale = std::max(0.5, static_cast<double>(gdk_monitor_get_scale_factor(monitor)));
                        preview.refresh_rate = normalize_refresh_hz(static_cast<double>(gdk_monitor_get_refresh_rate(monitor)));

                        const auto mm_width = gdk_monitor_get_width_mm(monitor);
                        if (mm_width > 0) {
                            preview.dpi = std::max(0, static_cast<int>(std::lround((preview.width * 25.4) / static_cast<double>(mm_width))));
                        }

                        outputs.push_back(preview);
                        g_object_unref(monitor);
                    }
                }
            }
        }

        if (!outputs.empty()) {
            if (auto* display = gdk_display_get_default()) {
                if (auto* monitors = gdk_display_get_monitors(display)) {
                    auto monitor_by_name = std::map<std::string, DisplayOutputPreview>{};
                    std::vector<DisplayOutputPreview> monitor_by_index;

                    const auto monitor_count = g_list_model_get_n_items(G_LIST_MODEL(monitors));
                    monitor_by_index.reserve(monitor_count);

                    for (guint index = 0; index < monitor_count; ++index) {
                        auto* monitor = GDK_MONITOR(g_list_model_get_item(G_LIST_MODEL(monitors), index));
                        if (!monitor) {
                            continue;
                        }

                        GdkRectangle geometry{};
                        gdk_monitor_get_geometry(monitor, &geometry);

                        DisplayOutputPreview preview;
                        if (const auto* connector = gdk_monitor_get_connector(monitor); connector && connector[0] != '\0') {
                            preview.name = connector;
                        }
                        preview.x = geometry.x;
                        preview.y = geometry.y;
                        preview.width = std::max(320, geometry.width);
                        preview.height = std::max(200, geometry.height);
                        preview.scale = std::max(0.5, static_cast<double>(gdk_monitor_get_scale_factor(monitor)));
                        preview.refresh_rate = normalize_refresh_hz(static_cast<double>(gdk_monitor_get_refresh_rate(monitor)));

                        const auto mm_width = gdk_monitor_get_width_mm(monitor);
                        if (mm_width > 0) {
                            preview.dpi = std::max(0, static_cast<int>(std::lround((preview.width * 25.4) / static_cast<double>(mm_width))));
                        }

                        if (!preview.name.empty()) {
                            monitor_by_name[preview.name] = preview;
                        }
                        monitor_by_index.push_back(preview);
                        g_object_unref(monitor);
                    }

                    for (std::size_t index = 0; index < outputs.size(); ++index) {
                        auto& output = outputs[index];

                        auto matched = monitor_by_name.find(output.name);
                        if (matched == monitor_by_name.end() && index < monitor_by_index.size()) {
                            matched = monitor_by_name.insert({output.name, monitor_by_index[index]}).first;
                        }
                        if (matched == monitor_by_name.end()) {
                            continue;
                        }

                        if (output.width <= 0) {
                            output.width = matched->second.width;
                        }
                        if (output.height <= 0) {
                            output.height = matched->second.height;
                        }
                        if (output.scale <= 0.0) {
                            output.scale = matched->second.scale;
                        }
                        if (output.dpi <= 0) {
                            output.dpi = matched->second.dpi;
                        }
                        if (output.refresh_rate.empty()) {
                            output.refresh_rate = matched->second.refresh_rate;
                        }
                    }
                }
            }
        }

        if (outputs.empty()) {
            DisplayOutputPreview fallback;
            fallback.name = "Display-1";
            fallback.width = 1920;
            fallback.height = 1080;
            fallback.x = 0;
            fallback.y = 0;
            fallback.scale = 1.0;
            outputs.push_back(fallback);
        }

        primary_output_name_.clear();
        if (!saved_primary.empty()) {
            for (const auto& output : outputs) {
                if (output.name == saved_primary) {
                    primary_output_name_ = saved_primary;
                    break;
                }
            }
        }
        if (primary_output_name_.empty() && !outputs.empty()) {
            primary_output_name_ = outputs.front().name;
        }

        return outputs;
    }

    static int scale_dimension(int dimension, double scale, int minimum) {
        return std::max(minimum, static_cast<int>(std::lround(static_cast<double>(dimension) * scale)));
    }

    static void maybe_snap(int current, int target, int threshold, int& value) {
        if (std::abs(current - target) <= threshold) {
            value += target - current;
        }
    }

    void rebuild_display_layout_preview() {
        if (display_layout_canvas_ == nullptr) {
            return;
        }

        while (auto* child = display_layout_canvas_->get_first_child()) {
            display_layout_canvas_->remove(*child);
        }
        display_layout_widgets_.clear();

        if (display_outputs_preview_.empty()) {
            return;
        }

        int min_x = display_outputs_preview_.front().x;
        int min_y = display_outputs_preview_.front().y;
        int max_x = display_outputs_preview_.front().x + display_outputs_preview_.front().width;
        int max_y = display_outputs_preview_.front().y + display_outputs_preview_.front().height;
        for (const auto& output : display_outputs_preview_) {
            min_x = std::min(min_x, output.x);
            min_y = std::min(min_y, output.y);
            max_x = std::max(max_x, output.x + output.width);
            max_y = std::max(max_y, output.y + output.height);
        }

        constexpr int canvas_width = 430;
        constexpr int canvas_height = 210;
        constexpr int canvas_margin = 10;

        const auto logical_width = std::max(1, max_x - min_x);
        const auto logical_height = std::max(1, max_y - min_y);
        const auto available_width = static_cast<double>(canvas_width - (canvas_margin * 2));
        const auto available_height = static_cast<double>(canvas_height - (canvas_margin * 2));

        display_preview_scale_ = std::max(0.08, std::min(available_width / static_cast<double>(logical_width), available_height / static_cast<double>(logical_height)));
        display_preview_min_x_ = min_x;
        display_preview_min_y_ = min_y;

        for (auto& output : display_outputs_preview_) {
            const auto logical_width = std::max(320.0, static_cast<double>(output.width) / std::max(0.5, output.scale));
            const auto logical_height = std::max(200.0, static_cast<double>(output.height) / std::max(0.5, output.scale));
            const auto width = scale_dimension(static_cast<int>(std::lround(logical_width)), display_preview_scale_, 78);
            const auto height = scale_dimension(static_cast<int>(std::lround(logical_height)), display_preview_scale_, 50);
            const auto px = canvas_margin + static_cast<int>(std::lround((output.x - min_x) * display_preview_scale_));
            const auto py = canvas_margin + static_cast<int>(std::lround((output.y - min_y) * display_preview_scale_));

            std::ostringstream label;
            label << output.name;
            if (output.name == primary_output_name_) {
                label << "  *";
            }
            const auto refresh_value = config_string(
                {"display", "outputs", output.name, "refresh_rate"},
                config_string({"display", "refresh_rate"}, "60")
            );
            label << "\n" << output.width << "x" << output.height << " @" << format_decimal(output.scale, 2) << "x";
            const auto normalized_refresh = output.refresh_rate.empty()
                ? normalize_refresh_value(refresh_value, "")
                : normalize_refresh_value(output.refresh_rate, "");
            if (!normalized_refresh.empty()) {
                label << "  " << normalized_refresh << "Hz";
            }
            if (output.dpi > 0) {
                label << "\n" << output.dpi << " DPI";
            }

            auto* monitor = Gtk::make_managed<Gtk::Button>(label.str());
            monitor->add_css_class("display-preview-output");
            if (output.name == primary_output_name_) {
                monitor->add_css_class("display-preview-primary");
            }
            monitor->set_size_request(width, height);
            monitor->set_can_focus(false);
            monitor->set_valign(Gtk::Align::FILL);
            monitor->set_halign(Gtk::Align::FILL);
            monitor->signal_clicked().connect([this, name = output.name]() {
                primary_output_name_ = name;
                config_manager_.set_value({"display", "primary_output"}, primary_output_name_);
                config_manager_.save();
                rebuild_display_layout_preview();
                refresh_display_mode_combos();
                sync_display_controls_for_primary_output();
            });

            const auto gesture = Gtk::GestureDrag::create();
            gesture->signal_drag_begin().connect([this, name = output.name](double, double) {
                display_dragged_output_name_ = name;
                auto widget_it = display_layout_widgets_.find(name);
                if (widget_it != display_layout_widgets_.end() && widget_it->second != nullptr) {
                    display_layout_canvas_->get_child_position(*widget_it->second, display_drag_origin_x_, display_drag_origin_y_);
                }
            });
            gesture->signal_drag_update().connect([this](double offset_x, double offset_y) {
                if (display_dragged_output_name_.empty()) {
                    return;
                }

                auto widget_it = display_layout_widgets_.find(display_dragged_output_name_);
                if (widget_it == display_layout_widgets_.end() || widget_it->second == nullptr) {
                    return;
                }

                auto* widget = widget_it->second;
                const auto width = widget->get_width();
                const auto height = widget->get_height();

                const auto max_x = std::max(10, 430 - width - 10);
                const auto max_y = std::max(10, 210 - height - 10);
                auto new_x = std::clamp(static_cast<int>(std::lround(display_drag_origin_x_ + offset_x)), 10, max_x);
                auto new_y = std::clamp(static_cast<int>(std::lround(display_drag_origin_y_ + offset_y)), 10, max_y);

                constexpr int snap_threshold = 12;
                for (const auto& candidate : display_layout_widgets_) {
                    if (candidate.first == display_dragged_output_name_ || candidate.second == nullptr) {
                        continue;
                    }

                    double ox = 0.0;
                    double oy = 0.0;
                    display_layout_canvas_->get_child_position(*candidate.second, ox, oy);
                    const auto other_x = static_cast<int>(std::lround(ox));
                    const auto other_y = static_cast<int>(std::lround(oy));
                    const auto other_w = candidate.second->get_width();
                    const auto other_h = candidate.second->get_height();

                    maybe_snap(new_x, other_x, snap_threshold, new_x);
                    maybe_snap(new_x, other_x + other_w, snap_threshold, new_x);
                    maybe_snap(new_x + width, other_x, snap_threshold, new_x);
                    maybe_snap(new_x + width, other_x + other_w, snap_threshold, new_x);
                    maybe_snap(new_x + (width / 2), other_x + (other_w / 2), snap_threshold, new_x);

                    maybe_snap(new_y, other_y, snap_threshold, new_y);
                    maybe_snap(new_y, other_y + other_h, snap_threshold, new_y);
                    maybe_snap(new_y + height, other_y, snap_threshold, new_y);
                    maybe_snap(new_y + height, other_y + other_h, snap_threshold, new_y);
                    maybe_snap(new_y + (height / 2), other_y + (other_h / 2), snap_threshold, new_y);
                }

                new_x = std::clamp(new_x, 10, max_x);
                new_y = std::clamp(new_y, 10, max_y);

                display_layout_canvas_->move(*widget, new_x, new_y);

                for (auto& output : display_outputs_preview_) {
                    if (output.name == display_dragged_output_name_) {
                        output.x = display_preview_min_x_ + static_cast<int>(std::lround((new_x - 10) / std::max(0.01, display_preview_scale_)));
                        output.y = display_preview_min_y_ + static_cast<int>(std::lround((new_y - 10) / std::max(0.01, display_preview_scale_)));
                        break;
                    }
                }
            });
            gesture->signal_drag_end().connect([this](double, double) {
                if (!display_dragged_output_name_.empty() && display_auto_apply_enabled()) {
                    apply_display_layout_preview();
                }
                display_dragged_output_name_.clear();
            });
            monitor->add_controller(gesture);

            display_layout_canvas_->put(*monitor, px, py);
            display_layout_widgets_[output.name] = monitor;
        }
    }

    bool apply_display_layout_preview() {
        if (!command_exists("wlr-randr") || display_outputs_preview_.empty()) {
            return false;
        }

        if (primary_output_name_.empty()) {
            primary_output_name_ = display_outputs_preview_.front().name;
        }

        int primary_x = 0;
        int primary_y = 0;
        for (const auto& output : display_outputs_preview_) {
            if (output.name == primary_output_name_) {
                primary_x = output.x;
                primary_y = output.y;
                break;
            }
        }

        bool success = true;
        for (std::size_t index = 0; index < display_outputs_preview_.size(); ++index) {
            const auto& output = display_outputs_preview_[index];
            const auto runtime_output_name = resolve_runtime_output_name(output.name, index);
            const auto normalized_x = output.x - primary_x;
            const auto normalized_y = output.y - primary_y;
            const auto command =
                "wlr-randr --output " + shell_double_quote(runtime_output_name)
                + " --pos " + std::to_string(normalized_x) + "," + std::to_string(normalized_y)
                + " >/dev/null 2>&1";
            const auto wrapped = "sh -lc " + shell_double_quote(command);
            if (std::system(wrapped.c_str()) != 0) {
                success = false;
                break;
            }
        }

        if (success) {
            config_manager_.set_value({"display", "primary_output"}, primary_output_name_);

            for (std::size_t index = 0; index < display_outputs_preview_.size(); ++index) {
                const auto& output = display_outputs_preview_[index];
                const auto runtime_output_name = resolve_runtime_output_name(output.name, index);
                const auto normalized_x = output.x - primary_x;
                const auto normalized_y = output.y - primary_y;

                config_manager_.set_value({"display", "outputs", output.name, "x"}, normalized_x);
                config_manager_.set_value({"display", "outputs", output.name, "y"}, normalized_y);
                if (runtime_output_name != output.name) {
                    config_manager_.set_value({"display", "outputs", runtime_output_name, "x"}, normalized_x);
                    config_manager_.set_value({"display", "outputs", runtime_output_name, "y"}, normalized_y);
                }
            }

            config_manager_.save();
            set_status_caption(tr("cc_status_display_layout_applied"));
            display_outputs_preview_ = detect_display_outputs_preview();
            rebuild_display_layout_preview();
            refresh_display_output_selector();
        }

        return success;
    }

    Gtk::Widget* create_display_layout_preview_row() {
        auto* controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        controls->add_css_class("display-layout-controls");

        auto* canvas = Gtk::make_managed<Gtk::Fixed>();
        canvas->set_size_request(430, 210);
        canvas->add_css_class("display-layout-canvas");
        display_layout_canvas_ = canvas;

        auto* apply_button = Gtk::make_managed<Gtk::Button>(tr("cc_action_apply_layout"));
        apply_button->add_css_class("hyalo-button");
        apply_button->add_css_class("hyalo-button-primary");
        apply_button->add_css_class("accent");
        apply_button->signal_clicked().connect([this]() {
            if (!apply_display_layout_preview()) {
                set_status_caption(tr("cc_status_display_layout_apply_failed"));
            }
        });

        controls->append(*canvas);
        controls->append(*apply_button);

        display_outputs_preview_ = detect_display_outputs_preview();
        rebuild_display_layout_preview();
        refresh_display_output_selector();

        return create_row(tr("cc_row_display_preview_title"), tr("cc_row_display_preview_subtitle"), *controls);
    }

    void apply_display_settings_now() {
        apply_display_runtime_settings();
        display_outputs_preview_ = detect_display_outputs_preview();
        rebuild_display_layout_preview();
        refresh_display_output_selector();
        refresh_display_mode_combos();
        sync_display_controls_for_primary_output();
    }

    Gtk::Widget* create_display_apply_controls_row() {
        auto* controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        controls->set_hexpand(true);

        auto* auto_apply_label = Gtk::make_managed<Gtk::Label>(tr("cc_row_display_auto_apply_label"));
        auto_apply_label->set_xalign(0.0f);
        auto_apply_label->set_hexpand(true);

        auto* auto_apply_switch = Gtk::make_managed<Gtk::Switch>();
        auto_apply_switch->add_css_class("settings-switch");
        auto_apply_switch->set_active(display_auto_apply_enabled());
        auto_apply_switch->signal_state_set().connect([this](bool enabled) {
            if (updating_widgets_) {
                return false;
            }

            config_manager_.set_value({"display", "auto_apply"}, enabled);
            if (config_manager_.save()) {
                set_status_caption(tr("cc_status_saved"));
            } else {
                set_status_caption(tr("cc_status_save_failed"));
            }
            return false;
        }, false);

        auto* apply_now_button = Gtk::make_managed<Gtk::Button>(tr("cc_action_apply"));
        apply_now_button->add_css_class("hyalo-button");
        apply_now_button->add_css_class("hyalo-button-primary");
        apply_now_button->add_css_class("accent");
        apply_now_button->signal_clicked().connect([this]() {
            persist_config("cc_status_saved", [this]() {
                apply_display_settings_now();
            });
        });

        controls->append(*auto_apply_label);
        controls->append(*auto_apply_switch);
        controls->append(*apply_now_button);

        return create_row(tr("cc_row_display_apply_title"), tr("cc_row_display_apply_subtitle"), *controls);
    }

    std::pair<std::vector<std::string>, std::vector<std::string>> build_resolution_options() {
        auto resolution_values = std::vector<std::string>{};

        const auto detected_outputs = detect_display_outputs_preview();
        for (const auto& output : detected_outputs) {
            if (output.width <= 0 || output.height <= 0) {
                continue;
            }

            const auto value = std::to_string(output.width) + "x" + std::to_string(output.height);
            if (std::find(resolution_values.begin(), resolution_values.end(), value) == resolution_values.end()) {
                resolution_values.push_back(value);
            }
        }

        const auto configured_global = config_string({"display", "resolution"}, "1920x1080");
        if (std::find(resolution_values.begin(), resolution_values.end(), configured_global) == resolution_values.end()) {
            resolution_values.push_back(configured_global);
        }

        if (resolution_values.empty()) {
            resolution_values.push_back("1920x1080");
        }

        std::sort(resolution_values.begin(), resolution_values.end(), [](const auto& left, const auto& right) {
            const auto parse_pair = [](const std::string& value) {
                const auto x = value.find('x');
                if (x == std::string::npos) {
                    return std::pair<int, int>{0, 0};
                }
                return std::pair<int, int>{std::stoi(value.substr(0, x)), std::stoi(value.substr(x + 1))};
            };

            const auto [lw, lh] = parse_pair(left);
            const auto [rw, rh] = parse_pair(right);
            const auto left_pixels = static_cast<long long>(lw) * static_cast<long long>(lh);
            const auto right_pixels = static_cast<long long>(rw) * static_cast<long long>(rh);
            if (left_pixels == right_pixels) {
                return left > right;
            }
            return left_pixels > right_pixels;
        });

        auto resolution_labels = std::vector<std::string>{};
        resolution_labels.reserve(resolution_values.size());
        for (const auto& value : resolution_values) {
            const auto x = value.find('x');
            if (x == std::string::npos) {
                resolution_labels.push_back(value);
            } else {
                resolution_labels.push_back(value.substr(0, x) + " x " + value.substr(x + 1));
            }
        }

        return {resolution_labels, resolution_values};
    }
    std::string normalize_refresh_value(double hz) const {
        auto value = format_decimal(hz, 2);
        while (!value.empty() && value.back() == '0') {
            value.pop_back();
        }
        if (!value.empty() && value.back() == '.') {
            value.pop_back();
        }
        return value.empty() ? "60" : value;
    }

    std::string normalize_refresh_value(const std::string& value, const std::string& fallback = "60") const {
        const auto trimmed_value = trim(value);
        if (trimmed_value.empty()) {
            return fallback;
        }

        try {
            auto hz = std::stod(trimmed_value);
            if (hz > 1000.0) {
                hz /= 1000.0;
            }
            if (hz <= 1.0) {
                return fallback;
            }
            return normalize_refresh_value(hz);
        } catch (...) {
            return fallback;
        }
    }

    void update_bound_combo_values(Gtk::ComboBoxText* combo, const std::vector<std::string>& values) {
        for (auto& binding : bound_combos_) {
            if (binding.widget == combo) {
                binding.values = values;
                return;
            }
        }
    }

    const std::vector<std::string>* bound_combo_values(Gtk::ComboBoxText* combo) const {
        for (const auto& binding : bound_combos_) {
            if (binding.widget == combo) {
                return &binding.values;
            }
        }
        return nullptr;
    }

    void refresh_display_output_selector() {
        if (display_output_combo_ == nullptr) {
            return;
        }

        display_output_values_.clear();
        auto labels = std::vector<std::string>{};
        for (const auto& output : display_outputs_preview_) {
            display_output_values_.push_back(output.name);
            labels.push_back(
                output.name
                + " ("
                + std::to_string(output.width)
                + " x "
                + std::to_string(output.height)
                + ")"
            );
        }

        if (display_output_values_.empty()) {
            display_output_values_.push_back("Display-1");
            labels.push_back("Display-1");
        }

        if (primary_output_name_.empty()
            || std::find(display_output_values_.begin(), display_output_values_.end(), primary_output_name_) == display_output_values_.end()) {
            primary_output_name_ = display_output_values_.front();
        }

        updating_widgets_ = true;
        display_output_combo_->remove_all();
        for (const auto& label : labels) {
            display_output_combo_->append(label);
        }
        display_output_combo_->set_active(index_for_value(display_output_values_, primary_output_name_));
        updating_widgets_ = false;
    }

    Gtk::Widget* create_display_output_selector_row() {
        auto* combo = Gtk::make_managed<Gtk::ComboBoxText>();
        combo->add_css_class("settings-combo");
        display_output_combo_ = combo;

        refresh_display_output_selector();

        combo->signal_changed().connect([this]() {
            if (updating_widgets_ || display_output_combo_ == nullptr) {
                return;
            }

            const auto index = display_output_combo_->get_active_row_number();
            if (index < 0 || static_cast<std::size_t>(index) >= display_output_values_.size()) {
                return;
            }

            primary_output_name_ = display_output_values_[static_cast<std::size_t>(index)];
            config_manager_.set_value({"display", "primary_output"}, primary_output_name_);
            config_manager_.save();

            rebuild_display_layout_preview();
            refresh_display_mode_combos();
            sync_display_controls_for_primary_output();
            set_status_caption(tr("cc_status_saved"));
        });

        return create_row(tr("cc_row_display_target_title"), tr("cc_row_display_target_subtitle"), *combo);
    }

    DisplayCapabilities detect_display_capabilities(const std::string& output_name) const {
        auto capabilities = DisplayCapabilities{};

        const auto json_output = read_command_output("sh -lc 'command -v wlr-randr >/dev/null 2>&1 && wlr-randr --json 2>/dev/null'");
        auto parsed = nlohmann::json{};
        if (!json_output.empty()) {
            parsed = nlohmann::json::parse(json_output, nullptr, false);
        }

        const auto extract_hz = [this](const nlohmann::json& mode) -> std::string {
            if (!mode.is_object()) {
                return {};
            }

            const auto parse_refresh_field = [this](const nlohmann::json& field) -> std::string {
                double hz = 0.0;
                if (field.is_number()) {
                    hz = field.get<double>();
                } else if (field.is_string()) {
                    const auto text = trim(field.get<std::string>());
                    if (text.empty()) {
                        return {};
                    }

                    std::smatch match;
                    static const auto numeric_pattern = std::regex(R"(([0-9]+(?:\.[0-9]+)?))", std::regex::icase);
                    if (!std::regex_search(text, match, numeric_pattern) || match.size() < 2) {
                        return {};
                    }
                    hz = std::stod(match[1].str());
                } else {
                    return {};
                }

                if (hz > 1000.0) {
                    hz /= 1000.0;
                }
                if (hz <= 1.0) {
                    return {};
                }
                return normalize_refresh_value(hz);
            };

            static const auto refresh_keys = std::array<const char*, 8>{
                "refresh",
                "refresh_rate",
                "hz",
                "refresh_hz",
                "refreshHz",
                "refresh_mhz",
                "refresh_milli_hz",
                "mHz"
            };

            for (const auto* key : refresh_keys) {
                if (!mode.contains(key)) {
                    continue;
                }
                const auto parsed = parse_refresh_field(mode[key]);
                if (!parsed.empty()) {
                    return parsed;
                }
            }

            return {};
        };

        const auto collect_modes = [&](const nlohmann::json& output) {
            auto resolutions_seen = std::set<std::string>{};
            auto refresh_seen = std::set<std::string>{};
            auto refresh_per_res_seen = std::map<std::string, std::set<std::string>>{};
            static const auto mode_string_pattern = std::regex(R"((\d+)\s*x\s*(\d+)(?:[^0-9]+([0-9]+(?:\.[0-9]+)?))?)", std::regex::icase);

            if (output.contains("modes") && output["modes"].is_array()) {
                for (const auto& mode : output["modes"]) {
                    if (mode.is_string()) {
                        std::smatch mode_match;
                        const auto mode_text = mode.get<std::string>();
                        if (std::regex_search(mode_text, mode_match, mode_string_pattern) && mode_match.size() >= 3) {
                            const auto width = std::stoi(mode_match[1].str());
                            const auto height = std::stoi(mode_match[2].str());
                            std::string res_value;
                            if (width > 0 && height > 0) {
                                res_value = std::to_string(width) + "x" + std::to_string(height);
                                if (resolutions_seen.insert(res_value).second) {
                                    capabilities.resolution_values.push_back(res_value);
                                }
                            }

                            if (mode_match.size() >= 4 && mode_match[3].matched) {
                                const auto hz_value = normalize_refresh_value(std::stod(mode_match[3].str()));
                                if (!hz_value.empty() && refresh_seen.insert(hz_value).second) {
                                    capabilities.refresh_values.push_back(hz_value);
                                }
                                if (!hz_value.empty() && !res_value.empty() && refresh_per_res_seen[res_value].insert(hz_value).second) {
                                    capabilities.refresh_per_resolution[res_value].push_back(hz_value);
                                }
                            }
                        }
                        continue;
                    }

                    if (!mode.is_object()) {
                        continue;
                    }

                    int width = 0;
                    int height = 0;
                    if (mode.contains("width") && mode["width"].is_number_integer()) {
                        width = mode["width"].get<int>();
                    } else if (mode.contains("w") && mode["w"].is_number_integer()) {
                        width = mode["w"].get<int>();
                    }
                    if (mode.contains("height") && mode["height"].is_number_integer()) {
                        height = mode["height"].get<int>();
                    } else if (mode.contains("h") && mode["h"].is_number_integer()) {
                        height = mode["h"].get<int>();
                    }

                    std::string res_value;
                    if (width > 0 && height > 0) {
                        res_value = std::to_string(width) + "x" + std::to_string(height);
                        if (resolutions_seen.insert(res_value).second) {
                            capabilities.resolution_values.push_back(res_value);
                        }
                    }

                    const auto hz_value = extract_hz(mode);
                    if (!hz_value.empty() && refresh_seen.insert(hz_value).second) {
                        capabilities.refresh_values.push_back(hz_value);
                    }
                    if (!hz_value.empty() && !res_value.empty() && refresh_per_res_seen[res_value].insert(hz_value).second) {
                        capabilities.refresh_per_resolution[res_value].push_back(hz_value);
                    }
                }
            }

            const auto add_mode_from_object = [&](const nlohmann::json& mode) {
                int width = 0;
                int height = 0;
                if (!mode.is_object()) {
                    return;
                }

                if (mode.contains("width") && mode["width"].is_number_integer()) {
                    width = mode["width"].get<int>();
                } else if (mode.contains("w") && mode["w"].is_number_integer()) {
                    width = mode["w"].get<int>();
                }
                if (mode.contains("height") && mode["height"].is_number_integer()) {
                    height = mode["height"].get<int>();
                } else if (mode.contains("h") && mode["h"].is_number_integer()) {
                    height = mode["h"].get<int>();
                }

                if (width > 0 && height > 0) {
                    const auto value = std::to_string(width) + "x" + std::to_string(height);
                    if (resolutions_seen.insert(value).second) {
                        capabilities.resolution_values.push_back(value);
                    }

                    const auto hz_value = extract_hz(mode);
                    if (!hz_value.empty() && refresh_seen.insert(hz_value).second) {
                        capabilities.refresh_values.push_back(hz_value);
                    }
                    if (!hz_value.empty() && refresh_per_res_seen[value].insert(hz_value).second) {
                        capabilities.refresh_per_resolution[value].push_back(hz_value);
                    }
                } else {
                    const auto hz_value = extract_hz(mode);
                    if (!hz_value.empty() && refresh_seen.insert(hz_value).second) {
                        capabilities.refresh_values.push_back(hz_value);
                    }
                }
            };

            if (output.contains("current_mode") && output["current_mode"].is_object()) {
                add_mode_from_object(output["current_mode"]);
            }
            if (output.contains("currentMode") && output["currentMode"].is_object()) {
                add_mode_from_object(output["currentMode"]);
            }
            if (output.contains("mode") && output["mode"].is_object()) {
                add_mode_from_object(output["mode"]);
            }
        };

        const auto normalize_output_name = [](std::string value) {
            value = lowercase(trim(value));
            if (value.find("output ") == 0) {
                value = trim(value.substr(7));
            }
            if (!value.empty() && value.back() == ':') {
                value.pop_back();
            }
            return trim(value);
        };

        const auto parse_display_alias_index = [&](const std::string& value) {
            std::smatch match;
            static const auto alias_pattern = std::regex(R"(^display-([0-9]+)$)", std::regex::icase);
            const auto normalized = normalize_output_name(value);
            if (!std::regex_match(normalized, match, alias_pattern) || match.size() < 2) {
                return -1;
            }

            const auto parsed_index = std::stoi(match[1].str()) - 1;
            return parsed_index >= 0 ? parsed_index : -1;
        };

        const auto requested_name = normalize_output_name(output_name);
        const auto requested_alias_index = parse_display_alias_index(output_name);

        auto parsed_outputs = std::vector<std::pair<std::string, nlohmann::json>>{};
        if (!parsed.is_discarded() && parsed.is_array()) {
            for (const auto& output : parsed) {
                parsed_outputs.push_back({std::string{}, output});
            }
        } else if (!parsed.is_discarded() && parsed.is_object()) {
            if (parsed.contains("outputs") && parsed["outputs"].is_array()) {
                for (const auto& output : parsed["outputs"]) {
                    parsed_outputs.push_back({std::string{}, output});
                }
            } else if (parsed.contains("outputs") && parsed["outputs"].is_object()) {
                for (const auto& [name, output] : parsed["outputs"].items()) {
                    parsed_outputs.push_back({name, output});
                }
            } else {
                for (const auto& [name, output] : parsed.items()) {
                    parsed_outputs.push_back({name, output});
                }
            }
        }

        bool matched = false;
        for (const auto& [fallback_name, output] : parsed_outputs) {
            if (!output.is_object()) {
                continue;
            }

            auto name = output.value("name", std::string{});
            if (name.empty()) {
                name = fallback_name;
            }
            if (normalize_output_name(name) != requested_name) {
                continue;
            }

            collect_modes(output);
            matched = true;
            break;
        }

        if (!matched && requested_alias_index >= 0 && static_cast<std::size_t>(requested_alias_index) < parsed_outputs.size()) {
            const auto& output = parsed_outputs[static_cast<std::size_t>(requested_alias_index)].second;
            if (output.is_object()) {
                collect_modes(output);
                matched = true;
            }
        }

        if (!matched && parsed_outputs.size() == 1 && parsed_outputs.front().second.is_object()) {
            collect_modes(parsed_outputs.front().second);
            matched = true;
        }

        if (!matched) {
            const auto text_output = read_command_output("sh -lc 'command -v wlr-randr >/dev/null 2>&1 && wlr-randr 2>/dev/null'");
            if (!text_output.empty()) {
                auto resolutions_seen = std::set<std::string>{};
                auto refresh_seen = std::set<std::string>{};
                auto refresh_per_res_seen = std::map<std::string, std::set<std::string>>{};

                const auto parse_mode_line = [this, &resolutions_seen, &refresh_seen, &refresh_per_res_seen, &capabilities](const std::string& line) {
                    static const auto mode_pattern = std::regex(R"((\d+)\s*x\s*(\d+)(?:[^0-9]+([0-9]+(?:\.[0-9]+)?))?)", std::regex::icase);
                    std::smatch mode_match;
                    if (!std::regex_search(line, mode_match, mode_pattern) || mode_match.size() < 3) {
                        return;
                    }

                    const auto width = std::stoi(mode_match[1].str());
                    const auto height = std::stoi(mode_match[2].str());
                    std::string resolution_value;
                    if (width > 0 && height > 0) {
                        resolution_value = std::to_string(width) + "x" + std::to_string(height);
                        if (resolutions_seen.insert(resolution_value).second) {
                            capabilities.resolution_values.push_back(resolution_value);
                        }
                    }

                    if (mode_match.size() >= 4) {
                        const auto hz_value = normalize_refresh_value(std::stod(mode_match[3].str()));
                        if (!hz_value.empty() && refresh_seen.insert(hz_value).second) {
                            capabilities.refresh_values.push_back(hz_value);
                        }
                        if (!hz_value.empty() && !resolution_value.empty() && refresh_per_res_seen[resolution_value].insert(hz_value).second) {
                            capabilities.refresh_per_resolution[resolution_value].push_back(hz_value);
                        }
                    }
                };

                std::istringstream stream(text_output);
                auto line = std::string{};
                auto inside_target_output = false;
                auto output_header_index = -1;
                auto output_headers_seen = 0;
                while (std::getline(stream, line)) {
                    if (line.empty()) {
                        continue;
                    }

                    const auto is_header = !std::isspace(static_cast<unsigned char>(line.front()));
                    if (is_header) {
                        auto normalized = trim(line);
                        if (lowercase(normalized).find("output ") == 0) {
                            normalized = trim(normalized.substr(7));
                        }

                        auto output_token = normalized.substr(0, normalized.find(' '));
                        output_token = normalize_output_name(output_token);
                        ++output_header_index;
                        ++output_headers_seen;

                        inside_target_output = output_token == requested_name
                            || (requested_alias_index >= 0 && output_header_index == requested_alias_index);
                        if (inside_target_output) {
                            parse_mode_line(normalized);
                        }
                        continue;
                    }

                    if (inside_target_output) {
                        parse_mode_line(line);
                    }
                }

                if (capabilities.refresh_values.empty() && capabilities.resolution_values.empty() && output_headers_seen == 1) {
                    std::istringstream fallback_stream(text_output);
                    while (std::getline(fallback_stream, line)) {
                        if (line.empty()) {
                            continue;
                        }
                        parse_mode_line(line);
                    }
                }
            }
        }

        std::sort(capabilities.resolution_values.begin(), capabilities.resolution_values.end(), [](const auto& left, const auto& right) {
            const auto parse_pair = [](const std::string& value) {
                const auto x = value.find('x');
                if (x == std::string::npos) {
                    return std::pair<int, int>{0, 0};
                }
                return std::pair<int, int>{std::stoi(value.substr(0, x)), std::stoi(value.substr(x + 1))};
            };
            const auto [lw, lh] = parse_pair(left);
            const auto [rw, rh] = parse_pair(right);
            const auto left_pixels = static_cast<long long>(lw) * static_cast<long long>(lh);
            const auto right_pixels = static_cast<long long>(rw) * static_cast<long long>(rh);
            if (left_pixels == right_pixels) {
                return left > right;
            }
            return left_pixels > right_pixels;
        });

        std::sort(capabilities.refresh_values.begin(), capabilities.refresh_values.end(), [](const auto& left, const auto& right) {
            return std::stod(left) < std::stod(right);
        });

        for (auto& [res, rates] : capabilities.refresh_per_resolution) {
            std::sort(rates.begin(), rates.end(), [](const auto& left, const auto& right) {
                return std::stod(left) < std::stod(right);
            });
        }

        for (const auto& value : capabilities.resolution_values) {
            const auto x = value.find('x');
            if (x == std::string::npos) {
                capabilities.resolution_labels.push_back(value);
            } else {
                capabilities.resolution_labels.push_back(value.substr(0, x) + " x " + value.substr(x + 1));
            }
        }

        for (const auto& value : capabilities.refresh_values) {
            capabilities.refresh_labels.push_back(value + " Hz");
        }

        return capabilities;
    }

    void refresh_display_mode_combos() {
        if (resolution_combo_ == nullptr || refresh_combo_ == nullptr) {
            return;
        }

        const auto output_name = active_display_output_name();

        auto capabilities = detect_display_capabilities(output_name);
        if (capabilities.resolution_values.empty()) {
            const auto fallback = build_resolution_options();
            capabilities.resolution_labels = fallback.first;
            capabilities.resolution_values = fallback.second;
        }
        if (capabilities.refresh_values.empty()) {
            auto refresh_values = std::set<std::string>{"60"};
            const auto configured_refresh = output_name.empty()
                ? config_string({"display", "refresh_rate"}, "60")
                : config_string({"display", "outputs", output_name, "refresh_rate"}, config_string({"display", "refresh_rate"}, "60"));
            const auto normalized_configured_refresh = normalize_refresh_value(configured_refresh, "60");
            if (!normalized_configured_refresh.empty()) {
                refresh_values.insert(normalized_configured_refresh);
            }

            capabilities.refresh_values.assign(refresh_values.begin(), refresh_values.end());
            std::sort(capabilities.refresh_values.begin(), capabilities.refresh_values.end(), [](const auto& left, const auto& right) {
                return std::stod(left) < std::stod(right);
            });
            capabilities.refresh_labels.clear();
            for (const auto& value : capabilities.refresh_values) {
                capabilities.refresh_labels.push_back(value + " Hz");
            }
        }

        cached_capabilities_ = capabilities;

        auto hw_resolution = std::string{"1920x1080"};
        auto hw_refresh = std::string{"60"};
        if (!output_name.empty()) {
            const auto it = std::find_if(display_outputs_preview_.begin(), display_outputs_preview_.end(),
                [&output_name](const DisplayOutputPreview& o) { return o.name == output_name; });
            if (it != display_outputs_preview_.end()) {
                hw_resolution = std::to_string(it->width) + "x" + std::to_string(it->height);
                if (!it->refresh_rate.empty()) {
                    hw_refresh = it->refresh_rate;
                }
            }
        }

        const auto selected_resolution = output_name.empty()
            ? config_string({"display", "resolution"}, hw_resolution)
            : config_string({"display", "outputs", output_name, "resolution"}, config_string({"display", "resolution"}, hw_resolution));
        const auto selected_refresh = output_name.empty()
            ? normalize_refresh_value(config_string({"display", "refresh_rate"}, hw_refresh), hw_refresh)
            : normalize_refresh_value(config_string({"display", "outputs", output_name, "refresh_rate"}, config_string({"display", "refresh_rate"}, hw_refresh)), hw_refresh);

        auto filtered_refresh_values = capabilities.refresh_values;
        auto filtered_refresh_labels = capabilities.refresh_labels;
        const auto res_it = capabilities.refresh_per_resolution.find(selected_resolution);
        if (res_it != capabilities.refresh_per_resolution.end() && !res_it->second.empty()) {
            filtered_refresh_values = res_it->second;
            filtered_refresh_labels.clear();
            for (const auto& v : filtered_refresh_values) {
                filtered_refresh_labels.push_back(v + " Hz");
            }
        }

        updating_widgets_ = true;

        resolution_combo_->remove_all();
        for (const auto& label : capabilities.resolution_labels) {
            resolution_combo_->append(label);
        }
        resolution_combo_->set_active(index_for_value(capabilities.resolution_values, selected_resolution));
        update_bound_combo_values(resolution_combo_, capabilities.resolution_values);

        refresh_combo_->remove_all();
        for (const auto& label : filtered_refresh_labels) {
            refresh_combo_->append(label);
        }
        refresh_combo_->set_active(index_for_value(filtered_refresh_values, selected_refresh));
        update_bound_combo_values(refresh_combo_, filtered_refresh_values);

        updating_widgets_ = false;
    }

    void update_refresh_for_selected_resolution() {
        if (refresh_combo_ == nullptr || resolution_combo_ == nullptr) {
            return;
        }

        const auto* res_values = bound_combo_values(resolution_combo_);
        if (res_values == nullptr) {
            return;
        }

        const auto res_index = resolution_combo_->get_active_row_number();
        if (res_index < 0 || static_cast<std::size_t>(res_index) >= res_values->size()) {
            return;
        }

        const auto& selected_res = (*res_values)[static_cast<std::size_t>(res_index)];

        auto refresh_values = cached_capabilities_.refresh_values;
        const auto it = cached_capabilities_.refresh_per_resolution.find(selected_res);
        if (it != cached_capabilities_.refresh_per_resolution.end() && !it->second.empty()) {
            refresh_values = it->second;
        }

        if (refresh_values.empty()) {
            refresh_values = {"60"};
        }

        std::vector<std::string> refresh_labels;
        for (const auto& v : refresh_values) {
            refresh_labels.push_back(v + " Hz");
        }

        const auto output_name = active_display_output_name();
        const auto current_refresh = output_name.empty()
            ? normalize_refresh_value(config_string({"display", "refresh_rate"}, "60"), "60")
            : normalize_refresh_value(config_string({"display", "outputs", output_name, "refresh_rate"}, config_string({"display", "refresh_rate"}, "60")), "60");

        updating_widgets_ = true;
        refresh_combo_->remove_all();
        for (const auto& label : refresh_labels) {
            refresh_combo_->append(label);
        }
        auto refresh_index = index_for_value(refresh_values, current_refresh);
        if (refresh_index < 0 && !refresh_values.empty()) {
            refresh_index = static_cast<int>(refresh_values.size()) - 1;
        }
        refresh_combo_->set_active(refresh_index);
        update_bound_combo_values(refresh_combo_, refresh_values);
        updating_widgets_ = false;

        if (refresh_index >= 0 && static_cast<std::size_t>(refresh_index) < refresh_values.size()) {
            const auto& new_refresh = refresh_values[static_cast<std::size_t>(refresh_index)];
            config_manager_.set_value({"display", "refresh_rate"}, new_refresh);
            mirror_display_setting_to_active_output({"display", "refresh_rate"}, new_refresh);
        }
    }

    void sync_preview_for_active_output_from_config() {
        const auto output_name = active_display_output_name();
        if (output_name.empty()) {
            return;
        }

        auto iterator = std::find_if(
            display_outputs_preview_.begin(),
            display_outputs_preview_.end(),
            [&output_name](const DisplayOutputPreview& output) { return output.name == output_name; }
        );
        if (iterator == display_outputs_preview_.end()) {
            return;
        }

        const auto default_resolution = std::to_string(std::max(320, iterator->width))
            + "x"
            + std::to_string(std::max(200, iterator->height));
        const auto resolution = config_string(
            {"display", "outputs", output_name, "resolution"},
            config_string({"display", "resolution"}, default_resolution)
        );

        int width = iterator->width;
        int height = iterator->height;
        if (parse_resolution_value(resolution, width, height)) {
            iterator->width = std::max(320, width);
            iterator->height = std::max(200, height);
        }

        const auto scaling = config_string(
            {"display", "outputs", output_name, "scaling"},
            config_string({"display", "scaling"}, "100")
        );
        try {
            iterator->scale = std::max(0.5, std::stod(scaling) / 100.0);
        } catch (...) {
            // Keep previous scale when parsing fails.
        }

        const auto refresh = config_string(
            {"display", "outputs", output_name, "refresh_rate"},
            config_string({"display", "refresh_rate"}, "60")
        );
        iterator->refresh_rate = normalize_refresh_value(refresh, "");

        const auto orientation = config_string(
            {"display", "outputs", output_name, "orientation"},
            config_string({"display", "orientation"}, "landscape")
        );
        if (orientation == "portrait-left" || orientation == "portrait-right") {
            std::swap(iterator->width, iterator->height);
        }

        rebuild_display_layout_preview();
        refresh_display_output_selector();
    }

    void sync_display_controls_for_primary_output() {
        const auto output_name = active_display_output_name();
        if (output_name.empty()) {
            return;
        }

        const auto orientation_values = std::vector<std::string>{"landscape", "portrait-left", "portrait-right", "inverted"};
        const auto scaling_values = std::vector<std::string>{"100", "125", "150", "175", "200"};

        const auto selected_orientation = config_string(
            {"display", "outputs", output_name, "orientation"},
            config_string({"display", "orientation"}, "landscape")
        );
        const auto selected_scaling = config_string(
            {"display", "outputs", output_name, "scaling"},
            config_string({"display", "scaling"}, "100")
        );
        const auto selected_vrr = config_bool(
            {"display", "outputs", output_name, "vrr"},
            config_bool({"display", "vrr"}, false)
        );

        updating_widgets_ = true;
        if (orientation_combo_ != nullptr) {
            orientation_combo_->set_active(index_for_value(orientation_values, selected_orientation));
        }
        if (scaling_combo_ != nullptr) {
            scaling_combo_->set_active(index_for_value(scaling_values, selected_scaling));
        }
        if (vrr_switch_ != nullptr) {
            vrr_switch_->set_active(selected_vrr);
        }
        updating_widgets_ = false;
    }

    std::string preferred_resolution_for_preset(
        const DisplayCapabilities& capabilities,
        const std::string& preset
    ) const {
        if (capabilities.resolution_values.empty()) {
            return "1920x1080";
        }

        if (preset == "presentation") {
            const auto preferred = std::string{"1920x1080"};
            if (std::find(capabilities.resolution_values.begin(), capabilities.resolution_values.end(), preferred)
                != capabilities.resolution_values.end()) {
                return preferred;
            }
        }

        return capabilities.resolution_values.front();
    }

    std::string preferred_refresh_for_preset(
        const DisplayCapabilities& capabilities,
        const std::string& preset
    ) const {
        if (capabilities.refresh_values.empty()) {
            return "60";
        }

        if (preset == "gaming") {
            return capabilities.refresh_values.back();
        }

        const auto sixty = std::string{"60"};
        if (std::find(capabilities.refresh_values.begin(), capabilities.refresh_values.end(), sixty)
            != capabilities.refresh_values.end()) {
            return sixty;
        }

        return capabilities.refresh_values.front();
    }

    void apply_display_preset(const std::string& preset) {
        auto outputs = detect_display_outputs_preview();
        if (outputs.empty()) {
            set_status_caption(tr("cc_status_monitors_not_detected"));
            return;
        }

        for (const auto& output : outputs) {
            const auto capabilities = detect_display_capabilities(output.name);
            const auto fallback_resolution = std::to_string(std::max(320, output.width)) + "x" + std::to_string(std::max(200, output.height));
            const auto resolution = capabilities.resolution_values.empty()
                ? fallback_resolution
                : preferred_resolution_for_preset(capabilities, preset);
            const auto refresh = capabilities.refresh_values.empty()
                ? normalize_refresh_value(config_string({"display", "outputs", output.name, "refresh_rate"}, config_string({"display", "refresh_rate"}, "60")), "60")
                : preferred_refresh_for_preset(capabilities, preset);
            const auto orientation = std::string{"landscape"};
            const auto scaling = preset == "office" ? std::string{"125"} : std::string{"100"};
            const auto vrr = preset == "gaming";

            config_manager_.set_value({"display", "outputs", output.name, "resolution"}, resolution);
            config_manager_.set_value({"display", "outputs", output.name, "refresh_rate"}, refresh);
            config_manager_.set_value({"display", "outputs", output.name, "orientation"}, orientation);
            config_manager_.set_value({"display", "outputs", output.name, "scaling"}, scaling);
            config_manager_.set_value({"display", "outputs", output.name, "vrr"}, vrr);
        }

        const auto active_output = active_display_output_name().empty() ? outputs.front().name : active_display_output_name();
        config_manager_.set_value({"display", "resolution"}, config_string({"display", "outputs", active_output, "resolution"}, "1920x1080"));
        config_manager_.set_value({"display", "refresh_rate"}, config_string({"display", "outputs", active_output, "refresh_rate"}, "60"));
        config_manager_.set_value({"display", "orientation"}, config_string({"display", "outputs", active_output, "orientation"}, "landscape"));
        config_manager_.set_value({"display", "scaling"}, config_string({"display", "outputs", active_output, "scaling"}, "100"));
        config_manager_.set_value({"display", "vrr"}, config_bool({"display", "outputs", active_output, "vrr"}, false));

        if (!config_manager_.save()) {
            set_status_caption(tr("cc_status_save_failed"));
            return;
        }

        apply_display_runtime_settings();
        refresh_display_mode_combos();
        sync_display_controls_for_primary_output();

        if (preset == "gaming") {
            set_status_caption(tr("cc_status_display_preset_gaming_applied"));
        } else if (preset == "presentation") {
            set_status_caption(tr("cc_status_display_preset_presentation_applied"));
        } else {
            set_status_caption(tr("cc_status_display_preset_office_applied"));
        }
    }

    void update_wallpaper_preview(const std::string& wallpaper_path) {
        if (wallpaper_preview_ == nullptr) {
            return;
        }

        std::error_code error;
        if (!wallpaper_path.empty() && std::filesystem::exists(wallpaper_path, error) && !error) {
            wallpaper_preview_->set_filename(wallpaper_path);
            return;
        }

        wallpaper_preview_->set_filename("");
    }

    void apply_appearance_changes_now() {
        if (!appearance_changes_pending_) {
            set_status_caption(tr("cc_status_nothing_to_apply"));
            return;
        }

        appearance_config_ = config_manager_.appearance();
        selected_theme_ = normalize_standard_theme(config_string({"appearance", "theme"}, selected_theme_.empty() ? std::string{"hyalo"} : selected_theme_));
        selected_color_mode_ = normalize_color_mode(config_string({"appearance", "color_mode"}, selected_color_mode_.empty() ? std::string{"auto"} : selected_color_mode_));

        write_theme_override();
        persist_config("cc_status_saved", [this]() {
            apply_appearance_runtime();
            sync_widgets_from_config();
            refresh_accent_buttons();
            clear_appearance_changes_pending();
        });
    }

    void apply_wallpaper_changes_now() {
        if (!wallpaper_changes_pending_) {
            set_status_caption(tr("cc_status_nothing_to_apply"));
            return;
        }

        persist_config("cc_status_saved", [this]() {
            apply_wallpaper_runtime();
            clear_wallpaper_changes_pending();
        });
    }

    Gtk::Widget* create_bound_switch_row(
        const std::string& title_key,
        const std::string& subtitle_key,
        std::vector<std::string> path,
        bool active,
        std::function<void(bool)> on_change = {},
        Gtk::Switch** out_switch = nullptr
    ) {
        auto* toggle = Gtk::make_managed<Gtk::Switch>();
        toggle->set_active(active);
        toggle->add_css_class("settings-switch");
        if (out_switch != nullptr) {
            *out_switch = toggle;
        }
        if (!path.empty()) {
            bound_switches_.push_back(BoundSwitch{toggle, path});
        }
        toggle->signal_state_set().connect([this, path = std::move(path), on_change = std::move(on_change)](bool state) {
            if (updating_widgets_) {
                return false;
            }

            config_manager_.set_value(path, state);
            mirror_display_setting_to_active_output(path, state);
            if (on_change) {
                on_change(state);
            }

            if (path_is_wallpaper_only_change(path)) {
                mark_wallpaper_changes_pending();
                return false;
            }

            if (is_appearance_path(path)) {
                if (should_preview_appearance_change(path)) {
                    appearance_config_ = config_manager_.appearance();
                    write_theme_override();
                }
                mark_appearance_changes_pending();
                return false;
            }

            persist_config("cc_status_saved", [this, path]() { apply_runtime_effects_for_path(path); });
            return false;
        }, false);
        return create_row(tr(title_key), tr(subtitle_key), *toggle);
    }

    Gtk::Widget* create_bound_combo_row(
        const std::string& title_key,
        const std::string& subtitle_key,
        const std::vector<std::string>& labels,
        const std::vector<std::string>& values,
        int active_index,
        Gtk::ComboBoxText*& out_combo,
        std::vector<std::string> path = {},
        std::function<void(int)> on_change = {}
    ) {
        auto* combo = Gtk::make_managed<Gtk::ComboBoxText>();
        combo->add_css_class("settings-combo");
        for (const auto& label : labels) {
            combo->append(label);
        }
        combo->set_active(active_index);
        if (!path.empty()) {
            bound_combos_.push_back(BoundCombo{combo, path, values});
        }
        combo->signal_changed().connect([this, combo, values, path = std::move(path), on_change = std::move(on_change)]() {
            if (updating_widgets_) {
                return;
            }

            const auto* dynamic_values = bound_combo_values(combo);
            const auto& active_values = dynamic_values != nullptr ? *dynamic_values : values;

            const auto index = combo->get_active_row_number();
            if (index < 0 || static_cast<std::size_t>(index) >= active_values.size()) {
                return;
            }

            if (!path.empty()) {
                config_manager_.set_value(path, active_values[static_cast<std::size_t>(index)]);
                mirror_display_setting_to_active_output(path, active_values[static_cast<std::size_t>(index)]);

                const auto display_resolution_or_scaling_changed = path.size() == 2
                    && path[0] == "display"
                    && (path[1] == "resolution" || path[1] == "scaling" || path[1] == "refresh_rate" || path[1] == "orientation");
                if (display_resolution_or_scaling_changed) {
                    sync_preview_for_active_output_from_config();
                }
            }
            if (on_change) {
                on_change(index);
            }

            if (path_is_wallpaper_only_change(path)) {
                mark_wallpaper_changes_pending();
                return;
            }

            if (is_appearance_path(path)) {
                if (should_preview_appearance_change(path)) {
                    appearance_config_ = config_manager_.appearance();
                    write_theme_override();
                }
                mark_appearance_changes_pending();
                return;
            }

            const auto keyboard_layout_changed = path.size() == 2 && path[0] == "input" && path[1] == "keyboard_layout";
            if (keyboard_layout_changed) {
                refresh_keyboard_preview();
            }
            persist_config(
                keyboard_layout_changed ? "cc_status_keyboard_layout_restart_session" : "cc_status_saved",
                [this, path]() { apply_runtime_effects_for_path(path); }
            );
        });
        out_combo = combo;
        return create_row(tr(title_key), tr(subtitle_key), *combo);
    }

    Gtk::Widget* create_bound_scale_row(
        const std::string& title_key,
        const std::string& subtitle_key,
        double min,
        double max,
        double value,
        std::function<Glib::ustring(double)> formatter,
        Gtk::Scale*& out_scale,
        Gtk::Label*& out_value_label,
        std::vector<std::string> path = {},
        std::function<void(double)> on_change = {}
    ) {
        auto* control_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        control_box->add_css_class("settings-slider-box");

        auto* scale = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);
        scale->set_range(min, max);
        scale->set_value(value);
        scale->set_draw_value(false);
        scale->set_size_request(180, -1);
        scale->add_css_class("settings-scale");
        if (!path.empty()) {
            bound_scales_.push_back(BoundScale{scale, path});
        }

        auto* value_label = make_label(formatter(value), "settings-inline-value");
        scale->signal_value_changed().connect([this, scale, value_label, formatter = std::move(formatter), path = std::move(path), on_change = std::move(on_change)]() {
            value_label->set_text(formatter(scale->get_value()));
            if (updating_widgets_) {
                return;
            }

            if (!path.empty()) {
                config_manager_.set_value(path, scale->get_value());
            }
            if (on_change) {
                on_change(scale->get_value());
            }

            if (path_is_wallpaper_only_change(path)) {
                mark_wallpaper_changes_pending();
                return;
            }

            if (is_appearance_path(path)) {
                if (should_preview_appearance_change(path)) {
                    appearance_config_ = config_manager_.appearance();
                    write_theme_override();
                }
                mark_appearance_changes_pending();
                return;
            }

            persist_config("cc_status_saved", [this, path]() { apply_runtime_effects_for_path(path); });
        });

        control_box->append(*scale);
        control_box->append(*value_label);

        out_scale = scale;
        out_value_label = value_label;
        return create_row(tr(title_key), tr(subtitle_key), *control_box);
    }

    Gtk::Widget* create_accent_row() {
        auto* swatches = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        swatches->add_css_class("settings-swatch-strip");

        accent_buttons_.clear();
        for (const auto& option : accent_options_) {
            auto* swatch = Gtk::make_managed<Gtk::Button>();
            swatch->add_css_class("settings-swatch");
            swatch->add_css_class(option.css_class);
            swatch->set_tooltip_text(tr(option.label_key));
            swatch->signal_clicked().connect([this, color = std::string{option.color}]() {
                appearance_config_.accent_color = color;
                config_manager_.set_value({"appearance", "accent_color"}, color);
                refresh_accent_buttons();
                write_theme_override();
                mark_appearance_changes_pending();
            });
            accent_buttons_.push_back(swatch);
            swatches->append(*swatch);
        }

        refresh_accent_buttons();
        return create_row(tr("cc_row_accent_title"), tr("cc_row_accent_subtitle"), *swatches);
    }

    Gtk::Widget* create_appearance_apply_row() {
        auto* button = Gtk::make_managed<Gtk::Button>(tr("cc_action_apply"));
        button->add_css_class("hyalo-button");
        button->add_css_class("hyalo-button-primary");
        button->add_css_class("accent");
        button->add_css_class("settings-apply-button");
        button->set_sensitive(true);
        button->signal_clicked().connect([this]() {
            apply_appearance_changes_now();
        });
        appearance_apply_button_ = button;
        return create_row(tr("cc_row_apply_appearance_title"), tr("cc_row_apply_appearance_subtitle"), *button);
    }

    Gtk::Widget* create_wallpaper_apply_row() {
        auto* button = Gtk::make_managed<Gtk::Button>(tr("cc_action_apply"));
        button->add_css_class("hyalo-button");
        button->add_css_class("hyalo-button-primary");
        button->add_css_class("accent");
        button->add_css_class("settings-apply-button");
        button->set_sensitive(true);
        button->signal_clicked().connect([this]() {
            apply_wallpaper_changes_now();
        });
        return create_row(tr("cc_row_apply_wallpaper_title"), tr("cc_row_apply_wallpaper_subtitle"), *button);
    }

    Gtk::Widget* create_wallpaper_preview_row(const std::string& wallpaper_path) {
        auto* preview = Gtk::make_managed<Gtk::Picture>();
        preview->set_size_request(280, 158);
        preview->set_can_shrink(true);
        preview->set_keep_aspect_ratio(true);
        preview->add_css_class("settings-wallpaper-preview");
        wallpaper_preview_ = preview;
        update_wallpaper_preview(wallpaper_path);
        return create_row(tr("cc_row_wallpaper_preview_title"), tr("cc_row_wallpaper_preview_subtitle"), *preview);
    }

    Gtk::Widget* create_gpu_drivers_manage_row() {
        auto* controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        controls->set_hexpand(true);
        controls->add_css_class("gpu-driver-stack");

        selected_gpu_vendor_filter_ = detect_preferred_gpu_vendor_filter();

        auto* installed_label = Gtk::make_managed<Gtk::Label>();
        installed_label->set_wrap(true);
        installed_label->set_xalign(0.0f);
        installed_label->set_halign(Gtk::Align::START);
        installed_label->add_css_class("settings-row-subtitle");
        installed_label->add_css_class("gpu-driver-installed-label");
        gpu_installed_drivers_label_ = installed_label;

        auto* top_controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        top_controls->add_css_class("gpu-driver-toolbar");

        auto* refresh_installed_button = Gtk::make_managed<Gtk::Button>(tr("cc_action_refresh_installed"));
        refresh_installed_button->add_css_class("hyalo-button");
        refresh_installed_button->add_css_class("hyalo-button-secondary");
        refresh_installed_button->signal_clicked().connect([this]() {
            refresh_installed_gpu_drivers();
            set_status_caption(tr("cc_status_gpu_installed_refreshed"));
        });

        auto* vendor_combo = Gtk::make_managed<Gtk::ComboBoxText>();
        vendor_combo->add_css_class("settings-combo");
        vendor_combo->add_css_class("gpu-driver-vendor-combo");
        vendor_combo->set_hexpand(true);
        vendor_combo->append(tr("cc_gpu_vendor_all"));
        vendor_combo->append(tr("cc_gpu_vendor_nvidia"));
        vendor_combo->append(tr("cc_gpu_vendor_amd"));
        vendor_combo->append(tr("cc_gpu_vendor_intel"));
        if (selected_gpu_vendor_filter_ == "nvidia") {
            vendor_combo->set_active(1);
        } else if (selected_gpu_vendor_filter_ == "amd") {
            vendor_combo->set_active(2);
        } else if (selected_gpu_vendor_filter_ == "intel") {
            vendor_combo->set_active(3);
        } else {
            vendor_combo->set_active(0);
        }
        vendor_combo->signal_changed().connect([this, vendor_combo]() {
            const auto index = vendor_combo->get_active_row_number();
            if (index == 1) {
                selected_gpu_vendor_filter_ = "nvidia";
            } else if (index == 2) {
                selected_gpu_vendor_filter_ = "amd";
            } else if (index == 3) {
                selected_gpu_vendor_filter_ = "intel";
            } else {
                selected_gpu_vendor_filter_ = "all";
            }
        });
        gpu_vendor_filter_combo_ = vendor_combo;

        auto* scan_button = Gtk::make_managed<Gtk::Button>(tr("cc_action_scan"));
        scan_button->add_css_class("hyalo-button");
        scan_button->add_css_class("hyalo-button-secondary");
        scan_button->signal_clicked().connect([this]() {
            scan_available_gpu_drivers();
        });

        top_controls->append(*refresh_installed_button);
        top_controls->append(*vendor_combo);
        top_controls->append(*scan_button);

        auto* install_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        install_row->add_css_class("gpu-driver-install-row");
        auto* combo = Gtk::make_managed<Gtk::ComboBoxText>();
        combo->set_hexpand(true);
        combo->add_css_class("settings-combo");
        gpu_driver_candidate_combo_ = combo;

        auto* install_button = Gtk::make_managed<Gtk::Button>(tr("cc_action_install"));
        install_button->add_css_class("hyalo-button");
        install_button->add_css_class("hyalo-button-primary");
        install_button->add_css_class("accent");
        install_button->signal_clicked().connect([this]() {
            install_selected_gpu_driver();
        });
        gpu_driver_install_button_ = install_button;

        install_row->append(*combo);
        install_row->append(*install_button);

        controls->append(*installed_label);
        controls->append(*top_controls);
        controls->append(*install_row);

        refresh_installed_gpu_drivers();
        if (gpu_driver_candidate_combo_ != nullptr) {
            gpu_driver_candidate_combo_->remove_all();
            gpu_driver_candidate_combo_->append(tr("cc_gpu_scan_hint"));
            gpu_driver_candidate_combo_->set_active(0);
        }

        return create_row(tr("cc_row_gpu_drivers_title"), tr("cc_row_gpu_drivers_subtitle"), *controls);
    }

    Gtk::Widget* create_display_presets_row() {
        auto* controls = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        controls->set_hexpand(true);

        auto* office_button = Gtk::make_managed<Gtk::Button>(tr("cc_action_display_preset_office"));
        office_button->add_css_class("hyalo-button");
        office_button->add_css_class("hyalo-button-secondary");
        office_button->signal_clicked().connect([this]() {
            apply_display_preset("office");
        });

        auto* gaming_button = Gtk::make_managed<Gtk::Button>(tr("cc_action_display_preset_gaming"));
        gaming_button->add_css_class("hyalo-button");
        gaming_button->add_css_class("hyalo-button-primary");
        gaming_button->add_css_class("accent");
        gaming_button->signal_clicked().connect([this]() {
            apply_display_preset("gaming");
        });

        auto* presentation_button = Gtk::make_managed<Gtk::Button>(tr("cc_action_display_preset_presentation"));
        presentation_button->add_css_class("hyalo-button");
        presentation_button->add_css_class("hyalo-button-secondary");
        presentation_button->signal_clicked().connect([this]() {
            apply_display_preset("presentation");
        });

        controls->append(*office_button);
        controls->append(*gaming_button);
        controls->append(*presentation_button);

        return create_row(tr("cc_row_display_presets_title"), tr("cc_row_display_presets_subtitle"), *controls);
    }

    std::vector<Gtk::Widget*> build_component_rows() {
        std::vector<Gtk::Widget*> rows;
        rows.push_back(create_info_row("HyaloOS", HYALO_DE_VERSION));
        rows.push_back(create_info_row("hyalo-control-center", HYALO_DE_VERSION));
        rows.push_back(create_info_row("hyalo-panel", HYALO_DE_VERSION));
        rows.push_back(create_info_row("hyalo-core", HYALO_DE_VERSION));
    #if HYALO_HAS_TERMINAL
        rows.push_back(create_info_row("hyalo-terminal", HYALO_DE_VERSION));
#endif
        return rows;
    }

    Gtk::Box* build_appearance_page() {
        auto* page = create_page_shell("cc_page_appearance_kicker", "cc_page_appearance_summary");

        const auto font_family = config_string({"appearance", "font_family"}, "Sans");
        const auto font_size = config_int({"appearance", "font_size"}, 11);
        const auto font_hinting = config_string({"appearance", "font_hinting"}, "subpixel-slight");
        const auto wallpaper = config_string({"appearance", "wallpaper"}, current_system_wallpaper());
        const auto daily_wallpaper = config_bool({"appearance", "daily_wallpaper"}, config_bool({"appearance", "wallpaper_slideshow"}, false));
        const auto wallpaper_interval = std::to_string(std::clamp(config_int({"appearance", "wallpaper_interval_minutes"}, 1440), 1, 10080));
        connected_outputs_ = detect_connected_outputs();
        output_wallpaper_combos_.clear();

        page->append(*create_card(
            "cc_card_appearance_style_title",
            "cc_card_appearance_style_subtitle",
            {
                create_bound_combo_row("cc_row_theme_title", "cc_row_theme_subtitle", theme_labels_, theme_values_, index_for_value(theme_values_, selected_theme_), theme_combo_, {"appearance", "theme"}, [this](int index) { if (static_cast<std::size_t>(index) < theme_values_.size()) { selected_theme_ = theme_values_[static_cast<std::size_t>(index)]; } }),
                create_bound_combo_row("cc_row_icon_pack_title", "cc_row_icon_pack_subtitle", icon_pack_labels_, icon_pack_values_, index_for_value(icon_pack_values_, selected_icon_pack_), icon_pack_combo_, {"appearance", "icon_pack"}, [this](int index) { if (static_cast<std::size_t>(index) < icon_pack_values_.size()) { selected_icon_pack_ = icon_pack_values_[static_cast<std::size_t>(index)]; } }),
                create_bound_combo_row("cc_row_cursor_theme_title", "cc_row_cursor_theme_subtitle", cursor_theme_labels_, cursor_theme_values_, index_for_value(cursor_theme_values_, selected_cursor_theme_), cursor_theme_combo_, {"appearance", "cursor_theme"}, [this](int index) { if (static_cast<std::size_t>(index) < cursor_theme_values_.size()) { selected_cursor_theme_ = cursor_theme_values_[static_cast<std::size_t>(index)]; } }),
                create_bound_combo_row("cc_row_window_theme_title", "cc_row_window_theme_subtitle", window_theme_labels_, window_theme_values_, index_for_value(window_theme_values_, selected_window_theme_), window_theme_combo_, {"appearance", "window_theme"}, [this](int index) { if (static_cast<std::size_t>(index) < window_theme_values_.size()) { selected_window_theme_ = window_theme_values_[static_cast<std::size_t>(index)]; } }),
                create_appearance_apply_row()
            }
        ));

        page->append(*create_card(
            "cc_card_appearance_mood_title",
            "cc_card_appearance_mood_subtitle",
            {
                create_bound_combo_row("cc_row_color_mode_title", "cc_row_color_mode_subtitle", translated_options({"cc_option_color_mode_dark", "cc_option_color_mode_light", "cc_option_color_mode_auto"}), {"dark", "light", "auto"}, index_for_value({"dark", "light", "auto"}, selected_color_mode_), color_mode_combo_, {"appearance", "color_mode"}, [this](int index) { selected_color_mode_ = std::array<std::string, 3>{"dark", "light", "auto"}[static_cast<std::size_t>(index)]; }),
                create_accent_row(),
                create_bound_scale_row("cc_row_transparency_title", "cc_row_transparency_subtitle", 0.0, 1.0, appearance_config_.transparency, [](double current) { return Glib::ustring{std::to_string(static_cast<int>(current * 100.0)) + "%"}; }, transparency_scale_, transparency_value_label_, {"appearance", "transparency"}, [this](double current) { appearance_config_.transparency = current; }),
                create_bound_switch_row("cc_row_blur_title", "cc_row_blur_subtitle", {"appearance", "enable_blur"}, config_bool({"appearance", "enable_blur"}, true)),
                create_appearance_apply_row()
            }
        ));

        std::vector<Gtk::Widget*> wallpaper_rows;
        wallpaper_rows.push_back(create_bound_combo_row("cc_row_wallpaper_title", "cc_row_wallpaper_subtitle", wallpaper_labels_, wallpaper_values_, index_for_value(wallpaper_values_, wallpaper), wallpaper_combo_, {"appearance", "wallpaper"}, [this](int index) {
            if (static_cast<std::size_t>(index) < wallpaper_values_.size()) {
                update_wallpaper_preview(wallpaper_values_[static_cast<std::size_t>(index)]);
            }
        }));
        wallpaper_rows.push_back(create_wallpaper_preview_row(wallpaper));

        for (const auto& output_name : connected_outputs_) {
            Gtk::ComboBoxText* combo = nullptr;
            const auto output_wallpaper = output_wallpaper_value(output_name);
            wallpaper_rows.push_back(create_bound_combo_row(
                tr("cc_row_wallpaper_output_title") + " " + output_name,
                tr("cc_row_wallpaper_output_subtitle"),
                wallpaper_labels_,
                wallpaper_values_,
                index_for_value(wallpaper_values_, output_wallpaper),
                combo,
                {"appearance", "wallpaper_per_output", output_name},
                [this](int index) {
                    if (static_cast<std::size_t>(index) < wallpaper_values_.size()) {
                        update_wallpaper_preview(wallpaper_values_[static_cast<std::size_t>(index)]);
                    }
                }
            ));
            output_wallpaper_combos_.push_back(OutputWallpaperBinding{output_name, combo});
        }

        wallpaper_rows.push_back(create_wallpaper_apply_row());
        wallpaper_rows.push_back(create_action_row("cc_row_wallpaper_import_title", "cc_row_wallpaper_import_subtitle", "cc_action_import_wallpaper", true, [this]() { open_wallpaper_import_dialog(); }));
        wallpaper_rows.push_back(create_bound_switch_row("cc_row_slideshow_title", "cc_row_slideshow_subtitle", {"appearance", "daily_wallpaper"}, daily_wallpaper));
        wallpaper_rows.push_back(create_bound_combo_row(
            "cc_row_wallpaper_interval_title",
            "cc_row_wallpaper_interval_subtitle",
            translated_options({"cc_option_1_min", "cc_option_5_min", "cc_option_10_min", "cc_option_15_min", "cc_option_30_min", "cc_option_1_hour", "cc_option_3_hours", "cc_option_24_hours"}),
            {"1", "5", "10", "15", "30", "60", "180", "1440"},
            index_for_value({"1", "5", "10", "15", "30", "60", "180", "1440"}, wallpaper_interval),
            wallpaper_interval_combo_,
            {"appearance", "wallpaper_interval_minutes"}
        ));

        page->append(*create_card(
            "cc_card_appearance_wallpaper_title",
            "cc_card_appearance_wallpaper_subtitle",
            wallpaper_rows
        ));

        page->append(*create_card(
            "cc_card_appearance_typography_title",
            "cc_card_appearance_typography_subtitle",
            {
                create_bound_combo_row("cc_row_font_title", "cc_row_font_subtitle", font_labels_, font_values_, index_for_value(font_values_, font_family), font_combo_, {"appearance", "font_family"}),
                create_bound_scale_row("cc_row_font_size_title", "cc_row_font_size_subtitle", 8.0, 24.0, static_cast<double>(font_size), [](double current) { return Glib::ustring{std::to_string(static_cast<int>(current)) + " pt"}; }, font_scale_, font_value_label_, {"appearance", "font_size"}, [this](double current) { config_manager_.set_value({"appearance", "font_size"}, static_cast<int>(current)); }),
                create_bound_combo_row("cc_row_hinting_title", "cc_row_hinting_subtitle", translated_options({"cc_option_hinting_subpixel", "cc_option_hinting_grayscale", "cc_option_hinting_none"}), {"subpixel-slight", "grayscale-medium", "none"}, index_for_value({"subpixel-slight", "grayscale-medium", "none"}, font_hinting), hinting_combo_, {"appearance", "font_hinting"}),
                create_appearance_apply_row()
            }
        ));

        {
            const auto deco_button_size = static_cast<double>(config_int({"appearance", "deco_button_size"}, 28));
            const auto deco_button_spacing = static_cast<double>(config_int({"appearance", "deco_button_spacing"}, 6));
            const auto deco_shadow_size = static_cast<double>(config_int({"appearance", "deco_shadow_size"}, 24));
            const auto deco_titlebar_padding = static_cast<double>(config_int({"appearance", "deco_titlebar_padding"}, 10));
            const auto deco_animations = config_bool({"appearance", "deco_animations"}, true);

            page->append(*create_card(
                "Dekoracje okien",
                "Rozmiar przycisków, cienie i animacje okien",
                {
                    create_bound_scale_row("Rozmiar przycisków", "Wysokość i szerokość przycisków tytułu okna", 18.0, 40.0, deco_button_size, [](double v) { return Glib::ustring{std::to_string(static_cast<int>(v)) + " px"}; }, deco_button_size_scale_, deco_button_size_label_, {"appearance", "deco_button_size"}),
                    create_bound_scale_row("Odstęp przycisków", "Odległość między przyciskami tytułu", 0.0, 16.0, deco_button_spacing, [](double v) { return Glib::ustring{std::to_string(static_cast<int>(v)) + " px"}; }, deco_button_spacing_scale_, deco_button_spacing_label_, {"appearance", "deco_button_spacing"}),
                    create_bound_scale_row("Cień okna", "Rozmiar cienia aktywnego okna", 0.0, 60.0, deco_shadow_size, [](double v) { return Glib::ustring{std::to_string(static_cast<int>(v)) + " px"}; }, deco_shadow_scale_, deco_shadow_label_, {"appearance", "deco_shadow_size"}),
                    create_bound_scale_row("Wypełnienie tytułu", "Przestrzeń wokół przycisków i tekstu tytułu", 2.0, 20.0, deco_titlebar_padding, [](double v) { return Glib::ustring{std::to_string(static_cast<int>(v)) + " px"}; }, deco_titlebar_padding_scale_, deco_titlebar_padding_label_, {"appearance", "deco_titlebar_padding"}),
                    create_bound_switch_row("Animacje okien", "Efekt pojawiania i znikania okien", {"appearance", "deco_animations"}, deco_animations),
                    create_appearance_apply_row()
                }
            ));
        }

        return page;
    }

    Gtk::Box* build_display_page() {
        auto* page = create_page_shell("cc_page_display_kicker", "cc_page_display_summary");
        const auto [resolution_labels, resolution_values] = build_resolution_options();
        const auto selected_resolution = config_string({"display", "resolution"}, "1920x1080");
        const auto active_resolution_index = index_for_value(resolution_values, selected_resolution);

        const auto orientation_values = std::vector<std::string>{"landscape", "portrait-left", "portrait-right", "inverted"};
        const auto selected_orientation = config_string({"display", "orientation"}, "landscape");
        const auto active_orientation_index = index_for_value(orientation_values, selected_orientation);

        const auto scaling_values = std::vector<std::string>{"100", "125", "150", "175", "200"};
        const auto selected_scaling = config_string({"display", "scaling"}, "100");
        const auto active_scaling_index = index_for_value(scaling_values, selected_scaling);

        const auto vrr_enabled = config_bool({"display", "vrr"}, false);
        const auto fractional_enabled = config_bool({"display", "fractional_scaling"}, true);
        const auto night_light_enabled = config_bool({"display", "night_light"}, false);
        const auto night_light_temp = static_cast<double>(std::clamp(config_int({"display", "night_light_temperature"}, 4100), 2500, 6500));

        page->append(*create_card(
            "cc_card_display_layout_title",
            "cc_card_display_layout_subtitle",
            {
                create_action_row("cc_row_display_arrangement_title", "cc_row_display_arrangement_subtitle", "cc_action_display_arrangement", true, [this]() { open_display_arrangement(); }),
                create_display_layout_preview_row(),
                create_display_output_selector_row(),
                create_display_apply_controls_row(),
                create_bound_combo_row("cc_row_resolution_title", "cc_row_resolution_subtitle", resolution_labels, resolution_values, active_resolution_index, resolution_combo_, {"display", "resolution"}, [this](int) { update_refresh_for_selected_resolution(); }),
                create_bound_combo_row("cc_row_orientation_title", "cc_row_orientation_subtitle", translated_options({"cc_option_orientation_landscape", "cc_option_orientation_left", "cc_option_orientation_right", "cc_option_orientation_inverted"}), orientation_values, active_orientation_index, orientation_combo_, {"display", "orientation"}),
                create_bound_combo_row("cc_row_scaling_title", "cc_row_scaling_subtitle", {"100%", "125%", "150%", "175%", "200%"}, scaling_values, active_scaling_index, scaling_combo_, {"display", "scaling"})
            }
        ));

        page->append(*create_card(
            "cc_card_display_refresh_title",
            "cc_card_display_refresh_subtitle",
            {
                create_bound_combo_row("cc_row_refresh_title", "cc_row_refresh_subtitle", {"60 Hz"}, {"60"}, 0, refresh_combo_, {"display", "refresh_rate"}),
                create_bound_switch_row("cc_row_vrr_title", "cc_row_vrr_subtitle", {"display", "vrr"}, vrr_enabled, std::function<void(bool)>{}, &vrr_switch_),
                create_bound_switch_row("cc_row_fractional_scaling_title", "cc_row_fractional_scaling_subtitle", {"display", "fractional_scaling"}, fractional_enabled)
            }
        ));

        page->append(*create_card(
            "cc_card_display_presets_title",
            "cc_card_display_presets_subtitle",
            {
                create_display_presets_row()
            }
        ));

        page->append(*create_card(
            "cc_card_display_comfort_title",
            "cc_card_display_comfort_subtitle",
            {
                create_bound_switch_row("cc_row_night_light_title", "cc_row_night_light_subtitle", {"display", "night_light"}, night_light_enabled),
                create_bound_scale_row("cc_row_color_temperature_title", "cc_row_color_temperature_subtitle", 2500.0, 6500.0, night_light_temp, [](double current) { return Glib::ustring{std::to_string(static_cast<int>(current)) + " K"}; }, color_temperature_scale_, color_temperature_value_label_, {"display", "night_light_temperature"}),
                create_action_row("cc_row_icc_title", "cc_row_icc_subtitle", "cc_action_open_profiles", false, [this]() { open_color_profiles(); })
            }
        ));

        page->append(*create_card(
            "cc_card_display_drivers_title",
            "cc_card_display_drivers_subtitle",
            {
                create_gpu_drivers_manage_row(),
                create_action_row("cc_row_gpu_profile_title", "cc_row_gpu_profile_subtitle", "cc_action_manage_gpu_drivers", false, [this]() { open_gpu_driver_manager(); }),
                create_info_row(tr("cc_row_gpu_drivers_restart_title"), tr("cc_row_gpu_drivers_restart_subtitle"))
            }
        ));

        refresh_display_mode_combos();
        sync_display_controls_for_primary_output();

        return page;
    }

    Gtk::Box* build_input_page() {
        auto* page = create_page_shell("cc_page_input_kicker", "cc_page_input_summary");

        const auto pointer_speed = config_number({"input", "pointer_speed"}, 1.15);
        const auto acceleration = config_string({"input", "acceleration"}, "adaptive");
        const auto natural_scroll = config_bool({"input", "natural_scroll"}, true);
        const auto touchpad_gestures = config_bool({"input", "touchpad_gestures"}, true);
        const auto keyboard_layout = config_string({"input", "keyboard_layout"}, "pl-programmer");
        const auto repeat_delay = config_number({"input", "repeat_delay_ms"}, 320.0);
        const auto repeat_rate = config_number({"input", "repeat_rate"}, 28.0);
        const auto launcher_shortcut = config_string({"shortcuts", "launcher"}, "W-space");
        const auto screenshot_full_shortcut = config_string({"shortcuts", "screenshot_full"}, "Print");
        const auto screenshot_area_shortcut = config_string({"shortcuts", "screenshot_area"}, "S-Print");

        page->append(*create_card(
            "cc_card_input_pointer_title",
            "cc_card_input_pointer_subtitle",
            {
                create_bound_scale_row("cc_row_pointer_speed_title", "cc_row_pointer_speed_subtitle", 0.1, 2.0, pointer_speed, [](double current) { return Glib::ustring{format_decimal(current, 2) + "x"}; }, pointer_speed_scale_, pointer_speed_value_label_, {"input", "pointer_speed"}),
                create_bound_combo_row("cc_row_acceleration_title", "cc_row_acceleration_subtitle", translated_options({"cc_option_acceleration_adaptive", "cc_option_acceleration_flat", "cc_option_acceleration_disabled"}), {"adaptive", "flat", "disabled"}, index_for_value({"adaptive", "flat", "disabled"}, acceleration), acceleration_combo_, {"input", "acceleration"}),
                create_bound_switch_row("cc_row_natural_scroll_title", "cc_row_natural_scroll_subtitle", {"input", "natural_scroll"}, natural_scroll),
                create_bound_switch_row("cc_row_touchpad_gestures_title", "cc_row_touchpad_gestures_subtitle", {"input", "touchpad_gestures"}, touchpad_gestures)
            }
        ));

        page->append(*create_card(
            "cc_card_input_keyboard_title",
            "cc_card_input_keyboard_subtitle",
            {
                create_bound_combo_row("cc_row_keyboard_layout_title", "cc_row_keyboard_layout_subtitle", keyboard_layout_labels_, keyboard_layout_values_, index_for_value(keyboard_layout_values_, keyboard_layout), keyboard_layout_combo_, {"input", "keyboard_layout"}),
                create_keyboard_preview_widget(),
                create_action_row("cc_row_install_keyboard_title", "cc_row_install_keyboard_subtitle", "cc_action_install", false, [this]() { show_language_pack_dialog(); }),
                create_bound_scale_row("cc_row_repeat_delay_title", "cc_row_repeat_delay_subtitle", 150.0, 800.0, repeat_delay, [](double current) { return Glib::ustring{std::to_string(static_cast<int>(current)) + " ms"}; }, repeat_delay_scale_, repeat_delay_value_label_, {"input", "repeat_delay_ms"}),
                create_bound_scale_row("cc_row_repeat_rate_title", "cc_row_repeat_rate_subtitle", 10.0, 60.0, repeat_rate, [](double current) { return Glib::ustring{std::to_string(static_cast<int>(current)) + "/s"}; }, repeat_rate_scale_, repeat_rate_value_label_, {"input", "repeat_rate"}),
                create_bound_combo_row("cc_row_shortcut_launcher_title", "cc_row_shortcut_launcher_subtitle", {"Super + Space", "Super + Return", "Alt + Space"}, {"W-space", "W-Return", "A-space"}, index_for_value({"W-space", "W-Return", "A-space"}, launcher_shortcut), launcher_shortcut_combo_, {"shortcuts", "launcher"}),
                create_bound_combo_row("cc_row_shortcut_screenshot_full_title", "cc_row_shortcut_screenshot_full_subtitle", {"Print", "Super + Print", "Ctrl + Print"}, {"Print", "W-Print", "C-Print"}, index_for_value({"Print", "W-Print", "C-Print"}, screenshot_full_shortcut), screenshot_full_shortcut_combo_, {"shortcuts", "screenshot_full"}),
                create_bound_combo_row("cc_row_shortcut_screenshot_area_title", "cc_row_shortcut_screenshot_area_subtitle", {"Shift + Print", "Super + Shift + Print", "Ctrl + Shift + Print"}, {"S-Print", "W-S-Print", "C-S-Print"}, index_for_value({"S-Print", "W-S-Print", "C-S-Print"}, screenshot_area_shortcut), screenshot_area_shortcut_combo_, {"shortcuts", "screenshot_area"}),
                create_action_row("cc_row_shortcuts_title", "cc_row_shortcuts_subtitle", "cc_action_configure", false, [this]() { open_shortcuts_config(); })
            }
        ));

        page->append(*create_card(
            "cc_card_input_special_title",
            "cc_card_input_special_subtitle",
            {
                create_action_row("cc_row_tablet_title", "cc_row_tablet_subtitle", "cc_action_configure_tablet", false, [this]() { configure_tablet(); }),
                create_bound_scale_row("cc_row_pressure_title", "cc_row_pressure_subtitle", 0.0, 100.0, 62.0, [](double current) { return Glib::ustring{std::to_string(static_cast<int>(current)) + "%"}; }, pressure_scale_, pressure_value_label_, {"input", "tablet_pressure"}),
                create_action_row("cc_row_gamepad_title", "cc_row_gamepad_subtitle", "cc_action_run_test", false, [this]() { test_gamepad(); })
            }
        ));

        return page;
    }

    Gtk::Box* build_workspace_page() {
        auto* page = create_page_shell("cc_page_workspace_kicker", "cc_page_workspace_summary");

        const auto hot_corners_val = config_string({"workspace", "hot_corners"}, "off");
        const auto timeout_val = config_string({"workspace", "notifications_timeout"}, "5");

        // Card 1: Window manager
        page->append(*create_card(
            "cc_card_workspace_windows_title",
            "cc_card_workspace_windows_subtitle",
            {
                create_bound_switch_row("cc_row_snapping_title", "cc_row_snapping_subtitle", {"workspace", "window_snapping"}, config_bool({"workspace", "window_snapping"}, true)),
                create_bound_combo_row("cc_row_hot_corners_title", "cc_row_hot_corners_subtitle", translated_options({"cc_option_hot_corner_off", "cc_option_hot_corner_overview", "cc_option_hot_corner_launcher", "cc_option_hot_corner_notifications"}), {"off", "overview", "launcher", "notifications"}, index_for_value({"off", "overview", "launcher", "notifications"}, hot_corners_val), hot_corners_combo_, {"workspace", "hot_corners"}),
                create_bound_switch_row("cc_row_focus_follows_title", "cc_row_focus_follows_subtitle", {"workspace", "focus_follows_mouse"}, config_bool({"workspace", "focus_follows_mouse"}, false))
            }
        ));

        // Card 1b: Desktop right-click menu
        page->append(*create_card(
            "cc_card_desktop_menu_title",
            "cc_card_desktop_menu_subtitle",
            {
                create_bound_switch_row("cc_row_menu_refresh_title", "cc_row_menu_refresh_subtitle", {"workspace", "desktop_menu_refresh"}, config_bool({"workspace", "desktop_menu_refresh"}, true)),
                create_bound_switch_row("cc_row_menu_settings_title", "cc_row_menu_settings_subtitle", {"workspace", "desktop_menu_settings"}, config_bool({"workspace", "desktop_menu_settings"}, true)),
                create_bound_switch_row("cc_row_menu_terminal_title", "cc_row_menu_terminal_subtitle", {"workspace", "desktop_menu_terminal"}, config_bool({"workspace", "desktop_menu_terminal"}, true)),
                create_bound_switch_row("cc_row_menu_files_title", "cc_row_menu_files_subtitle", {"workspace", "desktop_menu_files"}, config_bool({"workspace", "desktop_menu_files"}, true))
            }
        ));

        // Card 2: Virtual desktops — dynamic workspace list
        {
            auto* desktops_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
            desktops_card->add_css_class("settings-card");

            auto* card_header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
            card_header->add_css_class("settings-card-header");
            card_header->append(*make_label(tr("cc_card_workspace_desktops_title"), "settings-card-title"));
            card_header->append(*make_label(tr("cc_card_workspace_desktops_subtitle"), "settings-card-subtitle", true));
            desktops_card->append(*card_header);

            workspace_list_box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
            desktops_card->append(*workspace_list_box_);

            rebuild_workspace_list_ui();

            auto* add_btn = Gtk::make_managed<Gtk::Button>(tr("cc_workspace_add"));
            add_btn->add_css_class("hyalo-button");
            add_btn->add_css_class("hyalo-button-secondary");
            add_btn->set_margin_start(12);
            add_btn->set_margin_end(12);
            add_btn->set_margin_top(8);
            add_btn->set_margin_bottom(12);
            add_btn->signal_clicked().connect([this]() { add_workspace(); });
            desktops_card->append(*add_btn);

            auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
            sep->add_css_class("settings-divider");
            desktops_card->append(*sep);

            desktops_card->append(*create_bound_switch_row("cc_row_workspace_animations_title", "cc_row_workspace_animations_subtitle", {"workspace", "workspace_animations"}, config_bool({"workspace", "workspace_animations"}, true)));

            page->append(*desktops_card);
        }

        // Card 3: Available applications (DnD source)
        page->append(*build_app_pool_card());

        // Card 4: Panel and notifications
        page->append(*create_card(
            "cc_card_workspace_panel_title",
            "cc_card_workspace_panel_subtitle",
            {
                create_bound_combo_row("cc_row_panel_position_title", "cc_row_panel_position_subtitle", translated_options({"cc_option_panel_top", "cc_option_panel_bottom", "cc_option_panel_left", "cc_option_panel_right"}), {"top", "bottom", "left", "right"}, index_for_value({"top", "bottom", "left", "right"}, panel_config_.position), panel_position_combo_, {"panel", "position"}, [this](int index) { panel_config_.position = std::array<std::string, 4>{"top", "bottom", "left", "right"}[static_cast<std::size_t>(index)]; }),
                create_bound_scale_row("cc_row_panel_height_title", "cc_row_panel_height_subtitle", 32.0, 64.0, static_cast<double>(panel_config_.height), [](double current) { return Glib::ustring{std::to_string(static_cast<int>(current)) + " px"}; }, panel_height_scale_, panel_height_value_label_, {"panel", "height"}, [this](double current) { panel_config_.height = static_cast<int>(current); }),
                create_bound_switch_row("cc_row_show_all_workspaces_title", "cc_row_show_all_workspaces_subtitle", {"panel", "show_all_workspaces"}, panel_config_.show_all_workspaces, [this](bool state) { panel_config_.show_all_workspaces = state; }),
                create_bound_switch_row("cc_row_panel_autohide_title", "cc_row_panel_autohide_subtitle", {"panel", "auto_hide"}, config_bool({"panel", "auto_hide"}, false)),
                create_bound_combo_row("cc_row_notifications_timeout_title", "cc_row_notifications_timeout_subtitle", translated_options({"cc_option_notify_3", "cc_option_notify_5", "cc_option_notify_8", "cc_option_notify_manual"}), {"3", "5", "8", "manual"}, index_for_value({"3", "5", "8", "manual"}, timeout_val), notifications_timeout_combo_, {"workspace", "notifications_timeout"}),
                create_bound_switch_row("cc_row_dnd_title", "cc_row_dnd_subtitle", {"workspace", "do_not_disturb"}, config_bool({"workspace", "do_not_disturb"}, false))
            }
        ));

        return page;
    }

    Gtk::Box* build_connectivity_page() {
        auto* page = create_page_shell("cc_page_connectivity_kicker", "cc_page_connectivity_summary");

        page->append(*create_card(
            "cc_card_connectivity_network_title",
            "cc_card_connectivity_network_subtitle",
            {
                create_action_row("cc_row_known_networks_title", "cc_row_known_networks_subtitle", "cc_action_open_networks", true, [this]() { open_network_manager(); }),
                create_action_row("cc_row_hidden_network_title", "cc_row_hidden_network_subtitle", "cc_action_add_network", false, [this]() { add_hidden_network(); }),
                create_bound_combo_row("cc_row_ethernet_title", "cc_row_ethernet_subtitle", translated_options({"cc_option_ethernet_dhcp", "cc_option_ethernet_dhcp_dns", "cc_option_ethernet_static"}), {"dhcp", "dhcp-custom-dns", "static"}, 0, ethernet_combo_, {"network", "ethernet_mode"})
            }
        ));

        page->append(*create_card(
            "cc_card_connectivity_bluetooth_title",
            "cc_card_connectivity_bluetooth_subtitle",
            {
                create_bound_switch_row("cc_row_bluetooth_title", "cc_row_bluetooth_subtitle", {"network", "bluetooth_enabled"}, true),
                create_action_row("cc_row_devices_title", "cc_row_devices_subtitle", "cc_action_manage_devices", false, [this]() { open_bluetooth_manager(); }),
                create_action_row("cc_row_file_transfer_title", "cc_row_file_transfer_subtitle", "cc_action_send_file", false, [this]() { send_bluetooth_file(); })
            }
        ));

        page->append(*create_card(
            "cc_card_connectivity_vpn_title",
            "cc_card_connectivity_vpn_subtitle",
            {
                create_action_row("cc_row_vpn_profiles_title", "cc_row_vpn_profiles_subtitle", "cc_action_configure_vpn", false, [this]() { open_network_manager(); }),
                create_bound_combo_row("cc_row_proxy_mode_title", "cc_row_proxy_mode_subtitle", translated_options({"cc_option_proxy_off", "cc_option_proxy_manual", "cc_option_proxy_auto"}), {"off", "manual", "auto"}, 0, proxy_mode_combo_, {"network", "proxy_mode"}),
                create_action_row("cc_row_proxy_settings_title", "cc_row_proxy_settings_subtitle", "cc_action_edit_proxy", false, [this]() { edit_proxy_settings(); })
            }
        ));

        return page;
    }

    Gtk::Box* build_system_page() {
        auto* page = create_page_shell("cc_page_system_kicker", "cc_page_system_summary");

        const auto detected_sinks = detect_audio_sinks();
        const auto detected_sources = detect_audio_sources();
        const auto output_volume = detect_audio_volume("@DEFAULT_AUDIO_SINK@");
        const auto input_volume = detect_audio_volume("@DEFAULT_AUDIO_SOURCE@");

        std::vector<std::string> sink_labels, sink_values;
        for (const auto& s : detected_sinks) {
            sink_labels.push_back(s.description);
            sink_values.push_back(s.name);
        }
        if (sink_labels.empty()) {
            sink_labels.push_back(tr("cc_option_disabled"));
            sink_values.push_back("none");
        }

        std::vector<std::string> source_labels, source_values;
        for (const auto& s : detected_sources) {
            source_labels.push_back(s.description);
            source_values.push_back(s.name);
        }
        source_labels.push_back(tr("cc_option_disabled"));
        source_values.push_back("disabled");

        const auto audio_output = config_string({"audio", "output"}, sink_values.front());
        const auto audio_input = config_string({"audio", "input"}, source_values.front());
        const auto audio_echo_cancellation = config_bool({"audio", "echo_cancellation"}, true);
        const auto power_profile = config_string({"power", "profile"}, "balanced");
        const auto lid_action = config_string({"power", "lid_action"}, "suspend");
        const auto idle_suspend = config_string({"power", "idle_suspend"}, "15");

        page->append(*create_card(
            "cc_card_system_runtime_title",
            "cc_card_system_runtime_subtitle",
            {
                create_info_row(tr("cc_info_environment_title"), std::string{"HyaloOS "} + HYALO_DE_VERSION),
                create_info_row(tr("cc_info_session_type_title"), current_session_type()),
                create_info_row(tr("cc_info_desktop_title"), current_desktop_name()),
                create_info_row(tr("cc_info_kernel_title"), current_kernel_version()),
                create_info_row(tr("cc_info_host_title"), current_host_name()),
                create_info_row(tr("cc_info_config_path_title"), config_manager_.config_path().string())
            }
        ));

        page->append(*create_card("cc_card_system_versions_title", "cc_card_system_versions_subtitle", build_component_rows()));

        page->append(*create_card(
            "cc_card_system_audio_title",
            "cc_card_system_audio_subtitle",
            {
                create_bound_combo_row("cc_row_audio_output_title", "cc_row_audio_output_subtitle", sink_labels, sink_values, index_for_value(sink_values, audio_output), audio_output_combo_, {"audio", "output"}),
                create_bound_scale_row("cc_row_output_volume_title", "cc_row_output_volume_subtitle", 0.0, 1.5, output_volume, [](double v) { return Glib::ustring{std::to_string(static_cast<int>(v * 100.0)) + "%"}; }, output_volume_scale_, output_volume_value_label_, {"audio", "output_volume"}),
                create_bound_combo_row("cc_row_audio_input_title", "cc_row_audio_input_subtitle", source_labels, source_values, index_for_value(source_values, audio_input), audio_input_combo_, {"audio", "input"}),
                create_bound_scale_row("cc_row_input_volume_title", "cc_row_input_volume_subtitle", 0.0, 1.5, input_volume, [](double v) { return Glib::ustring{std::to_string(static_cast<int>(v * 100.0)) + "%"}; }, input_volume_scale_, input_volume_value_label_, {"audio", "input_volume"}),
                create_bound_switch_row("cc_row_echo_cancel_title", "cc_row_echo_cancel_subtitle", {"audio", "echo_cancellation"}, audio_echo_cancellation),
                create_action_row("cc_row_audio_mixer_title", "cc_row_audio_mixer_subtitle", "cc_action_open_mixer", true, [this]() { open_audio_mixer(); })
            }
        ));

        page->append(*create_card(
            "cc_card_system_power_title",
            "cc_card_system_power_subtitle",
            {
                create_bound_combo_row("cc_row_power_profile_title", "cc_row_power_profile_subtitle", translated_options({"cc_option_power_saver", "cc_option_power_balanced", "cc_option_power_performance"}), {"power-saver", "balanced", "performance"}, index_for_value({"power-saver", "balanced", "performance"}, power_profile), power_profile_combo_, {"power", "profile"}),
                create_bound_combo_row("cc_row_lid_action_title", "cc_row_lid_action_subtitle", translated_options({"cc_option_lid_suspend", "cc_option_lid_nothing", "cc_option_lid_hibernate", "cc_option_lid_screen_off"}), {"suspend", "nothing", "hibernate", "screen-off"}, index_for_value({"suspend", "nothing", "hibernate", "screen-off"}, lid_action), lid_action_combo_, {"power", "lid_action"}),
                create_bound_combo_row("cc_row_idle_suspend_title", "cc_row_idle_suspend_subtitle", translated_options({"cc_option_never", "cc_option_15_min", "cc_option_30_min", "cc_option_1_hour"}), {"never", "15", "30", "60"}, index_for_value({"never", "15", "30", "60"}, idle_suspend), idle_suspend_combo_, {"power", "idle_suspend"})
            }
        ));

        page->append(*create_card(
            "cc_card_system_storage_title",
            "cc_card_system_storage_subtitle",
            {
                create_action_row("cc_row_disk_usage_title", "cc_row_disk_usage_subtitle", "cc_action_open_analysis", false, [this]() { open_disk_usage(); }),
                create_action_row("cc_row_safe_remove_title", "cc_row_safe_remove_subtitle", "cc_action_manage_storage", false, [this]() { open_storage_manager(); }),
                create_action_row("cc_row_system_info_title", "cc_row_system_info_subtitle", "cc_action_show_specs", false, [this]() { show_system_specs(); })
            }
        ));

        page->append(*create_card(
            "cc_card_system_profile_title",
            "cc_card_system_profile_subtitle",
            {
                create_action_row("cc_row_export_profile_title", "cc_row_export_profile_subtitle", "cc_action_export", false, [this]() { export_profile(); }),
                create_action_row("cc_row_restore_defaults_title", "cc_row_restore_defaults_subtitle", "cc_action_reset", true, [this]() { reset_to_defaults(); })
            }
        ));

        return page;
    }

    Gtk::Box* build_privacy_page() {
        auto* page = create_page_shell("cc_page_privacy_kicker", "cc_page_privacy_summary");

        const auto privacy_camera = config_bool({"privacy", "camera"}, true);
        const auto privacy_microphone = config_bool({"privacy", "microphone"}, true);
        const auto privacy_location = config_bool({"privacy", "location"}, false);
        const auto privacy_screen_blank = config_string({"privacy", "screen_blank"}, "5");
        const auto privacy_password_on_resume = config_bool({"privacy", "password_on_resume"}, true);
        const auto privacy_firewall = config_bool({"privacy", "firewall"}, true);

        page->append(*create_card(
            "cc_card_privacy_accounts_title",
            "cc_card_privacy_accounts_subtitle",
            {
                create_action_row("cc_row_accounts_title", "cc_row_accounts_subtitle", "cc_action_connect_account", true, [this]() { open_online_accounts(); }),
                create_action_row("cc_row_sync_title", "cc_row_sync_subtitle", "cc_action_view_integrations", false, [this]() { open_online_accounts(); })
            }
        ));

        page->append(*create_card(
            "cc_card_privacy_permissions_title",
            "cc_card_privacy_permissions_subtitle",
            {
                create_bound_switch_row("cc_row_camera_title", "cc_row_camera_subtitle", {"privacy", "camera"}, privacy_camera),
                create_bound_switch_row("cc_row_microphone_title", "cc_row_microphone_subtitle", {"privacy", "microphone"}, privacy_microphone),
                create_bound_switch_row("cc_row_location_title", "cc_row_location_subtitle", {"privacy", "location"}, privacy_location),
                create_action_row("cc_row_permissions_title", "cc_row_permissions_subtitle", "cc_action_manage", false, [this]() { manage_app_permissions(); })
            }
        ));

        page->append(*create_card(
            "cc_card_privacy_lock_title",
            "cc_card_privacy_lock_subtitle",
            {
                create_bound_combo_row("cc_row_screen_blank_title", "cc_row_screen_blank_subtitle", translated_options({"cc_option_1_min", "cc_option_5_min", "cc_option_10_min", "cc_option_15_min", "cc_option_never"}), {"1", "5", "10", "15", "never"}, index_for_value({"1", "5", "10", "15", "never"}, privacy_screen_blank), screen_blank_combo_, {"privacy", "screen_blank"}),
                create_bound_switch_row("cc_row_password_resume_title", "cc_row_password_resume_subtitle", {"privacy", "password_on_resume"}, privacy_password_on_resume),
                create_bound_switch_row("cc_row_firewall_title", "cc_row_firewall_subtitle", {"privacy", "firewall"}, privacy_firewall),
                create_action_row("cc_row_firewall_rules_title", "cc_row_firewall_rules_subtitle", "cc_action_edit_rules", false, [this]() { open_firewall_rules(); })
            }
        ));

        return page;
    }

    Gtk::Box* build_users_page() {
        auto* page = create_page_shell("cc_page_users_kicker", "cc_page_users_summary");

        const auto autostart_notifications = config_bool({"autostart", "notifications"}, true);
        const auto autostart_output_profiles = config_bool({"autostart", "output_profiles"}, false);
        const auto autostart_xdg = config_bool({"autostart", "xdg"}, true);

        page->append(*create_card(
            "cc_card_users_accounts_title",
            "cc_card_users_accounts_subtitle",
            {
                create_action_row("cc_row_user_accounts_title", "cc_row_user_accounts_subtitle", "cc_action_manage_accounts", true, [this]() { open_user_accounts(); }),
                create_action_row("cc_row_password_title", "cc_row_password_subtitle", "cc_action_change_password", false, [this]() { change_user_password(); }),
                create_action_row("cc_row_admin_roles_title", "cc_row_admin_roles_subtitle", "cc_action_edit_roles", false, [this]() { show_user_roles(); })
            }
        ));

        page->append(*create_card(
            "cc_card_users_defaults_title",
            "cc_card_users_defaults_subtitle",
            {
                create_bound_combo_row("cc_row_browser_title", "cc_row_browser_subtitle", {"Firefox", "Chromium", "Brave"}, {"firefox", "chromium", "brave"}, 0, browser_combo_, {"apps", "default_browser"}),
                create_bound_combo_row("cc_row_mail_title", "cc_row_mail_subtitle", {"Thunderbird", "Geary", "Evolution"}, {"thunderbird", "geary", "evolution"}, 0, mail_combo_, {"apps", "default_mail"}),
                create_bound_combo_row("cc_row_video_title", "cc_row_video_subtitle", {"MPV", "VLC", "Celluloid"}, {"mpv", "vlc", "celluloid"}, 0, video_combo_, {"apps", "default_video"})
            }
        ));

        page->append(*create_card(
            "cc_card_users_region_title",
            "cc_card_users_region_subtitle",
            {
                create_bound_switch_row("cc_row_autostart_notifications_title", "cc_row_autostart_notifications_subtitle", {"autostart", "notifications"}, autostart_notifications),
                create_bound_switch_row("cc_row_autostart_output_profiles_title", "cc_row_autostart_output_profiles_subtitle", {"autostart", "output_profiles"}, autostart_output_profiles),
                create_bound_switch_row("cc_row_autostart_xdg_title", "cc_row_autostart_xdg_subtitle", {"autostart", "xdg"}, autostart_xdg),
                create_action_row("cc_row_autostart_title", "cc_row_autostart_subtitle", "cc_action_manage_autostart", false, [this]() { open_autostart_directory(); }),
                create_bound_combo_row("cc_row_region_title", "cc_row_region_subtitle", region_locale_labels_, region_locale_values_, index_for_value(region_locale_values_, config_string({"region", "locale"}, "pl_PL")), region_combo_, {"region", "locale"}),
                create_bound_combo_row("cc_row_language_title", "cc_row_language_subtitle", language_labels_, language_values_, index_for_value(language_values_, language_code_), language_combo_, {"language"}, [this](int index) {
                    if (index >= 0 && static_cast<std::size_t>(index) < language_values_.size()) {
                        language_code_ = language_values_[static_cast<std::size_t>(index)];
                    }
                    set_status_caption(tr("cc_status_language_restart"));
                })
            }
        ));

        return page;
    }

    void refresh_runtime_status() {
        status_value_.set_text("Wayland + HyaloOS " HYALO_DE_VERSION);
        status_caption_.set_text(tr("cc_session_caption"));
    }

    void set_status_caption(const std::string& caption) {
        if (command_exists("notify-send")) {
            launch_command(
                "sh -lc 'notify-send "
                + shell_double_quote(tr("cc_notification_title"))
                + " "
                + shell_double_quote(caption)
                + " >/dev/null 2>&1'");
        }
    }

    bool launch_command(const std::string& command_line) {
        GError* error = nullptr;
        const auto launched = g_spawn_command_line_async(command_line.c_str(), &error);
        if (!launched) {
            if (error) {
                g_error_free(error);
            }
            return false;
        }

        return true;
    }

    bool command_exists(const std::string& command) const {
        gchar* path = g_find_program_in_path(command.c_str());
        if (!path) {
            return false;
        }

        g_free(path);
        return true;
    }

    std::filesystem::path local_wallpaper_daemon_path() const {
        return config_manager_.paths().defaults_root.parent_path() / "labwc" / "hyalo-wallpaperd";
    }

    std::filesystem::path local_driver_manager_path() const {
        return config_manager_.paths().defaults_root.parent_path() / "labwc" / "hyalo-driver-manager";
    }

    std::filesystem::path user_local_wallpaper_daemon_path() const {
        const char* home = std::getenv("HOME");
        if (!home || std::string_view{home}.empty()) {
            return {};
        }

        return std::filesystem::path(home) / ".local" / "bin" / "hyalo-wallpaperd";
    }

    std::filesystem::path user_local_driver_manager_path() const {
        const char* home = std::getenv("HOME");
        if (!home || std::string_view{home}.empty()) {
            return {};
        }

        return std::filesystem::path(home) / ".local" / "bin" / "hyalo-driver-manager";
    }

    std::string wallpaper_daemon_shell_command() const {
        auto candidates = std::vector<std::filesystem::path>{};

        const auto user_local_path = user_local_wallpaper_daemon_path();
        if (!user_local_path.empty()) {
            candidates.push_back(user_local_path);
        }

        const auto inferred_prefix_bin = config_manager_.paths().defaults_root.parent_path().parent_path().parent_path() / "bin" / "hyalo-wallpaperd";
        candidates.push_back(inferred_prefix_bin);
        candidates.emplace_back("/usr/local/bin/hyalo-wallpaperd");
        candidates.emplace_back("/usr/bin/hyalo-wallpaperd");

        std::error_code error;
        for (const auto& candidate : candidates) {
            error.clear();
            if (!candidate.empty() && std::filesystem::exists(candidate, error)) {
                return shell_double_quote(candidate.string());
            }
        }

        if (command_exists("hyalo-wallpaperd")) {
            return "hyalo-wallpaperd";
        }

        const auto local_path = local_wallpaper_daemon_path();
        error.clear();
        if (std::filesystem::exists(local_path, error) && command_exists("python3")) {
            return std::string{"python3 "} + shell_double_quote(local_path.string());
        }

        return {};
    }

    std::string driver_manager_shell_command() const {
        const auto user_local_path = user_local_driver_manager_path();
        std::error_code error;
        if (!user_local_path.empty() && std::filesystem::exists(user_local_path, error)) {
            return shell_double_quote(user_local_path.string());
        }

        if (command_exists("hyalo-driver-manager")) {
            return "hyalo-driver-manager";
        }

        const auto local_path = local_driver_manager_path();
        error.clear();
        if (std::filesystem::exists(local_path, error)) {
            return std::string{"bash "} + shell_double_quote(local_path.string());
        }

        return {};
    }

    void open_gpu_driver_manager() {
        const auto manager_command = driver_manager_shell_command();
        if (manager_command.empty()) {
            set_status_caption(tr("cc_status_gpu_driver_tool_missing"));
            return;
        }

        const auto job = manager_command + " --interactive";
        const auto escaped = shell_double_quote(job);
        const auto command =
            "sh -lc 'if command -v x-terminal-emulator >/dev/null 2>&1; then x-terminal-emulator -e sh -lc " + escaped
            + "; elif command -v gnome-terminal >/dev/null 2>&1; then gnome-terminal -- sh -lc " + escaped
            + "; elif command -v kitty >/dev/null 2>&1; then kitty sh -lc " + escaped
            + "; elif command -v foot >/dev/null 2>&1; then foot sh -lc " + escaped
            + "; elif command -v alacritty >/dev/null 2>&1; then alacritty -e sh -lc " + escaped
            + "; else sh -lc " + escaped + "; fi'";

        if (!launch_command(command)) {
            set_status_caption(tr("cc_status_gpu_driver_launch_failed"));
            return;
        }

        set_status_caption(tr("cc_status_gpu_driver_started"));
    }

    std::string current_package_manager() const {
        if (command_exists("pacman")) {
            return "pacman";
        }
        if (command_exists("apt")) {
            return "apt";
        }
        if (command_exists("dnf")) {
            return "dnf";
        }
        return {};
    }

    std::vector<std::string> list_installed_gpu_drivers() const {
        auto result = std::vector<std::string>{};
        const auto manager = current_package_manager();

        if (manager == "pacman") {
            const auto output = read_command_output(
                "sh -lc 'pacman -Qq 2>/dev/null | grep -E "
                "\"^(nvidia|nvidia-dkms|nvidia-open|nvidia-utils|lib32-nvidia-utils|mesa|lib32-mesa|vulkan-radeon|vulkan-intel|xf86-video-amdgpu|xf86-video-intel|intel-media-driver|libva-mesa-driver)$\" "
                "| sort -u'"
            );
            result = split_nonempty_lines(output);
        } else if (manager == "apt") {
            const auto output = read_command_output(
                "sh -lc 'dpkg-query -W -f="
                "\"${binary:Package}\\n\" "
                "2>/dev/null | grep -E "
                "\"^(nvidia-driver|nvidia-dkms|nvidia-utils|libnvidia|mesa|libgl1-mesa-dri|mesa-vulkan-drivers|xserver-xorg-video-amdgpu|xserver-xorg-video-intel|intel-media-driver)\" "
                "| sort -u'"
            );
            result = split_nonempty_lines(output);
        } else if (manager == "dnf") {
            const auto output = read_command_output(
                "sh -lc 'rpm -qa 2>/dev/null | grep -Ei "
                "\"(nvidia|mesa|vulkan|amdgpu|intel-media-driver)\" "
                "| sort -u | head -n 40'"
            );
            result = split_nonempty_lines(output);
        }

        return result;
    }

    void refresh_installed_gpu_drivers() {
        if (gpu_installed_drivers_label_ == nullptr) {
            return;
        }

        const auto installed = list_installed_gpu_drivers();
        if (installed.empty()) {
            gpu_installed_drivers_label_->set_text(tr("cc_gpu_installed_none"));
            return;
        }

        auto text = tr("cc_gpu_installed_list_prefix");
        text += "\n";
        for (std::size_t index = 0; index < installed.size(); ++index) {
            text += "- " + installed[index];
            if (index + 1 < installed.size()) {
                text += "\n";
            }
        }
        gpu_installed_drivers_label_->set_text(text);
    }

    bool gpu_candidate_matches_vendor(const std::string& candidate) const {
        if (selected_gpu_vendor_filter_ == "all") {
            return true;
        }

        const auto lowered = lowercase(candidate);
        if (selected_gpu_vendor_filter_ == "nvidia") {
            return lowered.find("nvidia") != std::string::npos;
        }
        if (selected_gpu_vendor_filter_ == "amd") {
            return lowered.find("amdgpu") != std::string::npos
                || lowered.find("radeon") != std::string::npos;
        }
        if (selected_gpu_vendor_filter_ == "intel") {
            return lowered.find("intel") != std::string::npos;
        }
        return true;
    }

    std::string detect_preferred_gpu_vendor_filter() const {
        if (command_exists("lspci")) {
            const auto output = lowercase(read_command_output(
                "sh -lc 'lspci -nnk 2>/dev/null | grep -Ei "
                "\"(vga|3d|display)\"'"
            ));

            if (output.find("nvidia") != std::string::npos) {
                return "nvidia";
            }
            if (output.find("amd") != std::string::npos || output.find("advanced micro devices") != std::string::npos || output.find("radeon") != std::string::npos) {
                return "amd";
            }
            if (output.find("intel") != std::string::npos) {
                return "intel";
            }
        }

        const auto installed = list_installed_gpu_drivers();
        for (const auto& package : installed) {
            const auto lowered = lowercase(package);
            if (lowered.find("nvidia") != std::string::npos) {
                return "nvidia";
            }
            if (lowered.find("amdgpu") != std::string::npos || lowered.find("radeon") != std::string::npos) {
                return "amd";
            }
            if (lowered.find("intel") != std::string::npos) {
                return "intel";
            }
        }

        return "all";
    }

    std::filesystem::path gpu_install_status_path() const {
        if (const char* home = std::getenv("HOME")) {
            return std::filesystem::path(home) / ".cache" / "hyalo" / "gpu-driver-install.exit";
        }
        return std::filesystem::temp_directory_path() / "hyalo-gpu-driver-install.exit";
    }

    void watch_gpu_install_status() {
        if (gpu_install_status_watch_.connected()) {
            gpu_install_status_watch_.disconnect();
        }

        gpu_install_status_watch_ = Glib::signal_timeout().connect([this]() {
            if (!gpu_install_in_progress_) {
                return false;
            }

            const auto status_path = gpu_install_status_path();
            std::error_code error;
            if (!std::filesystem::exists(status_path, error) || error) {
                return true;
            }

            const auto raw = trim(read_text_file(status_path));
            int exit_code = -1;
            try {
                exit_code = std::stoi(raw);
            } catch (...) {
                exit_code = -1;
            }

            std::filesystem::remove(status_path, error);
            gpu_install_in_progress_ = false;

            if (exit_code == 0) {
                set_status_caption(tr("cc_status_gpu_install_success"));
                refresh_installed_gpu_drivers();
            } else {
                set_status_caption(tr("cc_status_gpu_install_failed"));
            }

            return false;
        }, 1000);
    }

    void scan_available_gpu_drivers() {
        gpu_driver_candidates_.clear();

        const auto manager = current_package_manager();
        if (manager.empty()) {
            set_status_caption(tr("cc_status_gpu_scan_tool_missing"));
            return;
        }

        auto candidates = std::vector<std::string>{};
        if (manager == "pacman") {
            const auto output = read_command_output(
                "sh -lc 'pacman -Ss --color never "
                "\"nvidia|mesa|vulkan|xf86-video|amdgpu|intel-media-driver|libva\" "
                "2>/dev/null | awk "
                "\"/^[^ ]+\\/[^ ]+/ {split($1,a,\"/\"); print a[2]}\" "
                "| sort -u | head -n 80'"
            );
            candidates = split_nonempty_lines(output);
        } else if (manager == "apt") {
            const auto output = read_command_output(
                "sh -lc 'apt-cache search --names-only "
                "\"nvidia-driver|mesa|vulkan|xserver-xorg-video|intel-media\" "
                "2>/dev/null | awk "
                "\"{print $1}\" "
                "| sort -u | head -n 80'"
            );
            candidates = split_nonempty_lines(output);
        } else if (manager == "dnf") {
            const auto output = read_command_output(
                "sh -lc 'dnf -q repoquery --qf "
                "\"%{name}\" "
                "\"*nvidia*\" \"*mesa*\" \"*vulkan*\" \"*amdgpu*\" \"*intel-media*\" "
                "2>/dev/null | sort -u | head -n 80'"
            );
            candidates = split_nonempty_lines(output);
        }

        auto filtered = std::vector<std::string>{};
        auto seen = std::unordered_set<std::string>{};
        for (const auto& candidate : candidates) {
            const auto lowered = lowercase(candidate);
            const auto relevant = lowered.find("nvidia") != std::string::npos
                || lowered.find("mesa") != std::string::npos
                || lowered.find("vulkan") != std::string::npos
                || lowered.find("xf86-video") != std::string::npos
                || lowered.find("amdgpu") != std::string::npos
                || lowered.find("intel") != std::string::npos
                || lowered.find("libva") != std::string::npos;
            if (!relevant || !gpu_candidate_matches_vendor(candidate) || !seen.insert(candidate).second) {
                continue;
            }
            filtered.push_back(candidate);
            if (filtered.size() >= 40) {
                break;
            }
        }

        gpu_driver_candidates_ = filtered;
        if (gpu_driver_candidate_combo_ != nullptr) {
            gpu_driver_candidate_combo_->remove_all();
            if (gpu_driver_candidates_.empty()) {
                gpu_driver_candidate_combo_->append(tr("cc_gpu_scan_no_results"));
                gpu_driver_candidate_combo_->set_active(0);
            } else {
                for (const auto& package : gpu_driver_candidates_) {
                    gpu_driver_candidate_combo_->append(package);
                }
                gpu_driver_candidate_combo_->set_active(0);
            }
        }

        if (gpu_driver_candidates_.empty()) {
            set_status_caption(tr("cc_gpu_scan_no_results"));
        } else {
            set_status_caption(tr("cc_gpu_scan_done"));
        }
    }

    void install_selected_gpu_driver() {
        if (gpu_install_in_progress_) {
            set_status_caption(tr("cc_status_gpu_install_running"));
            return;
        }

        if (gpu_driver_candidate_combo_ == nullptr || gpu_driver_candidates_.empty()) {
            set_status_caption(tr("cc_gpu_scan_hint"));
            return;
        }

        const auto index = gpu_driver_candidate_combo_->get_active_row_number();
        if (index < 0 || static_cast<std::size_t>(index) >= gpu_driver_candidates_.size()) {
            set_status_caption(tr("cc_gpu_scan_hint"));
            return;
        }

        const auto package = gpu_driver_candidates_[static_cast<std::size_t>(index)];
        const auto manager = current_package_manager();
        auto install_job = std::string{};

        if (manager == "pacman") {
            install_job = "sudo pacman -S --needed " + shell_double_quote(package);
        } else if (manager == "apt") {
            install_job = "sudo apt install " + shell_double_quote(package);
        } else if (manager == "dnf") {
            install_job = "sudo dnf install " + shell_double_quote(package);
        } else {
            set_status_caption(tr("cc_status_gpu_scan_tool_missing"));
            return;
        }

        const auto status_path = gpu_install_status_path();
        std::error_code error;
        std::filesystem::create_directories(status_path.parent_path(), error);
        std::filesystem::remove(status_path, error);

        const auto monitored_install_job = install_job
            + "; rc=$?; mkdir -p " + shell_quote(status_path.parent_path().string())
            + "; echo $rc > " + shell_quote(status_path.string())
            + "; exit $rc";

        const auto escaped = shell_double_quote(monitored_install_job);
        const auto command =
            "sh -lc 'if command -v x-terminal-emulator >/dev/null 2>&1; then x-terminal-emulator -e sh -lc " + escaped
            + "; elif command -v gnome-terminal >/dev/null 2>&1; then gnome-terminal -- sh -lc " + escaped
            + "; elif command -v kitty >/dev/null 2>&1; then kitty sh -lc " + escaped
            + "; elif command -v foot >/dev/null 2>&1; then foot sh -lc " + escaped
            + "; elif command -v alacritty >/dev/null 2>&1; then alacritty -e sh -lc " + escaped
            + "; else sh -lc " + escaped + "; fi'";

        if (!launch_command(command)) {
            set_status_caption(tr("cc_status_gpu_driver_launch_failed"));
            return;
        }

        gpu_install_in_progress_ = true;
        watch_gpu_install_status();
        set_status_caption(tr("cc_status_gpu_install_started"));
    }

    void start_wallpaper_daemon() {
        const auto daemon_command = wallpaper_daemon_shell_command();
        if (daemon_command.empty()) {
            return;
        }

        launch_command("sh -lc 'HYALO_WALLPAPER_BACKEND=\"${HYALO_WALLPAPER_BACKEND:-auto}\" " + daemon_command + " --daemon >/dev/null 2>&1 &'");
    }

    void stop_wallpaper_daemon() {
        launch_command("sh -lc 'pkill -xu " + std::to_string(getuid()) + " -f " + shell_double_quote("hyalo-wallpaperd.*--daemon") + " >/dev/null 2>&1 || true'");
    }

    void reconcile_wallpaper_daemon() {
        const auto daily_wallpaper = config_bool({"appearance", "daily_wallpaper"}, config_bool({"appearance", "wallpaper_slideshow"}, false));
        if (daily_wallpaper) {
            start_wallpaper_daemon();
            return;
        }

        stop_wallpaper_daemon();
    }

    void apply_wallpaper_daemon_once() {
        const auto daemon_command = wallpaper_daemon_shell_command();
        if (daemon_command.empty()) {
            return;
        }

        launch_command("sh -lc 'HYALO_WALLPAPER_BACKEND=\"${HYALO_WALLPAPER_BACKEND:-auto}\" " + daemon_command + " --apply-now >/dev/null 2>&1'");
    }

    void open_display_arrangement() {
        display_outputs_preview_ = detect_display_outputs_preview();
        rebuild_display_layout_preview();
        refresh_display_output_selector();
        refresh_display_mode_combos();
        sync_display_controls_for_primary_output();

        if (display_outputs_preview_.empty()) {
            set_status_caption(tr("cc_status_monitors_not_detected"));
            return;
        }

        std::ostringstream summary;
        summary << tr("cc_status_monitors_detected") << ": " << display_outputs_preview_.size();

        auto names = std::string{};
        for (std::size_t index = 0; index < display_outputs_preview_.size(); ++index) {
            names += display_outputs_preview_[index].name;
            if (index + 1 < display_outputs_preview_.size()) {
                names += ", ";
            }
        }

        set_status_caption(summary.str() + " (" + names + ")");

        if (command_exists("notify-send")) {
            const auto command =
                "sh -lc 'notify-send "
                + shell_double_quote(tr("cc_action_display_arrangement"))
                + " "
                + shell_double_quote(summary.str() + "\\n" + names)
                + " >/dev/null 2>&1'";
            launch_command(command);
        }
    }

    void open_color_profiles() {
        if (command_exists("gcm-viewer")) {
            launch_command("sh -lc 'gcm-viewer >/dev/null 2>&1 &'");
            return;
        }

        if (command_exists("gnome-control-center")) {
            launch_command("sh -lc 'gnome-control-center color >/dev/null 2>&1 &'");
            return;
        }

        if (command_exists("displaycal")) {
            launch_command("sh -lc 'displaycal >/dev/null 2>&1 &'");
            return;
        }

        if (command_exists("colormgr")) {
            launch_command("sh -lc 'colormgr get-devices >/dev/null 2>&1' ");
            set_status_caption(tr("cc_status_saved"));
            return;
        }

        set_status_caption(tr("cc_status_display_tool_missing"));
    }

    bool parse_resolution_value(const std::string& value, int& width, int& height) const {
        const auto separator = value.find('x');
        if (separator == std::string::npos) {
            return false;
        }

        try {
            width = std::stoi(value.substr(0, separator));
            height = std::stoi(value.substr(separator + 1));
            return width > 0 && height > 0;
        } catch (...) {
            return false;
        }
    }

    std::string orientation_to_transform(const std::string& orientation) const {
        if (orientation == "portrait-left") {
            return "90";
        }
        if (orientation == "portrait-right") {
            return "270";
        }
        if (orientation == "inverted") {
            return "180";
        }
        return "normal";
    }

    std::string orientation_to_transform_fallback(const std::string& orientation) const {
        if (orientation == "portrait-left") {
            return "flipped-270";
        }
        if (orientation == "portrait-right") {
            return "flipped-90";
        }
        if (orientation == "inverted") {
            return "flipped-180";
        }
        return "normal";
    }

    void apply_display_runtime_settings() {
        if (!command_exists("wlr-randr")) {
            set_status_caption(tr("cc_status_display_tool_missing"));
            return;
        }

        auto outputs = detect_display_outputs_preview();
        if (outputs.empty()) {
            set_status_caption(tr("cc_status_display_layout_apply_failed"));
            return;
        }

        auto primary_output = config_string({"display", "primary_output"}, primary_output_name_);
        if (primary_output.empty()) {
            primary_output = primary_output_name_;
        }
        if (primary_output.empty()) {
            primary_output = outputs.front().name;
        }

        const auto global_resolution = config_string({"display", "resolution"}, "1920x1080");
        const auto global_refresh = config_string({"display", "refresh_rate"}, "60");
        const auto global_orientation = config_string({"display", "orientation"}, "landscape");
        const auto global_scaling = config_string({"display", "scaling"}, "100");
        const auto global_vrr_enabled = config_bool({"display", "vrr"}, false);
        const auto fractional_scaling = config_bool({"display", "fractional_scaling"}, true);

        const auto connected = detect_connected_outputs();

        std::size_t primary_index = 0;
        for (std::size_t index = 0; index < outputs.size(); ++index) {
            const auto& name = outputs[index].name;
            const auto resolved = resolve_output_in_list(name, connected, index);
            if (name == primary_output
                || resolved == primary_output
                || normalize_output_identifier(name) == normalize_output_identifier(primary_output)
                || normalize_output_identifier(resolved) == normalize_output_identifier(primary_output)) {
                primary_index = index;
                break;
            }
        }

        const auto primary_x = outputs[primary_index].x;
        const auto primary_y = outputs[primary_index].y;

        std::string diag_log;
        bool had_errors = false;

        const auto run_wlr_randr = [this, &diag_log](const std::string& args) {
            const auto command = "wlr-randr " + args + " 2>&1";
            const auto wrapped = "sh -lc " + shell_double_quote(command);
            std::string output;
            int exit_status = -1;
            {
                FILE* pipe = popen(wrapped.c_str(), "r");
                if (pipe) {
                    std::array<char, 512> buf{};
                    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
                        output += buf.data();
                    }
                    exit_status = pclose(pipe);
                }
            }
            const auto success = WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0;
            diag_log += "[" + std::string(success ? "OK" : "FAIL") + "] wlr-randr " + args;
            if (!success && !output.empty()) {
                diag_log += " -> " + output.substr(0, 120);
            }
            diag_log += "\n";
            return success;
        };

        for (std::size_t index = 0; index < outputs.size(); ++index) {
            const auto& output = outputs[index];
            const auto config_output_name = output.name;
            const auto runtime_output_name = resolve_output_in_list(config_output_name, connected, index);
            const auto alias_output_name = std::string{"Display-"} + std::to_string(static_cast<int>(index) + 1);

            diag_log += "--- Output: config=" + config_output_name + " runtime=" + runtime_output_name + " ---\n";

            const auto output_arg = "--output " + shell_double_quote(runtime_output_name);

            const auto read_output_string = [this, &config_output_name, &runtime_output_name, &alias_output_name](const std::string& key, const std::string& fallback) {
                for (const auto* candidate : {&config_output_name, &runtime_output_name, &alias_output_name}) {
                    if (candidate->empty()) {
                        continue;
                    }
                    if (const auto* value = find_config_value({"display", "outputs", *candidate, key}); value && value->is_string()) {
                        return value->get<std::string>();
                    }
                }
                return fallback;
            };

            const auto read_output_bool = [this, &config_output_name, &runtime_output_name, &alias_output_name](const std::string& key, bool fallback) {
                for (const auto* candidate : {&config_output_name, &runtime_output_name, &alias_output_name}) {
                    if (candidate->empty()) {
                        continue;
                    }
                    if (const auto* value = find_config_value({"display", "outputs", *candidate, key}); value && value->is_boolean()) {
                        return value->get<bool>();
                    }
                }
                return fallback;
            };

            const auto read_output_int = [this, &config_output_name, &runtime_output_name, &alias_output_name](const std::string& key, int fallback) {
                for (const auto* candidate : {&config_output_name, &runtime_output_name, &alias_output_name}) {
                    if (candidate->empty()) {
                        continue;
                    }
                    if (const auto* value = find_config_value({"display", "outputs", *candidate, key}); value && value->is_number_integer()) {
                        return value->get<int>();
                    }
                }
                return fallback;
            };

            const auto resolution_value = read_output_string("resolution", global_resolution);
            const auto refresh_value = read_output_string("refresh_rate", global_refresh);
            const auto orientation_value = read_output_string("orientation", global_orientation);
            const auto scaling_value = read_output_string("scaling", global_scaling);
            const auto vrr_enabled = read_output_bool("vrr", global_vrr_enabled);
            const auto default_x = output.x - primary_x;
            const auto default_y = output.y - primary_y;
            const auto position_x = read_output_int("x", default_x);
            const auto position_y = read_output_int("y", default_y);

            auto width = 0;
            auto height = 0;
            const auto has_resolution = parse_resolution_value(resolution_value, width, height);

            double scale = 1.0;
            try {
                scale = std::max(0.5, std::stod(scaling_value) / 100.0);
            } catch (...) {
                scale = 1.0;
            }

            if (!fractional_scaling) {
                scale = std::max(1.0, std::round(scale));
            }

            const auto transform = orientation_to_transform(orientation_value);
            const auto scale_str = format_decimal(scale, 6);
            const auto pos_str = std::to_string(position_x) + "," + std::to_string(position_y);

            auto mode_str = std::string{};
            if (has_resolution) {
                mode_str = std::to_string(width) + "x" + std::to_string(height);
                const auto normalized_refresh = normalize_refresh_value(refresh_value, "");
                if (!normalized_refresh.empty()) {
                    mode_str += "@" + normalized_refresh;
                }
            }

            auto combined_ok = false;
            if (has_resolution) {
                combined_ok = run_wlr_randr(
                    output_arg
                    + " --mode " + mode_str
                    + " --scale " + scale_str
                    + " --transform " + transform
                    + " --pos " + pos_str
                );
            }

            if (!combined_ok) {
                auto mode_applied = true;
                if (has_resolution) {
                    mode_applied = run_wlr_randr(output_arg + " --mode " + mode_str);

                    if (!mode_applied && mode_str.find('@') != std::string::npos) {
                        const auto mode_only = mode_str.substr(0, mode_str.find('@'));
                        mode_applied = run_wlr_randr(output_arg + " --mode " + mode_only);
                    }

                    if (!mode_applied) {
                        had_errors = true;
                    }
                }

                if (!run_wlr_randr(output_arg + " --scale " + scale_str)) {
                    had_errors = true;
                }

                if (!run_wlr_randr(output_arg + " --transform " + transform)) {
                    const auto fallback_transform = orientation_to_transform_fallback(orientation_value);
                    if (fallback_transform != transform) {
                        if (!run_wlr_randr(output_arg + " --transform " + fallback_transform)) {
                            had_errors = true;
                        }
                    } else {
                        had_errors = true;
                    }
                }

                if (!run_wlr_randr(output_arg + " --pos " + pos_str)) {
                    had_errors = true;
                }
            }

            run_wlr_randr(output_arg + " --adaptive-sync " + (vrr_enabled ? "enabled" : "disabled"));
        }

        {
            const auto log_dir = std::string{getenv("HOME") ? getenv("HOME") : "/tmp"} + "/.config/luminade";
            const auto log_path = log_dir + "/display-apply.log";
            if (auto* file = std::fopen(log_path.c_str(), "w")) {
                std::fputs(diag_log.c_str(), file);
                std::fclose(file);
            }
        }

        write_labwc_integration_files();

        display_outputs_preview_ = detect_display_outputs_preview();
        rebuild_display_layout_preview();
        refresh_display_output_selector();
        refresh_display_mode_combos();
        sync_display_controls_for_primary_output();
        if (had_errors) {
            set_status_caption(tr("cc_status_display_layout_apply_failed"));
        } else {
            set_status_caption(tr("cc_status_saved"));
        }
    }

    void open_network_manager() {
        if (command_exists("nm-connection-editor")) {
            launch_command("sh -lc 'nm-connection-editor >/dev/null 2>&1 &'");
            return;
        }

        set_status_caption(tr("cc_status_network_tool_missing"));
    }

    struct AudioEndpoint {
        std::string name;
        std::string description;
    };

    std::vector<AudioEndpoint> detect_audio_sinks() const {
        const auto json_output = read_command_output("pactl --format=json list sinks 2>/dev/null");
        if (json_output.empty()) {
            return {};
        }

        const auto parsed = nlohmann::json::parse(json_output, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_array()) {
            return {};
        }

        std::vector<AudioEndpoint> sinks;
        for (const auto& sink : parsed) {
            if (!sink.contains("name") || !sink.contains("description")) {
                continue;
            }
            sinks.push_back({sink["name"].get<std::string>(), sink["description"].get<std::string>()});
        }
        return sinks;
    }

    std::vector<AudioEndpoint> detect_audio_sources() const {
        const auto json_output = read_command_output("pactl --format=json list sources 2>/dev/null");
        if (json_output.empty()) {
            return {};
        }

        const auto parsed = nlohmann::json::parse(json_output, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_array()) {
            return {};
        }

        std::vector<AudioEndpoint> sources;
        for (const auto& source : parsed) {
            if (!source.contains("name") || !source.contains("description")) {
                continue;
            }
            const auto name = source["name"].get<std::string>();
            if (name.find(".monitor") != std::string::npos) {
                continue;
            }
            sources.push_back({name, source["description"].get<std::string>()});
        }
        return sources;
    }

    double detect_audio_volume(const std::string& target) const {
        const auto output = read_command_output("wpctl get-volume " + target + " 2>/dev/null");
        try {
            const auto pos = output.find("Volume: ");
            if (pos != std::string::npos) {
                return std::stod(output.substr(pos + 8));
            }
        } catch (...) {}
        return 0.5;
    }

    void open_audio_mixer() {
        if (command_exists("pavucontrol")) {
            launch_command("sh -lc 'pavucontrol >/dev/null 2>&1 &'");
            return;
        }

        set_status_caption(tr("cc_status_audio_tool_missing"));
    }

    void open_firewall_rules() {
        if (command_exists("gufw")) {
            launch_command("sh -lc 'gufw >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr(command_exists("ufw") ? "cc_status_firewall_cli_only" : "cc_status_firewall_tool_missing"));
    }

    // ── Connectivity handlers ──────────────────────────────────────────

    void add_hidden_network() {
        if (command_exists("nmtui")) {
            launch_command("sh -lc 'hyalo-terminal -e nmtui-connect >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("nm-connection-editor")) {
            launch_command("sh -lc 'nm-connection-editor >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_network_tool_missing"));
    }

    void open_bluetooth_manager() {
        if (command_exists("blueman-manager")) {
            launch_command("sh -lc 'blueman-manager >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("bluetoothctl")) {
            launch_command("sh -lc 'hyalo-terminal -e bluetoothctl >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_bluetooth_tool_missing"));
    }

    void send_bluetooth_file() {
        if (command_exists("blueman-sendto")) {
            launch_command("sh -lc 'blueman-sendto >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("bluetooth-sendto")) {
            launch_command("sh -lc 'bluetooth-sendto >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_bluetooth_tool_missing"));
    }

    void edit_proxy_settings() {
        if (command_exists("nm-connection-editor")) {
            launch_command("sh -lc 'nm-connection-editor >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_network_tool_missing"));
    }

    void apply_ethernet_mode() {
        const auto mode = config_string({"network", "ethernet_mode"}, "dhcp");
        if (!command_exists("nmcli")) return;
        if (mode == "dhcp") {
            launch_command("sh -lc 'nmcli con mod \"$(nmcli -t -f NAME,TYPE con show --active | grep ethernet | head -1 | cut -d: -f1)\" ipv4.method auto ipv4.dns \"\" >/dev/null 2>&1'");
        } else if (mode == "dhcp-custom-dns") {
            launch_command("sh -lc 'nmcli con mod \"$(nmcli -t -f NAME,TYPE con show --active | grep ethernet | head -1 | cut -d: -f1)\" ipv4.dns \"1.1.1.1 8.8.8.8\" >/dev/null 2>&1'");
        }
    }

    void apply_proxy_mode() {
        const auto mode = config_string({"network", "proxy_mode"}, "off");
        if (!command_exists("gsettings")) return;
        if (mode == "off") {
            launch_command("sh -lc 'gsettings set org.gnome.system.proxy mode none >/dev/null 2>&1'");
        } else if (mode == "manual") {
            launch_command("sh -lc 'gsettings set org.gnome.system.proxy mode manual >/dev/null 2>&1'");
        } else if (mode == "auto") {
            launch_command("sh -lc 'gsettings set org.gnome.system.proxy mode auto >/dev/null 2>&1'");
        }
    }

    // ── System / Storage handlers ─────────────────────────────────────

    void open_disk_usage() {
        if (command_exists("baobab")) {
            launch_command("sh -lc 'baobab >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("filelight")) {
            launch_command("sh -lc 'filelight >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("ncdu")) {
            launch_command("sh -lc 'hyalo-terminal -e ncdu >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_tool_missing"));
    }

    void open_storage_manager() {
        if (command_exists("gnome-disks")) {
            launch_command("sh -lc 'gnome-disks >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("gparted")) {
            launch_command("sh -lc 'gparted >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("lsblk")) {
            launch_command("sh -lc 'hyalo-terminal -e lsblk >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_tool_missing"));
    }

    void show_system_specs() {
        const auto cpu = read_command_output("lscpu 2>/dev/null | grep 'Model name' | head -1 | sed 's/Model name:[[:space:]]*//'");
        const auto mem_kb = read_command_output("grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2}'");
        const auto gpu = read_command_output("lspci 2>/dev/null | grep -i 'vga\\|3d\\|display' | head -1 | sed 's/.*: //'");
        const auto disk = read_command_output("df -h / 2>/dev/null | tail -1 | awk '{print $2 \" (\" $5 \" used)\"}'");
        const auto kernel = current_kernel_version();

        std::string mem_str;
        try {
            const auto mem_mb = std::stol(mem_kb) / 1024;
            mem_str = std::to_string(mem_mb) + " MB";
            if (mem_mb > 1024) {
                mem_str = format_decimal(static_cast<double>(mem_mb) / 1024.0, 1) + " GB";
            }
        } catch (...) {
            mem_str = mem_kb + " kB";
        }

        std::ostringstream specs;
        if (!cpu.empty()) specs << "CPU: " << cpu << "\n";
        if (!mem_str.empty()) specs << "RAM: " << mem_str << "\n";
        if (!gpu.empty()) specs << "GPU: " << gpu << "\n";
        if (!disk.empty()) specs << "Disk /: " << disk << "\n";
        if (!kernel.empty()) specs << "Kernel: " << kernel << "\n";

        set_status_caption(specs.str());
    }

    void apply_lid_action() {
        const auto action = config_string({"power", "lid_action"}, "suspend");
        if (!command_exists("loginctl")) return;
        // logind HandleLidSwitch via loginctl or direct config
        const auto logind_val = (action == "hibernate") ? "hibernate"
                              : (action == "nothing") ? "ignore"
                              : (action == "screen-off") ? "lock"
                              : "suspend";
        launch_command("sh -lc 'if [ -w /etc/systemd/logind.conf.d/ ] || sudo -n mkdir -p /etc/systemd/logind.conf.d/ 2>/dev/null; then "
            "echo -e \"[Login]\\nHandleLidSwitch=" + std::string(logind_val) + "\" | sudo -n tee /etc/systemd/logind.conf.d/hyalo-lid.conf >/dev/null 2>&1 && "
            "sudo -n systemctl restart systemd-logind.service >/dev/null 2>&1; fi'");
    }

    // ── Privacy handlers ──────────────────────────────────────────────

    void open_online_accounts() {
        if (command_exists("gnome-control-center")) {
            launch_command("sh -lc 'gnome-control-center online-accounts >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_tool_missing"));
    }

    void manage_app_permissions() {
        if (command_exists("flatpak")) {
            const auto info = read_command_output("flatpak permission-list 2>/dev/null | head -20");
            if (!info.empty()) {
                set_status_caption(info);
                return;
            }
        }
        if (command_exists("flatseal")) {
            launch_command("sh -lc 'flatseal >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_tool_missing"));
    }

    void apply_camera_toggle() {
        const auto privacy_json = config_manager_.raw().value("privacy", nlohmann::json::object());
        const auto enabled = privacy_json.value("camera", true);
        // PipeWire: suspend/resume video sources via wpctl
        if (command_exists("wpctl")) {
            const auto sources = read_command_output("wpctl status 2>/dev/null | sed -n '/Video/,/^$/p' | grep -oP '\\d+\\.' | tr -d '.'");
            for (const auto& id : split_nonempty_lines(sources)) {
                launch_command("sh -lc 'wpctl set-mute " + id + (enabled ? " 0" : " 1") + " >/dev/null 2>&1'");
            }
            return;
        }
    }

    void apply_microphone_toggle() {
        const auto privacy_json = config_manager_.raw().value("privacy", nlohmann::json::object());
        const auto enabled = privacy_json.value("microphone", true);
        if (command_exists("wpctl")) {
            launch_command(std::string{"sh -lc 'wpctl set-mute @DEFAULT_AUDIO_SOURCE@ "} + (enabled ? "0" : "1") + " >/dev/null 2>&1'");
            return;
        }
        if (command_exists("pactl")) {
            launch_command(std::string{"sh -lc 'pactl set-source-mute @DEFAULT_SOURCE@ "} + (enabled ? "0" : "1") + " >/dev/null 2>&1'");
        }
    }

    void apply_firewall_toggle() {
        const auto privacy_json = config_manager_.raw().value("privacy", nlohmann::json::object());
        const auto enabled = privacy_json.value("firewall", true);
        if (!command_exists("ufw")) {
            set_status_caption(tr("cc_status_firewall_tool_missing"));
            return;
        }
        launch_command(std::string{"sh -lc 'sudo -n ufw "} + (enabled ? "enable" : "disable") + " >/dev/null 2>&1'");
    }

    // ── Users handlers ────────────────────────────────────────────────

    void open_user_accounts() {
        if (command_exists("gnome-control-center")) {
            launch_command("sh -lc 'gnome-control-center user-accounts >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("users-admin")) {
            launch_command("sh -lc 'users-admin >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_tool_missing"));
    }

    void change_user_password() {
        launch_command("sh -lc 'hyalo-terminal -e passwd >/dev/null 2>&1 &'");
    }

    void show_user_roles() {
        const auto groups = read_command_output("groups 2>/dev/null");
        const auto whoami = read_command_output("whoami 2>/dev/null");
        set_status_caption(whoami + ": " + groups);
    }

    void apply_default_apps() {
        const auto browser = config_string({"apps", "default_browser"}, "firefox");
        const auto mail = config_string({"apps", "default_mail"}, "thunderbird");
        const auto video = config_string({"apps", "default_video"}, "mpv");

        if (command_exists("xdg-settings")) {
            launch_command("sh -lc 'xdg-settings set default-web-browser " + shell_double_quote(browser) + ".desktop >/dev/null 2>&1'");
        }
        if (command_exists("xdg-mime")) {
            launch_command("sh -lc 'xdg-mime default " + shell_double_quote(mail) + ".desktop x-scheme-handler/mailto >/dev/null 2>&1'");
            launch_command("sh -lc 'xdg-mime default " + shell_double_quote(video) + ".desktop video/mp4 video/x-matroska video/webm >/dev/null 2>&1'");
        }
    }

    // ── Input handlers ────────────────────────────────────────────────

    // Returns the 4-row key label set for a given layout code.
    struct KeyboardRow {
        std::vector<std::string> keys;
    };
    struct KeyboardMap {
        KeyboardRow row1; // number row
        KeyboardRow row2; // qwerty row
        KeyboardRow row3; // home row
        KeyboardRow row4; // bottom row
    };

    KeyboardMap get_keyboard_map(const std::string& layout_code) const {
        // Extract base layout (before ':' separator)
        auto base = layout_code;
        const auto sep = layout_code.find(':');
        if (sep != std::string::npos) base = layout_code.substr(0, sep);
        if (base == "pl-programmer") base = "pl";
        if (base == "us-intl") base = "us";

        // Default US QWERTY
        KeyboardMap us{
            {{"` ","1","2","3","4","5","6","7","8","9","0","-","="}},
            {{"Q","W","E","R","T","Y","U","I","O","P","[","]","\\"}},
            {{"A","S","D","F","G","H","J","K","L",";","'"}},
            {{"Z","X","C","V","B","N","M",",",".","/"}}
        };

        if (base == "us") return us;

        if (base == "pl") {
            auto m = us;
            // Polish special chars via AltGr shown as subscript hints
            m.row2.keys = {"Q","W","Ę","R","T","Y","U","I","Ó","P","[","]","\\"};
            m.row3.keys = {"Ą","Ś","D","F","G","H","J","K","Ł",";","'"};
            m.row4.keys = {"Ż","Ź","Ć","V","B","Ń","M",",",".","/"};
            return m;
        }
        if (base == "de") {
            return {
                {{"^","1","2","3","4","5","6","7","8","9","0","ß","´"}},
                {{"Q","W","E","R","T","Z","U","I","O","P","Ü","+","#"}},
                {{"A","S","D","F","G","H","J","K","L","Ö","Ä"}},
                {{"Y","X","C","V","B","N","M",",",".","-"}}
            };
        }
        if (base == "fr") {
            return {
                {{"²","&","É","\"","'","(","-","È","_","Ç","À",")","="}},
                {{"A","Z","E","R","T","Y","U","I","O","P","^","$","*"}},
                {{"Q","S","D","F","G","H","J","K","L","M","Ù"}},
                {{"W","X","C","V","B","N",",",";",":","!"}}
            };
        }
        if (base == "es") {
            return {
                {{"º","1","2","3","4","5","6","7","8","9","0","'","¡"}},
                {{"Q","W","E","R","T","Y","U","I","O","P","` ","+","Ç"}},
                {{"A","S","D","F","G","H","J","K","L","Ñ","´"}},
                {{"Z","X","C","V","B","N","M",",",".","-"}}
            };
        }
        if (base == "it") {
            return {
                {{"\\","1","2","3","4","5","6","7","8","9","0","'","Ì"}},
                {{"Q","W","E","R","T","Y","U","I","O","P","È","+","Ù"}},
                {{"A","S","D","F","G","H","J","K","L","Ò","À"}},
                {{"Z","X","C","V","B","N","M",",",".","-"}}
            };
        }
        if (base == "cz") {
            return {
                {{";","+","Ě","Š","Č","Ř","Ž","Ý","Á","Í","É","=","´"}},
                {{"Q","W","E","R","T","Z","U","I","O","P","Ú",")","¨"}},
                {{"A","S","D","F","G","H","J","K","L","Ů","§"}},
                {{"Y","X","C","V","B","N","M",",",".","-"}}
            };
        }
        if (base == "ru") {
            return {
                {{"Ё","1","2","3","4","5","6","7","8","9","0","-","="}},
                {{"Й","Ц","У","К","Е","Н","Г","Ш","Щ","З","Х","Ъ","\\"}},
                {{"Ф","Ы","В","А","П","Р","О","Л","Д","Ж","Э"}},
                {{"Я","Ч","С","М","И","Т","Ь","Б","Ю","."}}
            };
        }
        if (base == "ua") {
            return {
                {{"'","1","2","3","4","5","6","7","8","9","0","-","="}},
                {{"Й","Ц","У","К","Е","Н","Г","Ш","Щ","З","Х","Ї","Ґ"}},
                {{"Ф","І","В","А","П","Р","О","Л","Д","Ж","Є"}},
                {{"Я","Ч","С","М","И","Т","Ь","Б","Ю","/"}}
            };
        }
        if (base == "se" || base == "no" || base == "dk" || base == "fi") {
            // Scandinavian layout (minor per-country differences, this covers the common shape)
            return {
                {{"§","1","2","3","4","5","6","7","8","9","0","+","´"}},
                {{"Q","W","E","R","T","Y","U","I","O","P","Å","¨","'"}},
                {{"A","S","D","F","G","H","J","K","L","Ö","Ä"}},
                {{"Z","X","C","V","B","N","M",",",".","-"}}
            };
        }
        if (base == "hu") {
            return {
                {{"0","1","2","3","4","5","6","7","8","9","Ö","Ü","Ó"}},
                {{"Q","W","E","R","T","Z","U","I","O","P","Ő","Ú","Ű"}},
                {{"A","S","D","F","G","H","J","K","L","É","Á"}},
                {{"Y","X","C","V","B","N","M",",",".","-"}}
            };
        }
        if (base == "tr") {
            return {
                {{"\"" ,"1","2","3","4","5","6","7","8","9","0","*","-"}},
                {{"Q","W","E","R","T","Y","U","I","O","P","Ğ","Ü",","}},
                {{"A","S","D","F","G","H","J","K","L","Ş","İ"}},
                {{"Z","X","C","V","B","N","M","Ö","Ç","."}}
            };
        }
        if (base == "sk") {
            return {
                {{";","+","Ľ","Š","Č","Ť","Ž","Ý","Á","Í","É","=","´"}},
                {{"Q","W","E","R","T","Z","U","I","O","P","Ú","Ä","Ň"}},
                {{"A","S","D","F","G","H","J","K","L","Ô","§"}},
                {{"Y","X","C","V","B","N","M",",",".","-"}}
            };
        }
        if (base == "ro") {
            auto m = us;
            m.row3.keys = {"Ă","Ș","D","F","G","H","J","K","L",";","Â"};
            m.row4.keys = {"Z","X","C","V","B","N","M",",","Ț","Î"};
            return m;
        }
        if (base == "pt" || base == "br") {
            return {
                {{"\\","1","2","3","4","5","6","7","8","9","0","'","«"}},
                {{"Q","W","E","R","T","Y","U","I","O","P","+","´","~"}},
                {{"A","S","D","F","G","H","J","K","L","Ç","º"}},
                {{"Z","X","C","V","B","N","M",",",".","-"}}
            };
        }
        if (base == "jp") {
            auto m = us;
            m.row1.keys = {"半","1","2","3","4","5","6","7","8","9","0","-","^"};
            m.row2.keys = {"Q","W","E","R","T","Y","U","I","O","P","@","[","]"};
            return m;
        }
        if (base == "kr") {
            auto m = us;
            m.row2.keys = {"ㅂ","ㅈ","ㄷ","ㄱ","ㅅ","ㅛ","ㅕ","ㅑ","ㅐ","ㅔ","[","]","\\"};
            m.row3.keys = {"ㅁ","ㄴ","ㅇ","ㄹ","ㅎ","ㅗ","ㅓ","ㅏ","ㅣ",";","'"};
            m.row4.keys = {"ㅋ","ㅌ","ㅊ","ㅍ","ㅠ","ㅜ","ㅡ",",",".","/"};
            return m;
        }

        // Fallback for gb, nl, be, ch and unknown
        return us;
    }

    void draw_keyboard_preview(const Cairo::RefPtr<Cairo::Context>& cr, int width, int /*height*/) {
        const auto layout = config_string({"input", "keyboard_layout"}, "pl-programmer");
        const auto km = get_keyboard_map(layout);

        // Resolve colors from GTK context
        const auto sc = get_style_context();
        auto fg = Gdk::RGBA();
        fg.set_rgba(0.88, 0.88, 0.88, 1.0);
        auto bg = Gdk::RGBA();
        bg.set_rgba(0.16, 0.16, 0.18, 1.0);
        auto key_bg = Gdk::RGBA();
        key_bg.set_rgba(0.22, 0.22, 0.25, 1.0);
        auto accent = Gdk::RGBA();
        accent.set_rgba(0.55, 0.85, 0.70, 0.25);

        const double pad = 6.0;
        const double gap = 3.0;
        const double usable = width - 2.0 * pad;
        // 13 keys in number row as baseline for key size
        const double key_w = (usable - 12.0 * gap) / 13.0;
        const double key_h = key_w * 0.85;
        const double radius = 4.0;
        const double font_size = std::clamp(key_w * 0.42, 8.0, 16.0);

        auto draw_rounded_rect = [&](double x, double y, double w, double h, double r) {
            cr->begin_new_sub_path();
            cr->arc(x + w - r, y + r, r, -M_PI / 2.0, 0);
            cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2.0);
            cr->arc(x + r, y + h - r, r, M_PI / 2.0, M_PI);
            cr->arc(x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
            cr->close_path();
        };

        // Background
        cr->set_source_rgba(bg.get_red(), bg.get_green(), bg.get_blue(), 1.0);
        draw_rounded_rect(0, 0, width, pad * 2.0 + 4.0 * key_h + 3.0 * gap, 8.0);
        cr->fill();

        // US QWERTY reference to detect special chars
        const auto us_map = get_keyboard_map("us");
        const std::vector<const KeyboardRow*> rows = {&km.row1, &km.row2, &km.row3, &km.row4};
        const std::vector<const KeyboardRow*> us_rows = {&us_map.row1, &us_map.row2, &us_map.row3, &us_map.row4};

        cr->select_font_face("sans-serif", Cairo::ToyFontFace::Slant::NORMAL, Cairo::ToyFontFace::Weight::BOLD);
        cr->set_font_size(font_size);

        double y = pad;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const auto& row_keys = rows[r]->keys;
            const auto& us_keys = us_rows[r]->keys;
            const double row_width = row_keys.size() * key_w + (row_keys.size() - 1) * gap;
            double x = pad + (usable - row_width) / 2.0;

            for (std::size_t k = 0; k < row_keys.size(); ++k) {
                const auto& label = row_keys[k];
                const bool is_special = (k < us_keys.size() && label != us_keys[k]);

                // Key background
                if (is_special) {
                    cr->set_source_rgba(accent.get_red(), accent.get_green(), accent.get_blue(), accent.get_alpha());
                } else {
                    cr->set_source_rgba(key_bg.get_red(), key_bg.get_green(), key_bg.get_blue(), 1.0);
                }
                draw_rounded_rect(x, y, key_w, key_h, radius);
                cr->fill();

                // Key label
                cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), is_special ? 1.0 : 0.7);
                auto extents = Cairo::TextExtents{};
                cr->get_text_extents(label, extents);
                cr->move_to(
                    x + (key_w - extents.width) / 2.0 - extents.x_bearing,
                    y + (key_h + extents.height) / 2.0
                );
                cr->show_text(label);

                x += key_w + gap;
            }
            y += key_h + gap;
        }
    }

    void refresh_keyboard_preview() {
        if (keyboard_preview_) {
            keyboard_preview_->queue_draw();
        }
    }

    // ── Language pack install dialog ──────────────────────────────────

    struct LangPackEntry {
        std::string id;           // e.g. "hunspell-pl"
        std::string label_key;    // translation key
        std::string description;  // package category
        std::string pacman_pkg;
        std::string apt_pkg;
        std::string dnf_pkg;
    };

    std::vector<LangPackEntry> get_lang_packs_for_layout(const std::string& layout) const {
        auto base = layout;
        const auto sep = layout.find(':');
        if (sep != std::string::npos) base = layout.substr(0, sep);
        if (base == "pl-programmer") base = "pl";
        if (base == "us-intl") base = "us";

        // Map layout base → lang code for packages
        static const std::map<std::string, std::string> layout_lang = {
            {"pl","pl"}, {"de","de"}, {"fr","fr"}, {"es","es"}, {"it","it"},
            {"pt","pt"}, {"br","pt-br"}, {"cz","cs"}, {"sk","sk"}, {"hu","hu"},
            {"ro","ro"}, {"tr","tr"}, {"se","sv"}, {"no","nb"}, {"dk","da"},
            {"fi","fi"}, {"nl","nl"}, {"ru","ru"}, {"ua","uk"}, {"jp","ja"},
            {"kr","ko"}, {"gb","en-gb"}, {"us","en"}, {"be","fr"}, {"ch","de"}
        };

        auto it = layout_lang.find(base);
        const auto lang = (it != layout_lang.end()) ? it->second : base;

        // Short code for package names (before '-')
        auto short_code = lang;
        const auto dash = short_code.find('-');
        if (dash != std::string::npos) short_code = short_code.substr(0, dash);

        std::vector<LangPackEntry> packs;

        // 1. Keyboard layouts package
        packs.push_back({"xkb-layouts", "cc_langpack_keyboard_layouts", "XKB",
            "xkeyboard-config", "xkb-data", "xkeyboard-config"});

        // 2. Spell-check dictionary
        packs.push_back({"hunspell-" + short_code, "cc_langpack_spellcheck", "Hunspell",
            "hunspell-" + short_code, "hunspell-" + lang, "hunspell-" + short_code});

        // 3. Hyphenation
        packs.push_back({"hyphen-" + short_code, "cc_langpack_hyphenation", "Hyphen",
            "hyphen-" + short_code, "hyphen-" + lang, "hyphen-" + short_code});

        // 4. Locale data
        if (short_code != "en") {
            packs.push_back({"glibc-lang-" + short_code, "cc_langpack_locale", "glibc",
                "glibc", "locales-all", "glibc-langpack-" + short_code});
        }

        // 5. Fonts for CJK / Cyrillic
        if (short_code == "ja") {
            packs.push_back({"fonts-ja", "cc_langpack_fonts", "CJK",
                "noto-fonts-cjk", "fonts-noto-cjk", "google-noto-sans-cjk-ttc-fonts"});
        } else if (short_code == "ko") {
            packs.push_back({"fonts-ko", "cc_langpack_fonts", "CJK",
                "noto-fonts-cjk", "fonts-noto-cjk", "google-noto-sans-cjk-ttc-fonts"});
        }

        // 6. Input method for CJK
        if (short_code == "ja" || short_code == "ko" || short_code == "zh") {
            packs.push_back({"ibus-input", "cc_langpack_input_method", "IBus",
                "ibus", "ibus", "ibus"});
        }

        return packs;
    }

    void show_language_pack_dialog() {
        const auto layout = config_string({"input", "keyboard_layout"}, "pl-programmer");
        const auto packs = get_lang_packs_for_layout(layout);

        Gtk::Window dialog;
        dialog.set_modal(true);
        dialog.set_hide_on_close(true);
        dialog.set_decorated(false);
        dialog.set_resizable(false);
        dialog.set_default_size(520, -1);
        dialog.set_title(tr("cc_langpack_dialog_title"));
        dialog.set_transient_for(*this);
        dialog.add_css_class("update-auth-modal");

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        content->add_css_class("update-auth-modal-surface");
        dialog.set_child(*content);

        // Title
        auto* title = Gtk::make_managed<Gtk::Label>(tr("cc_langpack_dialog_title"));
        title->set_xalign(0.0f);
        title->add_css_class("update-auth-modal-label");
        title->set_markup("<b>" + tr("cc_langpack_dialog_title") + "</b>");
        content->append(*title);

        // Subtitle
        auto* subtitle = Gtk::make_managed<Gtk::Label>(tr("cc_langpack_dialog_subtitle"));
        subtitle->set_xalign(0.0f);
        subtitle->set_wrap(true);
        subtitle->add_css_class("update-auth-modal-subtitle");
        content->append(*subtitle);

        // Spacer
        auto* spacer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        spacer->set_margin_top(6);
        content->append(*spacer);

        // Detect which packages are already installed
        const bool has_pacman = command_exists("pacman");
        const bool has_apt = command_exists("apt");
        const bool has_dnf = command_exists("dnf");

        struct PackRow {
            Gtk::CheckButton* check = nullptr;
            std::string install_cmd;
            bool already_installed = false;
        };
        std::vector<PackRow> pack_rows;

        for (const auto& pack : packs) {
            std::string pkg;
            std::string check_cmd;
            std::string install_base;
            if (has_pacman) {
                pkg = pack.pacman_pkg;
                check_cmd = "pacman -Qi " + pkg + " >/dev/null 2>&1";
                install_base = "sudo pacman -S --needed --noconfirm " + pkg;
            } else if (has_apt) {
                pkg = pack.apt_pkg;
                check_cmd = "dpkg -s " + pkg + " >/dev/null 2>&1";
                install_base = "sudo apt install -y " + pkg;
            } else if (has_dnf) {
                pkg = pack.dnf_pkg;
                check_cmd = "rpm -q " + pkg + " >/dev/null 2>&1";
                install_base = "sudo dnf install -y " + pkg;
            } else {
                continue;
            }

            const bool installed = (std::system(check_cmd.c_str()) == 0);

            auto* row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            row_box->set_margin_top(3);
            row_box->set_margin_bottom(3);

            auto* check = Gtk::make_managed<Gtk::CheckButton>();
            check->set_active(!installed);
            if (installed) {
                check->set_sensitive(false);
            }

            auto* text_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
            text_box->set_hexpand(true);

            auto label_text = tr(pack.label_key);
            auto* lbl = Gtk::make_managed<Gtk::Label>(label_text);
            lbl->set_xalign(0.0f);
            lbl->add_css_class("settings-row-title");

            auto detail = pkg + (installed ? ("  ✓ " + tr("cc_langpack_installed")) : "");
            auto* detail_lbl = Gtk::make_managed<Gtk::Label>(detail);
            detail_lbl->set_xalign(0.0f);
            detail_lbl->add_css_class("settings-row-subtitle");

            text_box->append(*lbl);
            text_box->append(*detail_lbl);

            row_box->append(*check);
            row_box->append(*text_box);
            content->append(*row_box);

            pack_rows.push_back({check, install_base, installed});
        }

        // Password entry for sudo
        auto* pw_label = Gtk::make_managed<Gtk::Label>(tr("cc_langpack_password_label"));
        pw_label->set_xalign(0.0f);
        pw_label->add_css_class("update-auth-modal-label");
        pw_label->set_margin_top(8);
        content->append(*pw_label);

        auto* pw_entry = Gtk::make_managed<Gtk::Entry>();
        pw_entry->set_visibility(false);
        pw_entry->set_input_purpose(Gtk::InputPurpose::PASSWORD);
        pw_entry->set_placeholder_text(tr("cc_langpack_password_placeholder"));
        pw_entry->add_css_class("settings-entry");
        content->append(*pw_entry);

        // Spacer before buttons
        auto* spacer2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        spacer2->set_margin_top(8);
        content->append(*spacer2);

        auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        btn_box->set_homogeneous(true);
        btn_box->set_margin_top(8);

        auto* cancel_btn = Gtk::make_managed<Gtk::Button>(tr("cc_action_cancel"));
        cancel_btn->add_css_class("hyalo-button");
        cancel_btn->add_css_class("hyalo-button-secondary");
        cancel_btn->add_css_class("update-auth-modal-action");
        cancel_btn->add_css_class("update-auth-modal-cancel");

        auto* install_btn = Gtk::make_managed<Gtk::Button>(tr("cc_langpack_install_selected"));
        install_btn->add_css_class("hyalo-button");
        install_btn->add_css_class("hyalo-button-primary");
        install_btn->add_css_class("update-auth-modal-action");
        install_btn->add_css_class("update-auth-modal-primary");

        btn_box->append(*cancel_btn);
        btn_box->append(*install_btn);
        content->append(*btn_box);

        auto loop = Glib::MainLoop::create();
        bool do_install = false;

        cancel_btn->signal_clicked().connect([&]() {
            dialog.close();
        });

        install_btn->signal_clicked().connect([&]() {
            do_install = true;
            dialog.close();
        });

        dialog.signal_hide().connect([&]() {
            if (loop->is_running()) loop->quit();
        });

        dialog.show();
        loop->run();

        if (!do_install) return;

        const auto password = pw_entry->get_text();

        // Collect selected packages — replace sudo with sudo -S
        std::string combined_cmd;
        for (const auto& pr : pack_rows) {
            if (pr.already_installed) continue;
            if (!pr.check->get_active()) continue;
            auto cmd = pr.install_cmd;
            if (const auto pos = cmd.find("sudo "); pos != std::string::npos) {
                cmd.replace(pos, 5, "sudo -S ");
            }
            if (!combined_cmd.empty()) combined_cmd += " && ";
            combined_cmd += cmd;
        }

        if (combined_cmd.empty()) {
            set_status_caption(tr("cc_langpack_nothing_selected"));
            return;
        }

        set_status_caption(tr("cc_status_keyboard_package_installing"));

        // Run install in background thread, pipe password to sudo -S via stdin
        auto password_copy = std::string(password);
        auto cmd_copy = combined_cmd;
        auto done_msg = tr("cc_langpack_install_done");
        auto fail_msg = tr("cc_langpack_install_failed");
        auto auth_fail_msg = tr("cc_langpack_auth_failed");

        std::thread([this, pw = password_copy, cc = cmd_copy, done_msg, fail_msg, auth_fail_msg]() {
            FILE* pipe = popen(("sh -c '" + cc + "' 2>&1").c_str(), "w");
            if (!pipe) {
                Glib::signal_idle().connect_once([this, fail_msg]() {
                    set_status_caption(fail_msg);
                });
                return;
            }
            fwrite(pw.c_str(), 1, pw.size(), pipe);
            fwrite("\n", 1, 1, pipe);
            const auto status = pclose(pipe);

            Glib::signal_idle().connect_once([this, status, done_msg, fail_msg, auth_fail_msg]() {
                if (status == 0) {
                    set_status_caption(done_msg);
                } else if (WEXITSTATUS(status) == 1) {
                    set_status_caption(auth_fail_msg);
                } else {
                    set_status_caption(fail_msg);
                }
            });
        }).detach();
    }

    void configure_tablet() {
        if (command_exists("libinput")) {
            const auto tablets = read_command_output("libinput list-devices 2>/dev/null | grep -A2 'Tablet' | head -6");
            if (!tablets.empty()) {
                set_status_caption(tablets);
                return;
            }
        }
        set_status_caption(tr("cc_status_no_tablet"));
    }

    void test_gamepad() {
        const auto devices = read_command_output("ls /dev/input/js* 2>/dev/null");
        if (devices.empty()) {
            set_status_caption(tr("cc_status_no_gamepad"));
            return;
        }
        if (command_exists("jstest")) {
            launch_command("sh -lc 'hyalo-terminal -e jstest " + shell_double_quote(trim(split_nonempty_lines(devices).front())) + " >/dev/null 2>&1 &'");
            return;
        }
        if (command_exists("evtest")) {
            launch_command("sh -lc 'hyalo-terminal -e evtest >/dev/null 2>&1 &'");
            return;
        }
        set_status_caption(tr("cc_status_gamepad_detected") + ": " + devices);
    }

    void apply_audio_routing() {
        const auto output = config_string({"audio", "output"}, "");
        const auto input = config_string({"audio", "input"}, "");

        if (!output.empty() && output != "none") {
            launch_command("sh -lc 'pactl set-default-sink " + shell_double_quote(output) + " >/dev/null 2>&1'");
        }

        if (input == "disabled" || input.empty()) {
            launch_command("sh -lc 'if command -v wpctl >/dev/null 2>&1; then wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 1 >/dev/null 2>&1; elif command -v pactl >/dev/null 2>&1; then pactl set-source-mute @DEFAULT_SOURCE@ 1 >/dev/null 2>&1; fi'");
            return;
        }

        launch_command("sh -lc 'pactl set-default-source " + shell_double_quote(input) + " >/dev/null 2>&1 && "
            "if command -v wpctl >/dev/null 2>&1; then wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 0 >/dev/null 2>&1; fi'");
    }

    void apply_audio_output_volume() {
        const auto* vol_json = find_config_value({"audio", "output_volume"});
        double volume = 0.5;
        if (vol_json && vol_json->is_number()) {
            volume = vol_json->get<double>();
        }
        volume = std::clamp(volume, 0.0, 1.5);
        launch_command("sh -lc 'wpctl set-volume @DEFAULT_AUDIO_SINK@ " + format_decimal(volume, 2) + " >/dev/null 2>&1'");
    }

    void apply_audio_input_volume() {
        const auto* vol_json = find_config_value({"audio", "input_volume"});
        double volume = 0.5;
        if (vol_json && vol_json->is_number()) {
            volume = vol_json->get<double>();
        }
        volume = std::clamp(volume, 0.0, 1.5);
        launch_command("sh -lc 'wpctl set-volume @DEFAULT_AUDIO_SOURCE@ " + format_decimal(volume, 2) + " >/dev/null 2>&1'");
    }

    void apply_echo_cancellation() {
        const auto enabled = config_bool({"audio", "echo_cancellation"}, true);
        if (enabled) {
            launch_command("sh -lc 'pactl load-module module-echo-cancel 2>/dev/null || true'");
        } else {
            launch_command("sh -lc 'pactl unload-module module-echo-cancel 2>/dev/null || true'");
        }
    }

    void apply_idle_suspend() {
        const auto idle_suspend = config_string({"power", "idle_suspend"}, "15");
        if (idle_suspend == "never") {
            launch_command("sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-ac-timeout 0 >/dev/null 2>&1 && gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-battery-timeout 0 >/dev/null 2>&1'");
            return;
        }

        const auto seconds = std::max(0, std::stoi(idle_suspend) * 60);
        launch_command(
            "sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-ac-timeout "
            + std::to_string(seconds)
            + " >/dev/null 2>&1 && gsettings set org.gnome.settings-daemon.plugins.power sleep-inactive-battery-timeout "
            + std::to_string(seconds)
            + " >/dev/null 2>&1'"
        );
    }

    void apply_night_light_runtime() {
        const auto enabled = config_bool({"display", "night_light"}, false);
        const auto temperature = std::clamp(config_int({"display", "night_light_temperature"}, 4100), 2500, 6500);

        if (command_exists("gsettings")) {
            launch_command(
                std::string{"sh -lc 'gsettings set org.gnome.settings-daemon.plugins.color night-light-enabled "}
                + (enabled ? "true" : "false")
                + " >/dev/null 2>&1 && gsettings set org.gnome.settings-daemon.plugins.color night-light-temperature "
                + std::to_string(temperature)
                + " >/dev/null 2>&1'"
            );
        }

        if (enabled && command_exists("wlsunset")) {
            launch_command("sh -lc 'pkill -xu " + std::to_string(getuid()) + " -f " + shell_double_quote("wlsunset -t") + " >/dev/null 2>&1 || true'");
            launch_command(
                "sh -lc 'wlsunset -t " + std::to_string(temperature)
                + " -T 6500 >/dev/null 2>&1 &'"
            );
        } else {
            launch_command("sh -lc 'pkill -xu " + std::to_string(getuid()) + " -f " + shell_double_quote("wlsunset -t") + " >/dev/null 2>&1 || true'");
        }
    }

    std::filesystem::path resolve_xdg_config_home() const {
        if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
            return std::filesystem::path(xdg_config_home);
        }

        if (const char* home = std::getenv("HOME")) {
            return std::filesystem::path(home) / ".config";
        }

        return std::filesystem::current_path() / ".config";
    }

    std::filesystem::path resolve_labwc_config_dir() const {
        return resolve_xdg_config_home() / "labwc";
    }

    std::filesystem::path resolve_labwc_rc_path() const {
        return resolve_labwc_config_dir() / "rc.xml";
    }

    std::filesystem::path resolve_labwc_environment_path() const {
        return resolve_labwc_config_dir() / "environment";
    }

    std::filesystem::path resolve_labwc_autostart_path() const {
        return resolve_labwc_config_dir() / "autostart";
    }

    std::filesystem::path resolve_labwc_menu_path() const {
        return resolve_labwc_config_dir() / "menu.xml";
    }

    std::string read_text_file(const std::filesystem::path& path) const {
        std::ifstream input(path);
        if (!input.is_open()) {
            return {};
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    bool write_text_file(const std::filesystem::path& path, const std::string& content) const {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);

        std::ofstream output(path);
        if (!output.is_open()) {
            return false;
        }

        output << content;
        return output.good();
    }

    static std::string upsert_shell_managed_block(
        const std::string& existing,
        const std::string& begin_marker,
        const std::string& end_marker,
        const std::string& block_body
    ) {
        auto replacement = std::string{};
        replacement += begin_marker + "\n";
        replacement += block_body.empty() ? std::string{"# managed by Hyalo Settings\n"} : block_body;
        if (!replacement.empty() && replacement.back() != '\n') {
            replacement.push_back('\n');
        }
        replacement += end_marker + "\n";

        auto base = existing;
        if (!base.empty() && base.back() != '\n') {
            base.push_back('\n');
        }

        return replace_managed_block(base, begin_marker, end_marker, replacement);
    }

    static std::string upsert_xml_section_block(
        const std::string& existing,
        const std::string& section_name,
        const std::string& begin_marker,
        const std::string& end_marker,
        const std::string& block_body
    ) {
        auto replacement = std::string{};
        replacement += "    " + begin_marker + "\n";
        replacement += block_body;
        if (!replacement.empty() && replacement.back() != '\n') {
            replacement.push_back('\n');
        }
        replacement += "    " + end_marker + "\n";

        auto text = existing;
        if (text.empty()) {
            return "<labwc_config>\n"
                   "  <" + section_name + ">\n"
                   + replacement
                   + "  </" + section_name + ">\n"
                   + "</labwc_config>\n";
        }

        const auto section_tag = "<" + section_name;
        const auto section_begin = text.find(section_tag);
        if (section_begin == std::string::npos) {
            const auto root_end = text.rfind("</labwc_config>");
            if (root_end == std::string::npos) {
                return text + "\n<labwc_config>\n  <" + section_name + ">\n" + replacement + "  </" + section_name + ">\n</labwc_config>\n";
            }

            return text.substr(0, root_end)
                + "  <" + section_name + ">\n"
                + replacement
                + "  </" + section_name + ">\n"
                + text.substr(root_end);
        }

        const auto section_open_end = text.find('>', section_begin);
        const auto section_end = text.find("</" + section_name + '>', section_open_end);
        if (section_open_end == std::string::npos || section_end == std::string::npos) {
            return text;
        }

        const auto section_content = text.substr(section_open_end + 1, section_end - section_open_end - 1);
        std::string updated_section_content;
        if (section_content.find(begin_marker) != std::string::npos && section_content.find(end_marker) != std::string::npos) {
            updated_section_content = replace_managed_block(section_content, begin_marker, end_marker, replacement);
        } else {
            updated_section_content = section_content;
            if (!updated_section_content.empty() && updated_section_content.back() != '\n') {
                updated_section_content.push_back('\n');
            }
            updated_section_content += replacement;
        }

        return text.substr(0, section_open_end + 1) + updated_section_content + text.substr(section_end);
    }

    std::pair<std::string, std::string> keyboard_layout_env_values() const {
        const auto layout = config_string({"input", "keyboard_layout"}, "pl-programmer");
        if (layout == "pl-programmer") {
            // "Polski programisty" in XKB maps to base "pl" on most systems.
            return {"pl", ""};
        }
        if (layout == "us-intl") {
            return {"us", "intl"};
        }

        const auto separator = layout.find(':');
        if (separator != std::string::npos) {
            const auto base_layout = trim(layout.substr(0, separator));
            const auto variant = trim(layout.substr(separator + 1));
            if (!base_layout.empty()) {
                return {base_layout, variant};
            }
        }

        if (!layout.empty()) {
            return {layout, ""};
        }

        return {"us", ""};
    }

    std::string keyboard_layout_for_gsettings() const {
        const auto [layout, variant] = keyboard_layout_env_values();
        if (layout.empty()) {
            return "us";
        }
        if (variant.empty()) {
            return layout;
        }
        return layout + "+" + variant;
    }

    void apply_keyboard_layout_runtime() {
        if (!write_labwc_integration_files()) {
            return;
        }

        reconfigure_labwc();

        // Keep GNOME/IBus sources aligned to avoid toolkit-level override.
        if (!command_exists("gsettings")) {
            return;
        }

        const auto source = keyboard_layout_for_gsettings();
        const auto source_array = "[('xkb', '" + source + "')]";
        launch_command(
            "sh -lc 'gsettings set org.gnome.desktop.input-sources sources "
            + shell_double_quote(source_array)
            + " >/dev/null 2>&1 && gsettings set org.gnome.desktop.input-sources mru-sources "
            + shell_double_quote(source_array)
            + " >/dev/null 2>&1'"
        );
    }

    std::string build_labwc_environment_block() const {
        const auto [layout, variant] = keyboard_layout_env_values();

        std::ostringstream block;
        block << "XKB_DEFAULT_LAYOUT=" << layout << "\n";
        if (!variant.empty()) {
            block << "XKB_DEFAULT_VARIANT=" << variant << "\n";
        }
        return block.str();
    }

    std::string build_labwc_theme_block() const {
        const auto theme_name = config_string({"appearance", "window_theme"}, selected_window_theme_.empty() ? std::string{"HyaloOS"} : selected_window_theme_);
        const auto icon_pack = config_string({"appearance", "icon_pack"}, selected_icon_pack_.empty() ? std::string{"Adwaita"} : selected_icon_pack_);
        const auto font_family = config_string({"appearance", "font_family"}, "Sans");
        const auto font_size = config_int({"appearance", "font_size"}, 11);
        constexpr int ui_radius = 0;
        const auto blurred = config_bool({"appearance", "enable_blur"}, true);

        std::ostringstream block;
        block << "      <name>" << xml_escape(theme_name) << "</name>\n";
        block << "      <icon>" << xml_escape(icon_pack) << "</icon>\n";
        block << "      <fallbackAppIcon>hyalowm</fallbackAppIcon>\n";
        block << "      <cornerRadius>" << ui_radius << "</cornerRadius>\n";
        block << "      <dropShadows>" << (blurred ? "yes" : "no") << "</dropShadows>\n";
        block << "      <dropShadowsOnTiled>no</dropShadowsOnTiled>\n";
        block << "      <windowAnimationDuration>" << (config_bool({"appearance", "deco_animations"}, true) ? 200 : 0) << "</windowAnimationDuration>\n";
        block << "      <font place=\"ActiveWindow\">\n";
        block << "        <name>" << xml_escape(font_family) << "</name>\n";
        block << "        <size>" << std::max(1, font_size) << "</size>\n";
        block << "        <slant>normal</slant>\n";
        block << "        <weight>bold</weight>\n";
        block << "      </font>\n";
        block << "      <font place=\"InactiveWindow\">\n";
        block << "        <name>" << xml_escape(font_family) << "</name>\n";
        block << "        <size>" << std::max(1, font_size) << "</size>\n";
        block << "        <slant>normal</slant>\n";
        block << "        <weight>normal</weight>\n";
        block << "      </font>\n";
        return block.str();
    }

    std::string build_labwc_keyboard_block() const {
        const auto repeat_rate = config_int({"input", "repeat_rate"}, 28);
        const auto repeat_delay = config_int({"input", "repeat_delay_ms"}, 320);
        const auto launcher_shortcut = config_string({"shortcuts", "launcher"}, "W-space");
        const auto screenshot_full_shortcut = config_string({"shortcuts", "screenshot_full"}, "Print");
        const auto screenshot_area_shortcut = config_string({"shortcuts", "screenshot_area"}, "S-Print");

        std::ostringstream block;
        block << "      <layoutScope>global</layoutScope>\n";
        block << "      <repeatRate>" << std::clamp(repeat_rate, 1, 80) << "</repeatRate>\n";
        block << "      <repeatDelay>" << std::clamp(repeat_delay, 100, 1200) << "</repeatDelay>\n";
        block << "      <keybind key=\"" << xml_escape(launcher_shortcut) << "\">\n";
        block << "        <action name=\"Execute\" command=\"pkill -USR1 -x hyalo-panel\" />\n";
        block << "      </keybind>\n";
        block << "      <keybind key=\"" << xml_escape(screenshot_full_shortcut) << "\">\n";
        block << "        <action name=\"Execute\" command=\"hyalo-screenshot full\" />\n";
        block << "      </keybind>\n";
        block << "      <keybind key=\"" << xml_escape(screenshot_area_shortcut) << "\">\n";
        block << "        <action name=\"Execute\" command=\"hyalo-screenshot area\" />\n";
        block << "      </keybind>\n";
        return block.str();
    }

    std::string build_labwc_libinput_block() const {
        const auto natural_scroll = config_bool({"input", "natural_scroll"}, true);
        const auto pointer_speed = std::clamp(config_number({"input", "pointer_speed"}, 1.15) - 1.0, -1.0, 1.0);
        const auto acceleration = config_string({"input", "acceleration"}, "adaptive");
        const auto touchpad_gestures = config_bool({"input", "touchpad_gestures"}, true);
        const auto accel_profile = acceleration == "adaptive" ? "adaptive" : "flat";

        std::ostringstream block;
        block << "      <device category=\"default\">\n";
        block << "        <naturalScroll>" << (natural_scroll ? "yes" : "no") << "</naturalScroll>\n";
        block << "        <pointerSpeed>" << format_decimal(pointer_speed, 2) << "</pointerSpeed>\n";
        block << "        <accelProfile>" << accel_profile << "</accelProfile>\n";
        block << "        <threeFingerDrag>" << (touchpad_gestures ? "yes" : "no") << "</threeFingerDrag>\n";
        block << "      </device>\n";
        return block.str();
    }

    std::string build_labwc_focus_block() const {
        const auto follow_mouse = config_bool({"workspace", "focus_follows_mouse"}, false);
        std::ostringstream block;
        block << "      <followMouse>" << (follow_mouse ? "yes" : "no") << "</followMouse>\n";
        block << "      <followMouseRequiresMovement>yes</followMouseRequiresMovement>\n";
        block << "      <raiseOnFocus>no</raiseOnFocus>\n";
        return block.str();
    }

    std::string build_labwc_desktops_block() const {
        const auto labels = workspace_labels();
        const auto n = static_cast<int>(labels.size());
        std::ostringstream block;
        block << "      <number>" << n << "</number>\n";
        block << "      <names>\n";
        for (const auto& label : labels) {
            block << "        <name>" << label << "</name>\n";
        }
        block << "      </names>\n";
        return block.str();
    }

    std::string build_labwc_snapping_block() const {
        const auto snapping = config_bool({"workspace", "window_snapping"}, true);
        std::ostringstream block;
        if (snapping) {
            block << "      <range>\n";
            block << "        <inner>24</inner>\n";
            block << "        <outer>24</outer>\n";
            block << "      </range>\n";
            block << "      <cornerRange>56</cornerRange>\n";
            block << "      <overlay>\n";
            block << "        <enabled>yes</enabled>\n";
            block << "        <delay>\n";
            block << "          <inner>120</inner>\n";
            block << "          <outer>120</outer>\n";
            block << "        </delay>\n";
            block << "      </overlay>\n";
            block << "      <topMaximize>yes</topMaximize>\n";
            block << "      <notifyClient>edge</notifyClient>\n";
        } else {
            block << "      <range>\n";
            block << "        <inner>0</inner>\n";
            block << "        <outer>0</outer>\n";
            block << "      </range>\n";
            block << "      <cornerRange>0</cornerRange>\n";
            block << "      <topMaximize>no</topMaximize>\n";
        }
        return block.str();
    }

    void apply_mako_timeout() {
        const auto timeout_str = config_string({"workspace", "notifications_timeout"}, "5");
        std::string timeout_ms = "5000";
        if (timeout_str == "manual" || timeout_str == "0") {
            timeout_ms = "0";
        } else {
            try { timeout_ms = std::to_string(std::clamp(std::stoi(timeout_str), 1, 60) * 1000); } catch (...) {}
        }

        const auto mako_config_path = std::filesystem::path{g_get_user_config_dir()} / "mako" / "config";
        auto config_text = read_text_file(mako_config_path);
        if (config_text.empty()) {
            return;
        }

        const auto key = std::string{"default-timeout="};
        const auto pos = config_text.find(key);
        if (pos != std::string::npos) {
            const auto eol = config_text.find('\n', pos);
            config_text.replace(pos, (eol != std::string::npos ? eol : config_text.size()) - pos, key + timeout_ms);
        }

        write_text_file(mako_config_path, config_text);
        launch_command("sh -lc 'command -v makoctl >/dev/null 2>&1 && makoctl reload >/dev/null 2>&1'");
    }

    void restart_panel() {
        launch_command("sh -lc 'pkill -USR1 -x hyalo-panel >/dev/null 2>&1'");
    }

    // --- Workspace management helpers ---

    std::vector<std::string> workspace_labels() const {
        const auto ws_json = config_manager_.raw().value("workspaces", nlohmann::json::object());
        if (ws_json.contains("labels") && ws_json["labels"].is_array() && !ws_json["labels"].empty()) {
            std::vector<std::string> labels;
            for (const auto& entry : ws_json["labels"]) {
                if (entry.is_string()) {
                    labels.push_back(entry.get<std::string>());
                }
            }
            if (!labels.empty()) {
                return labels;
            }
        }
        return {"1", "2", "3", "4"};
    }

    nlohmann::json workspace_assignments() const {
        const auto ws_json = config_manager_.raw().value("workspaces", nlohmann::json::object());
        if (ws_json.contains("assignments") && ws_json["assignments"].is_object()) {
            return ws_json["assignments"];
        }
        return nlohmann::json::object();
    }

    void save_workspace_labels(const std::vector<std::string>& labels) {
        auto labels_json = nlohmann::json::array();
        for (const auto& l : labels) {
            labels_json.push_back(l);
        }
        config_manager_.set_value({"workspaces", "labels"}, labels_json);
        config_manager_.set_value({"workspace", "workspace_count"}, std::to_string(labels.size()));
    }

    void save_workspace_assignments(const nlohmann::json& assignments) {
        config_manager_.set_value({"workspaces", "assignments"}, assignments);
    }

    void apply_workspace_config() {
        persist_config("cc_status_saved", [this]() {
            write_labwc_integration_files();
            reconfigure_labwc();
        });
    }

    void assign_app_to_workspace(const std::string& app_id, const std::string& workspace_name) {
        auto assignments = workspace_assignments();
        assignments[app_id] = workspace_name;
        save_workspace_assignments(assignments);
        apply_workspace_config();
        rebuild_workspace_list_ui();
    }

    void unassign_app(const std::string& app_id) {
        auto assignments = workspace_assignments();
        assignments.erase(app_id);
        save_workspace_assignments(assignments);
        apply_workspace_config();
        rebuild_workspace_list_ui();
    }

    std::string find_app_display_name(const std::string& app_id) const {
        for (const auto& app : available_apps_) {
            if (app.app_id == app_id) {
                return app.name;
            }
        }
        return app_id;
    }

    Gtk::Widget* create_app_chip(const std::string& app_id) {
        auto* chip = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        chip->add_css_class("workspace-app-chip");
        chip->set_margin_top(2);
        chip->set_margin_bottom(2);

        auto* label = Gtk::make_managed<Gtk::Label>(find_app_display_name(app_id));
        label->add_css_class("workspace-chip-label");
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_max_width_chars(14);
        chip->append(*label);

        auto* remove_btn = Gtk::make_managed<Gtk::Button>();
        remove_btn->set_label("×");
        remove_btn->add_css_class("workspace-chip-remove");
        remove_btn->add_css_class("flat");
        auto captured_app_id = app_id;
        remove_btn->signal_clicked().connect([this, captured_app_id]() {
            unassign_app(captured_app_id);
        });
        chip->append(*remove_btn);

        return chip;
    }

    Gtk::Box* create_workspace_slot(int index, const std::string& name, const std::vector<std::string>& assigned_app_ids) {
        auto* slot = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        slot->add_css_class("workspace-slot");
        slot->set_margin_start(12);
        slot->set_margin_end(12);
        slot->set_margin_top(8);
        slot->set_margin_bottom(8);

        auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* number_label = Gtk::make_managed<Gtk::Label>(std::to_string(index + 1) + ".");
        number_label->add_css_class("workspace-slot-number");
        number_label->set_valign(Gtk::Align::CENTER);
        header->append(*number_label);

        auto* name_entry = Gtk::make_managed<Gtk::Entry>();
        name_entry->set_text(name);
        name_entry->set_hexpand(true);
        name_entry->add_css_class("workspace-name-entry");
        const auto captured_index = index;
        name_entry->signal_changed().connect([this, name_entry, captured_index]() {
            auto labels = workspace_labels();
            if (captured_index < static_cast<int>(labels.size())) {
                labels[static_cast<std::size_t>(captured_index)] = std::string{name_entry->get_text()};
                save_workspace_labels(labels);
                auto assignments = workspace_assignments();
                save_workspace_assignments(assignments);
                apply_workspace_config();
            }
        });
        header->append(*name_entry);

        auto labels = workspace_labels();
        if (static_cast<int>(labels.size()) > 1) {
            auto* delete_btn = Gtk::make_managed<Gtk::Button>();
            delete_btn->set_label("🗑");
            delete_btn->add_css_class("flat");
            delete_btn->add_css_class("workspace-delete-button");
            delete_btn->set_valign(Gtk::Align::CENTER);
            delete_btn->signal_clicked().connect([this, captured_index]() {
                remove_workspace(captured_index);
            });
            header->append(*delete_btn);
        }

        slot->append(*header);

        // Assigned apps row
        auto* chips_box = Gtk::make_managed<Gtk::FlowBox>();
        chips_box->set_selection_mode(Gtk::SelectionMode::NONE);
        chips_box->set_homogeneous(false);
        chips_box->set_min_children_per_line(1);
        chips_box->set_max_children_per_line(6);
        chips_box->add_css_class("workspace-chips-area");

        if (assigned_app_ids.empty()) {
            auto* hint = Gtk::make_managed<Gtk::Label>(tr("cc_workspace_drop_hint"));
            hint->add_css_class("workspace-drop-hint");
            hint->set_opacity(0.5);
            chips_box->append(*hint);
        } else {
            for (const auto& aid : assigned_app_ids) {
                chips_box->append(*create_app_chip(aid));
            }
        }

        slot->append(*chips_box);

        // DropTarget — accept app_id string drops
        auto drop_target = Gtk::DropTarget::create(G_TYPE_STRING, Gdk::DragAction::COPY);
        drop_target->signal_drop().connect(
            [this, captured_index](const Glib::ValueBase& value, double, double) -> bool {
                const auto& str_val = static_cast<const Glib::Value<Glib::ustring>&>(value);
                const auto app_id = std::string{str_val.get()};
                if (app_id.empty()) {
                    return false;
                }
                auto labels = workspace_labels();
                if (captured_index < 0 || captured_index >= static_cast<int>(labels.size())) {
                    return false;
                }
                assign_app_to_workspace(app_id, labels[static_cast<std::size_t>(captured_index)]);
                return true;
            },
            false
        );
        slot->add_controller(drop_target);

        return slot;
    }

    void rebuild_workspace_list_ui() {
        if (!workspace_list_box_) {
            return;
        }

        // Remove old children
        while (auto* child = workspace_list_box_->get_first_child()) {
            workspace_list_box_->remove(*child);
        }

        const auto labels = workspace_labels();
        const auto assignments = workspace_assignments();

        // Group assignments by workspace name
        std::map<std::string, std::vector<std::string>> ws_apps;
        for (auto it = assignments.begin(); it != assignments.end(); ++it) {
            if (it.value().is_string()) {
                ws_apps[it.value().get<std::string>()].push_back(it.key());
            }
        }

        for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
            if (i > 0) {
                auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
                sep->add_css_class("settings-divider");
                workspace_list_box_->append(*sep);
            }
            const auto& ws_name = labels[static_cast<std::size_t>(i)];
            auto apps_for_ws = ws_apps.count(ws_name) ? ws_apps.at(ws_name) : std::vector<std::string>{};
            workspace_list_box_->append(*create_workspace_slot(i, ws_name, apps_for_ws));
        }
    }

    void add_workspace() {
        auto labels = workspace_labels();
        if (labels.size() >= 8) {
            return;
        }
        labels.push_back(tr("cc_workspace_default_name") + " " + std::to_string(labels.size() + 1));
        save_workspace_labels(labels);
        apply_workspace_config();
        rebuild_workspace_list_ui();
    }

    void remove_workspace(int index) {
        auto labels = workspace_labels();
        if (index < 0 || index >= static_cast<int>(labels.size()) || labels.size() <= 1) {
            return;
        }

        const auto removed_name = labels[static_cast<std::size_t>(index)];
        labels.erase(labels.begin() + index);
        save_workspace_labels(labels);

        // Remove assignments pointing to the removed workspace
        auto assignments = workspace_assignments();
        std::vector<std::string> to_remove;
        for (auto it = assignments.begin(); it != assignments.end(); ++it) {
            if (it.value().is_string() && it.value().get<std::string>() == removed_name) {
                to_remove.push_back(it.key());
            }
        }
        for (const auto& key : to_remove) {
            assignments.erase(key);
        }
        save_workspace_assignments(assignments);
        apply_workspace_config();
        rebuild_workspace_list_ui();
    }

    Gtk::Widget* build_app_pool_card() {
        if (available_apps_.empty()) {
            available_apps_ = discover_desktop_apps();
        }

        auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        card->add_css_class("settings-card");

        auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        header->add_css_class("settings-card-header");
        header->append(*make_label(tr("cc_card_workspace_apps_title"), "settings-card-title"));
        header->append(*make_label(tr("cc_card_workspace_apps_subtitle"), "settings-card-subtitle", true));
        card->append(*header);

        // Search entry
        auto* search = Gtk::make_managed<Gtk::SearchEntry>();
        search->set_placeholder_text(tr("cc_workspace_search_apps"));
        search->set_margin_start(12);
        search->set_margin_end(12);
        search->set_margin_top(8);
        search->set_margin_bottom(8);
        card->append(*search);

        // FlowBox with app tiles
        auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_min_content_height(200);
        scroll->set_max_content_height(300);
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_margin_start(12);
        scroll->set_margin_end(12);
        scroll->set_margin_bottom(12);

        app_pool_flowbox_ = Gtk::make_managed<Gtk::FlowBox>();
        app_pool_flowbox_->set_selection_mode(Gtk::SelectionMode::NONE);
        app_pool_flowbox_->set_homogeneous(true);
        app_pool_flowbox_->set_min_children_per_line(2);
        app_pool_flowbox_->set_max_children_per_line(4);
        app_pool_flowbox_->set_row_spacing(4);
        app_pool_flowbox_->set_column_spacing(4);

        for (const auto& app : available_apps_) {
            auto* tile = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            tile->add_css_class("app-pool-tile");
            tile->set_margin_start(4);
            tile->set_margin_end(4);
            tile->set_margin_top(4);
            tile->set_margin_bottom(4);

            auto* icon = Gtk::make_managed<Gtk::Image>();
            icon->set_from_icon_name(app.icon_name);
            icon->set_pixel_size(24);
            tile->append(*icon);

            auto* name_label = Gtk::make_managed<Gtk::Label>(app.name);
            name_label->set_ellipsize(Pango::EllipsizeMode::END);
            name_label->set_max_width_chars(18);
            name_label->set_xalign(0.0f);
            tile->append(*name_label);

            // DragSource for this app
            auto drag = Gtk::DragSource::create();
            drag->set_actions(Gdk::DragAction::COPY);
            auto captured_app_id = app.app_id;
            auto captured_icon_name = app.icon_name;
            drag->signal_prepare().connect(
                [captured_app_id](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
                    Glib::Value<Glib::ustring> val;
                    val.init(Glib::Value<Glib::ustring>::value_type());
                    val.set(Glib::ustring{captured_app_id});
                    return Gdk::ContentProvider::create(val);
                },
                false
            );
            tile->add_controller(drag);

            app_pool_flowbox_->append(*tile);
        }

        // Search filtering
        app_pool_flowbox_->set_filter_func([this, search](Gtk::FlowBoxChild* child) -> bool {
            const auto query = lowercase(std::string{search->get_text()});
            if (query.empty()) {
                return true;
            }

            auto* box = dynamic_cast<Gtk::Box*>(child->get_child());
            if (!box) {
                return false;
            }

            auto* label_widget = box->get_last_child();
            auto* label = dynamic_cast<Gtk::Label*>(label_widget);
            if (!label) {
                return false;
            }

            return lowercase(std::string{label->get_text()}).find(query) != std::string::npos;
        });

        search->signal_search_changed().connect([this]() {
            if (app_pool_flowbox_) {
                app_pool_flowbox_->invalidate_filter();
            }
        });

        scroll->set_child(*app_pool_flowbox_);
        card->append(*scroll);

        return card;
    }

    std::string build_labwc_window_rules_block() const {
        const auto assignments = workspace_assignments();
        if (assignments.empty()) {
            return {};
        }

        std::ostringstream block;
        for (auto it = assignments.begin(); it != assignments.end(); ++it) {
            if (!it.value().is_string()) {
                continue;
            }
            const auto& app_id = it.key();
            const auto& ws_name = it.value().get<std::string>();
            block << "      <windowRule identifier=\"" << app_id << "\" event=\"onFirstMap\">\n";
            block << "        <action name=\"SendToDesktop\" to=\"" << ws_name << "\" follow=\"no\" />\n";
            block << "      </windowRule>\n";
        }
        return block.str();
    }

    // --- end workspace management ---

    std::string build_display_restore_commands() const {
        const auto* outputs_json = find_config_value({"display", "outputs"});
        if (!outputs_json || !outputs_json->is_object() || outputs_json->empty()) {
            return {};
        }

        const auto global_resolution = config_string({"display", "resolution"}, "1920x1080");
        const auto global_refresh = config_string({"display", "refresh_rate"}, "60");
        const auto global_orientation = config_string({"display", "orientation"}, "landscape");
        const auto global_scaling = config_string({"display", "scaling"}, "100");
        const auto global_vrr = config_bool({"display", "vrr"}, false);
        const auto fractional = config_bool({"display", "fractional_scaling"}, true);

        std::ostringstream commands;
        commands << "if command -v wlr-randr >/dev/null 2>&1; then\n";

        for (const auto& [name, obj] : outputs_json->items()) {
            if (!obj.is_object()) {
                continue;
            }

            const auto res = obj.value("resolution", global_resolution);
            const auto refresh = obj.value("refresh_rate", global_refresh);
            const auto orient = obj.value("orientation", global_orientation);
            const auto scale_str = obj.value("scaling", global_scaling);
            const auto vrr = obj.contains("vrr") ? obj["vrr"].get<bool>() : global_vrr;
            const auto pos_x = obj.value("x", 0);
            const auto pos_y = obj.value("y", 0);

            double scale = 1.0;
            try { scale = std::max(0.5, std::stod(scale_str) / 100.0); } catch (...) {}
            if (!fractional) { scale = std::max(1.0, std::round(scale)); }

            const auto transform = orientation_to_transform(orient);
            const auto normalized_refresh = normalize_refresh_value(refresh, "");

            auto mode = res;
            if (!normalized_refresh.empty()) {
                mode += "@" + normalized_refresh;
            }

            commands << "  wlr-randr --output " << shell_double_quote(name)
                     << " --mode " << mode
                     << " --scale " << format_decimal(scale, 6)
                     << " --transform " << transform
                     << " --pos " << pos_x << "," << pos_y
                     << " --adaptive-sync " << (vrr ? "enabled" : "disabled")
                     << " >/dev/null 2>&1\n";
        }

        commands << "fi\n";
        return commands.str();
    }

    std::string build_labwc_autostart_block() const {
        std::ostringstream block;

        block << build_display_restore_commands();

        if (config_bool({"autostart", "notifications"}, true)) {
            block << "command -v mako >/dev/null 2>&1 && ! pgrep -xu \"$(id -u)\" mako >/dev/null 2>&1 && mako >/dev/null 2>&1 &\n";
        }
        if (config_bool({"autostart", "output_profiles"}, false)) {
            block << "command -v kanshi >/dev/null 2>&1 && ! pgrep -xu \"$(id -u)\" kanshi >/dev/null 2>&1 && kanshi >/dev/null 2>&1 &\n";
        }
        if (config_bool({"autostart", "xdg"}, true)) {
            block << "command -v dex >/dev/null 2>&1 && dex -a -e HyaloOS >/dev/null 2>&1 &\n";
        }
        const auto daily_wallpaper = config_bool({"appearance", "daily_wallpaper"}, config_bool({"appearance", "wallpaper_slideshow"}, false));
        const auto daemon_command = wallpaper_daemon_shell_command();
        if (daily_wallpaper && !daemon_command.empty()) {
            block << "HYALO_WALLPAPER_BACKEND=\"${HYALO_WALLPAPER_BACKEND:-auto}\" "
                  << daemon_command
                  << " --daemon >/dev/null 2>&1 &\n";
        }

        return block.str();
    }

    std::string build_labwc_menu_xml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<openbox_menu>\n"
            << "  <menu id=\"root-menu\" label=\"\">\n";

        if (config_bool({"workspace", "desktop_menu_refresh"}, true)) {
            xml << "    <item label=\"" << tr("dm_refresh") << "\">\n"
                << "      <action name=\"Reconfigure\" />\n"
                << "    </item>\n";
        }
        if (config_bool({"workspace", "desktop_menu_settings"}, true)) {
            xml << "    <item label=\"" << tr("dm_settings") << "\">\n"
                << "      <action name=\"Execute\" command=\"hyalo-control-center\" />\n"
                << "    </item>\n";
        }
        if (config_bool({"workspace", "desktop_menu_terminal"}, true)) {
            xml << "    <item label=\"" << tr("dm_terminal") << "\">\n"
                << "      <action name=\"Execute\" command=\"hyalo-terminal\" />\n"
                << "    </item>\n";
        }
        if (config_bool({"workspace", "desktop_menu_files"}, true)) {
            xml << "    <item label=\"" << tr("dm_files") << "\">\n"
                << "      <action name=\"Execute\" command=\"xdg-open ~\" />\n"
                << "    </item>\n";
        }

        xml << "  </menu>\n"
            << "</openbox_menu>\n";
        return xml.str();
    }

    void write_labwc_themerc_override() {
        const auto override_path = resolve_labwc_config_dir() / "themerc-override";
        std::error_code ec;
        std::filesystem::create_directories(override_path.parent_path(), ec);

        const auto mode = effective_color_mode();
        const auto dark = mode == "dark";
        const auto theme = normalize_standard_theme(config_string({"appearance", "theme"}, selected_theme_.empty() ? std::string{"hyalo"} : selected_theme_));
        constexpr int radius = 0;

        // Derive decoration colors from the same palette as build_theme_tokens().
        struct DecoColors {
            std::string title_active;
            std::string title_inactive;
            std::string text_active;
            std::string text_inactive;
            std::string border_active;
            std::string border_inactive;
            std::string button_hover;
            std::string button_icon_active;
            std::string button_icon_inactive;
            std::string shadow_active;
            std::string shadow_inactive;
            std::string menu_bg;
            std::string menu_border;
            std::string menu_text;
            std::string menu_active_bg;
            std::string menu_active_text;
            std::string menu_separator;
            std::string menu_title_bg;
            std::string osd_bg;
            std::string osd_border;
            std::string osd_text;
            std::string switcher_active_border;
            std::string switcher_active_bg;
            std::string snap_bg;
            std::string snap_border;
        };

        auto colors = DecoColors{};

        if (theme == "graphite" || theme == "hyalo") {
            if (dark) {
                colors = {
                    "#171c25", "#141820", "#f3f6fb", "#909cad",
                    "#2a3440", "#232b36", "#2a3440",
                    "#f3f6fb", "#909cad",
                    "#05070c3a", "#05070c20",
                    "#171c25", "#2a3440", "#f3f6fb", "#2a3440", "#f3f6fb", "#2a3440", "#141820",
                    "#171c25", "#2a3440", "#f3f6fb",
                    "#384353", "#1d232d",
                    "#1d232d60", "#384353",
                };
            } else {
                colors = {
                    "#fbfcfe", "#eef2f6", "#1f2933", "#7a8795",
                    "#c8d0dc", "#dde3ea", "#e6ebf1",
                    "#1f2933", "#7a8795",
                    "#5c697822", "#5c697812",
                    "#fbfcfe", "#c8d0dc", "#1f2933", "#dce3eb", "#1f2933", "#dde3ea", "#eef2f6",
                    "#fbfcfe", "#c8d0dc", "#1f2933",
                    "#b0bcc8", "#dce3eb",
                    "#c8d0dc60", "#b0bcc8",
                };
            }
        } else if (theme == "glass") {
            if (dark) {
                colors = {
                    "#151d25", "#121921", "#eef7fb", "#90a4ae",
                    "#283843", "#23323d", "#30414d",
                    "#eef7fb", "#90a4ae",
                    "#07101840", "#07101828",
                    "#151d25", "#283843", "#eef7fb", "#30414d", "#eef7fb", "#283843", "#121921",
                    "#151d25", "#283843", "#eef7fb",
                    "#3a4d5b", "#1e2a33",
                    "#1e2a3360", "#3a4d5b",
                };
            } else {
                colors = {
                    "#ffffff", "#f4f6f9", "#13212b", "#76848d",
                    "#c0ccd5", "#d8e0e6", "#f6f7fa",
                    "#13212b", "#76848d",
                    "#6a768322", "#6a768312",
                    "#ffffff", "#c0ccd5", "#13212b", "#e8ebf0", "#13212b", "#d8e0e6", "#f4f6f9",
                    "#ffffff", "#c0ccd5", "#13212b",
                    "#a8b4bf", "#e8ebf0",
                    "#c0ccd560", "#a8b4bf",
                };
            }
        } else {
            // pastel
            if (dark) {
                colors = {
                    "#211f26", "#1c1a20", "#f4f0ee", "#9d9297",
                    "#36333d", "#312f39", "#403c47",
                    "#f4f0ee", "#9d9297",
                    "#09090d3a", "#09090d20",
                    "#211f26", "#36333d", "#f4f0ee", "#403c47", "#f4f0ee", "#36333d", "#1c1a20",
                    "#211f26", "#36333d", "#f4f0ee",
                    "#494550", "#292731",
                    "#29273160", "#494550",
                };
            } else {
                colors = {
                    "#fffaf7", "#f3ecea", "#24313a", "#7f8f94",
                    "#d6e4e0", "#e2ebe8", "#efe6eb",
                    "#24313a", "#7f8f94",
                    "#978d8922", "#978d8912",
                    "#fffaf7", "#d6e4e0", "#24313a", "#e2d8df", "#24313a", "#e2ebe8", "#f3ecea",
                    "#fffaf7", "#d6e4e0", "#24313a",
                    "#bfd2cc", "#e2d8df",
                    "#c6ddd260", "#bfd2cc",
                };
            }
        }

        std::ostringstream out;
        out << "# Generated by Hyalo Control Center — do not edit\n\n";

        const auto deco_button_size = config_int({"appearance", "deco_button_size"}, 28);
        const auto deco_button_spacing = config_int({"appearance", "deco_button_spacing"}, 6);
        const auto deco_shadow_size = config_int({"appearance", "deco_shadow_size"}, 24);
        const auto deco_titlebar_padding = config_int({"appearance", "deco_titlebar_padding"}, 10);
        const auto deco_inactive_shadow = std::max(1, deco_shadow_size * 60 / 100);
        const auto button_radius_val = deco_button_size / 2;

        out << "window.titlebar.padding.width: " << deco_titlebar_padding << "\n";
        out << "window.titlebar.padding.height: " << std::max(2, deco_titlebar_padding * 70 / 100) << "\n";
        out << "window.button.width: " << deco_button_size << "\n";
        out << "window.button.height: " << deco_button_size << "\n";
        out << "window.button.spacing: " << deco_button_spacing << "\n";
        out << "window.active.shadow.size: " << deco_shadow_size << "\n";
        out << "window.inactive.shadow.size: " << deco_inactive_shadow << "\n";

        out << "window.active.title.bg.color: " << colors.title_active << "\n";
        out << "window.inactive.title.bg.color: " << colors.title_inactive << "\n";
        out << "window.active.label.text.color: " << colors.text_active << "\n";
        out << "window.inactive.label.text.color: " << colors.text_inactive << "\n";
        out << "window.active.border.color: " << colors.border_active << "\n";
        out << "window.inactive.border.color: " << colors.border_inactive << "\n";
        out << "window.button.hover.bg.color: " << colors.button_hover << "\n";
        out << "window.button.hover.bg.corner-radius: " << button_radius_val << "\n";
        out << "window.active.button.unpressed.image.color: " << colors.button_icon_active << "\n";
        out << "window.inactive.button.unpressed.image.color: " << colors.button_icon_inactive << "\n";
        out << "window.active.shadow.color: " << colors.shadow_active << "\n";
        out << "window.inactive.shadow.color: " << colors.shadow_inactive << "\n";

        out << "menu.border.color: " << colors.menu_border << "\n";
        out << "menu.items.bg.color: " << colors.menu_bg << "\n";
        out << "menu.items.text.color: " << colors.menu_text << "\n";
        out << "menu.items.active.bg.color: " << colors.menu_active_bg << "\n";
        out << "menu.items.active.text.color: " << colors.menu_active_text << "\n";
        out << "menu.separator.color: " << colors.menu_separator << "\n";
        out << "menu.title.bg.color: " << colors.menu_title_bg << "\n";
        out << "menu.title.text.color: " << colors.menu_text << "\n";

        out << "osd.bg.color: " << colors.osd_bg << "\n";
        out << "osd.border.color: " << colors.osd_border << "\n";
        out << "osd.label.text.color: " << colors.osd_text << "\n";
        out << "osd.window-switcher.style-classic.item.active.border.color: " << colors.switcher_active_border << "\n";
        out << "osd.window-switcher.style-classic.item.active.bg.color: " << colors.switcher_active_bg << "\n";
        out << "osd.window-switcher.style-thumbnail.item.active.border.color: " << colors.switcher_active_border << "\n";
        out << "osd.window-switcher.style-thumbnail.item.active.bg.color: " << colors.switcher_active_bg << "\n";
        out << "osd.window-switcher.preview.border.color: " << colors.osd_border << "\n";

        out << "snapping.overlay.region.bg.color: " << colors.snap_bg << "\n";
        out << "snapping.overlay.edge.bg.color: " << colors.snap_bg << "\n";
        out << "snapping.overlay.region.border.color: " << colors.snap_border << "\n";
        out << "snapping.overlay.edge.border.color: " << colors.snap_border << "\n";

        write_text_file(override_path, out.str());
    }

    bool write_labwc_integration_files() {
        const auto environment_path = resolve_labwc_environment_path();
        const auto autostart_path = resolve_labwc_autostart_path();
        const auto rc_path = resolve_labwc_rc_path();

        const auto environment = upsert_shell_managed_block(
            read_text_file(environment_path),
            "# BEGIN HYALO CONTROL CENTER ENVIRONMENT",
            "# END HYALO CONTROL CENTER ENVIRONMENT",
            build_labwc_environment_block()
        );

        auto rc_text = read_text_file(rc_path);
        rc_text = upsert_xml_section_block(
            rc_text,
            "theme",
            "<!-- BEGIN HYALO CONTROL CENTER THEME -->",
            "<!-- END HYALO CONTROL CENTER THEME -->",
            build_labwc_theme_block()
        );
        rc_text = upsert_xml_section_block(
            rc_text,
            "keyboard",
            "<!-- BEGIN HYALO CONTROL CENTER KEYBOARD -->",
            "<!-- END HYALO CONTROL CENTER KEYBOARD -->",
            build_labwc_keyboard_block()
        );
        rc_text = upsert_xml_section_block(
            rc_text,
            "libinput",
            "<!-- BEGIN HYALO CONTROL CENTER LIBINPUT -->",
            "<!-- END HYALO CONTROL CENTER LIBINPUT -->",
            build_labwc_libinput_block()
        );
        rc_text = upsert_xml_section_block(
            rc_text,
            "focus",
            "<!-- BEGIN HYALO CONTROL CENTER FOCUS -->",
            "<!-- END HYALO CONTROL CENTER FOCUS -->",
            build_labwc_focus_block()
        );
        rc_text = upsert_xml_section_block(
            rc_text,
            "desktops",
            "<!-- BEGIN HYALO CONTROL CENTER DESKTOPS -->",
            "<!-- END HYALO CONTROL CENTER DESKTOPS -->",
            build_labwc_desktops_block()
        );
        rc_text = upsert_xml_section_block(
            rc_text,
            "snapping",
            "<!-- BEGIN HYALO CONTROL CENTER SNAPPING -->",
            "<!-- END HYALO CONTROL CENTER SNAPPING -->",
            build_labwc_snapping_block()
        );
        rc_text = upsert_xml_section_block(
            rc_text,
            "windowRules",
            "<!-- BEGIN HYALO CONTROL CENTER WINDOWRULES -->",
            "<!-- END HYALO CONTROL CENTER WINDOWRULES -->",
            build_labwc_window_rules_block()
        );

        const auto autostart = upsert_shell_managed_block(
            read_text_file(autostart_path),
            "# BEGIN HYALO CONTROL CENTER AUTOSTART",
            "# END HYALO CONTROL CENTER AUTOSTART",
            build_labwc_autostart_block()
        );

        return write_text_file(environment_path, environment)
            && write_text_file(rc_path, rc_text)
            && write_text_file(autostart_path, autostart)
            && write_text_file(resolve_labwc_menu_path(), build_labwc_menu_xml());
    }

    void reconfigure_labwc() {
        launch_command("sh -lc 'if command -v labwc >/dev/null 2>&1; then labwc --reconfigure >/dev/null 2>&1; elif [ -n \"$LABWC_PID\" ]; then kill -HUP \"$LABWC_PID\" >/dev/null 2>&1; fi'");
    }

    void apply_autostart_runtime() {
        if (config_bool({"autostart", "notifications"}, true)) {
            launch_command("sh -lc 'command -v mako >/dev/null 2>&1 && ! pgrep -xu \"$(id -u)\" mako >/dev/null 2>&1 && mako >/dev/null 2>&1 &'");
        } else {
            launch_command("sh -lc 'pkill -xu \"$(id -u)\" mako >/dev/null 2>&1 || true'");
        }

        if (config_bool({"autostart", "output_profiles"}, false)) {
            launch_command("sh -lc 'command -v kanshi >/dev/null 2>&1 && ! pgrep -xu \"$(id -u)\" kanshi >/dev/null 2>&1 && kanshi >/dev/null 2>&1 &'");
        } else {
            launch_command("sh -lc 'pkill -xu \"$(id -u)\" kanshi >/dev/null 2>&1 || true'");
        }
    }

    std::string effective_wallpaper_path() {
        refresh_appearance_catalogs();

        const auto configured_wallpaper = config_string({"appearance", "wallpaper"}, current_system_wallpaper());
        const auto daily_wallpaper = config_bool({"appearance", "daily_wallpaper"}, config_bool({"appearance", "wallpaper_slideshow"}, false));
        if (!daily_wallpaper) {
            return configured_wallpaper;
        }

        if (wallpaper_values_.empty()) {
            return configured_wallpaper;
        }

        const auto now = std::time(nullptr);
        std::tm local_tm{};
        localtime_r(&now, &local_tm);
        const auto index = static_cast<std::size_t>(std::max(0, local_tm.tm_yday)) % wallpaper_values_.size();
        return wallpaper_values_[index];
    }

    void apply_wallpaper_runtime() {
        apply_wallpaper_daemon_once();
        reconcile_wallpaper_daemon();
    }

    bool path_is_wallpaper_only_change(const std::vector<std::string>& path) const {
        if (path.empty() || path.front() != "appearance") {
            return false;
        }

        const auto joined_path = path.size() > 1 ? path[0] + "." + path[1] : path[0];
        return joined_path == "appearance.wallpaper"
            || joined_path == "appearance.wallpaper_per_output"
            || joined_path == "appearance.wallpaper_interval_minutes"
            || joined_path == "appearance.daily_wallpaper"
            || joined_path == "appearance.wallpaper_slideshow";
    }

    void apply_system_appearance_runtime() {
        const auto icon_pack = config_string({"appearance", "icon_pack"}, selected_icon_pack_);
        const auto cursor_theme = config_string({"appearance", "cursor_theme"}, selected_cursor_theme_);
        const auto font_family = config_string({"appearance", "font_family"}, "Sans");
        const auto font_size = config_int({"appearance", "font_size"}, 11);
        const auto hinting = config_string({"appearance", "font_hinting"}, "subpixel-slight");
        const auto color_mode = effective_color_mode();
        const auto font_name = font_family + " " + std::to_string(std::max(1, font_size));

        std::string antialiasing = "rgba";
        std::string hinting_level = "slight";
        if (hinting == "grayscale-medium") {
            antialiasing = "grayscale";
            hinting_level = "medium";
        } else if (hinting == "none") {
            antialiasing = "none";
            hinting_level = "none";
        }

        launch_command(
            "sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.desktop.interface gtk-theme \"Adwaita\" >/dev/null 2>&1 && gsettings set org.gnome.desktop.interface icon-theme "
            + shell_double_quote(icon_pack)
            + " >/dev/null 2>&1 && gsettings set org.gnome.desktop.interface cursor-theme "
            + shell_double_quote(cursor_theme)
            + " >/dev/null 2>&1 && gsettings set org.gnome.desktop.interface font-name "
            + shell_double_quote(font_name)
            + " >/dev/null 2>&1 && gsettings set org.gnome.desktop.interface font-antialiasing "
            + shell_double_quote(antialiasing)
            + " >/dev/null 2>&1 && gsettings set org.gnome.desktop.interface font-hinting "
            + shell_double_quote(hinting_level)
            + " >/dev/null 2>&1'"
        );

        if (color_mode == "dark") {
            launch_command("sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.desktop.interface color-scheme \"prefer-dark\" >/dev/null 2>&1'");
        } else {
            launch_command("sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.desktop.interface color-scheme \"default\" >/dev/null 2>&1'");
        }
    }

    void apply_appearance_runtime() {
        apply_system_appearance_runtime();
        write_theme_override();
        write_labwc_themerc_override();
        write_labwc_integration_files();
        reconfigure_labwc();
    }

    void open_autostart_directory() {
        write_labwc_integration_files();
        const auto autostart_path = resolve_labwc_autostart_path();
        launch_command("sh -lc 'xdg-open " + shell_double_quote(autostart_path.string()) + " >/dev/null 2>&1'");
    }

    void open_shortcuts_config() {
        write_labwc_integration_files();
        const auto user_rc = resolve_labwc_rc_path();
        launch_command("sh -lc 'xdg-open " + shell_double_quote(user_rc.string()) + " >/dev/null 2>&1'");
    }

    void apply_runtime_effects_for_path(const std::vector<std::string>& path) {
        if (path.empty()) {
            return;
        }

        if (path_is_wallpaper_only_change(path)) {
            apply_wallpaper_runtime();
            return;
        }

        if (path.front() == "appearance") {
            apply_system_appearance_runtime();
            write_theme_override();
            write_labwc_themerc_override();
            write_labwc_integration_files();
            reconfigure_labwc();
            return;
        }

        if (path.front() == "input" || path.front() == "shortcuts") {
            if (path.size() > 1 && path[0] == "input" && path[1] == "keyboard_layout") {
                apply_keyboard_layout_runtime();
                return;
            }

            if (write_labwc_integration_files()) {
                reconfigure_labwc();
            }
            return;
        }

        if (path.front() == "autostart") {
            if (write_labwc_integration_files()) {
                apply_autostart_runtime();
            }
            return;
        }

        const auto joined_path = path.size() > 1 ? path[0] + "." + path[1] : path[0];

        if (joined_path == "workspace.do_not_disturb") {
            const auto workspace_json = config_manager_.raw().value("workspace", nlohmann::json::object());
            const auto enabled = workspace_json.value("do_not_disturb", false);
            launch_command(
                std::string{"sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.desktop.notifications show-banners "}
                + (enabled ? "false" : "true")
                + " >/dev/null 2>&1'"
            );
            return;
        }

        if (joined_path == "workspace.notifications_timeout") {
            apply_mako_timeout();
            return;
        }

        if (joined_path == "workspace.window_snapping"
            || joined_path == "workspace.focus_follows_mouse"
            || joined_path == "workspace.workspace_count"
            || joined_path == "workspace.desktop_menu_refresh"
            || joined_path == "workspace.desktop_menu_settings"
            || joined_path == "workspace.desktop_menu_terminal"
            || joined_path == "workspace.desktop_menu_files") {
            if (write_labwc_integration_files()) {
                reconfigure_labwc();
            }
            return;
        }

        if (path.front() == "workspaces") {
            if (write_labwc_integration_files()) {
                reconfigure_labwc();
            }
            return;
        }

        if (path.front() == "panel") {
            restart_panel();
            return;
        }

        if (joined_path == "network.bluetooth_enabled") {
            const auto network_json = config_manager_.raw().value("network", nlohmann::json::object());
            const auto enabled = network_json.value("bluetooth_enabled", false);
            launch_command(
                std::string{"sh -lc 'command -v bluetoothctl >/dev/null 2>&1 && bluetoothctl power "}
                + (enabled ? "on" : "off")
                + " >/dev/null 2>&1'"
            );
            return;
        }

        if (joined_path == "power.profile") {
            const auto power_json = config_manager_.raw().value("power", nlohmann::json::object());
            const auto profile = power_json.value("profile", std::string{"balanced"});
            launch_command(
                "sh -lc 'command -v powerprofilesctl >/dev/null 2>&1 && powerprofilesctl set "
                + shell_double_quote(profile)
                + " >/dev/null 2>&1'"
            );
            return;
        }

        if (joined_path == "power.idle_suspend") {
            apply_idle_suspend();
            return;
        }

        if (joined_path == "audio.output" || joined_path == "audio.input") {
            apply_audio_routing();
            return;
        }

        if (joined_path == "audio.output_volume") {
            apply_audio_output_volume();
            return;
        }

        if (joined_path == "audio.input_volume") {
            apply_audio_input_volume();
            return;
        }

        if (joined_path == "audio.echo_cancellation") {
            apply_echo_cancellation();
            return;
        }

        if (joined_path == "display.night_light" || joined_path == "display.night_light_temperature") {
            apply_night_light_runtime();
            return;
        }

        if (joined_path == "display.resolution"
            || joined_path == "display.orientation"
            || joined_path == "display.scaling"
            || joined_path == "display.refresh_rate"
            || joined_path == "display.vrr"
            || joined_path == "display.fractional_scaling") {
            if (display_auto_apply_enabled()) {
                apply_display_settings_now();
            } else {
                set_status_caption(tr("cc_status_display_apply_pending"));
            }
            return;
        }

        if (joined_path == "privacy.location") {
            const auto privacy_json = config_manager_.raw().value("privacy", nlohmann::json::object());
            const auto enabled = privacy_json.value("location", false);
            launch_command(
                std::string{"sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.system.location enabled "}
                + (enabled ? "true" : "false")
                + " >/dev/null 2>&1'"
            );
            return;
        }

        if (joined_path == "privacy.password_on_resume") {
            const auto privacy_json = config_manager_.raw().value("privacy", nlohmann::json::object());
            const auto enabled = privacy_json.value("password_on_resume", true);
            launch_command(
                std::string{"sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.desktop.screensaver lock-enabled "}
                + (enabled ? "true" : "false")
                + " >/dev/null 2>&1'"
            );
            return;
        }

        if (joined_path == "privacy.screen_blank") {
            const auto value = config_string({"privacy", "screen_blank"}, "5");
            const auto seconds = value == "never" ? 0 : std::max(0, std::stoi(value) * 60);
            launch_command(
                "sh -lc 'command -v gsettings >/dev/null 2>&1 && gsettings set org.gnome.desktop.session idle-delay uint32 "
                + std::to_string(seconds)
                + " >/dev/null 2>&1'"
            );
            return;
        }

        if (joined_path == "privacy.camera") {
            apply_camera_toggle();
            return;
        }

        if (joined_path == "privacy.microphone") {
            apply_microphone_toggle();
            return;
        }

        if (joined_path == "privacy.firewall") {
            apply_firewall_toggle();
            return;
        }

        if (joined_path == "power.lid_action") {
            apply_lid_action();
            return;
        }

        if (joined_path == "network.ethernet_mode") {
            apply_ethernet_mode();
            return;
        }

        if (joined_path == "network.proxy_mode") {
            apply_proxy_mode();
            return;
        }

        if (joined_path == "apps.default_browser"
            || joined_path == "apps.default_mail"
            || joined_path == "apps.default_video") {
            apply_default_apps();
            return;
        }
    }

    void persist_config(const std::string& success_key, std::function<void()> after_save = {}) {
        if (config_manager_.save()) {
            const auto previous_status = status_caption_.get_text();
            if (after_save) {
                after_save();
            }

            const auto current_status = status_caption_.get_text();
            const auto failed_status = tr("cc_status_display_layout_apply_failed");
            const auto tool_missing_status = tr("cc_status_display_tool_missing");
            const auto apply_pending_status = tr("cc_status_display_apply_pending");

            if (current_status == failed_status || current_status == tool_missing_status || current_status == apply_pending_status) {
                return;
            }

            if (current_status == previous_status) {
                set_status_caption(tr(success_key));
            }
            return;
        }

        set_status_caption(tr("cc_status_save_failed"));
    }

    void export_profile() {
        if (!config_manager_.save()) {
            set_status_caption(tr("cc_status_export_failed"));
            return;
        }

        std::error_code error;
        std::filesystem::create_directories(config_manager_.paths().user_config_root, error);
        const auto export_path = config_manager_.paths().user_config_root / "hyalo-profile-export.json";
        std::filesystem::copy_file(config_manager_.config_path(), export_path, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            set_status_caption(tr("cc_status_export_failed"));
            return;
        }

        set_status_caption(tr("cc_status_exported"));
    }

    void reset_to_defaults() {
        if (!config_manager_.load_defaults()) {
            set_status_caption(tr("cc_status_save_failed"));
            return;
        }

        sync_from_config();
        write_theme_override();
        if (!config_manager_.save()) {
            set_status_caption(tr("cc_status_save_failed"));
            return;
        }

        apply_system_appearance_runtime();
        write_labwc_themerc_override();
        write_labwc_integration_files();
        apply_autostart_runtime();
        reconfigure_labwc();

        sync_widgets_from_config();
        refresh_accent_buttons();
        clear_appearance_changes_pending();
        set_status_caption(tr("cc_status_reset"));
    }

    void sync_widgets_from_config() {
        updating_widgets_ = true;
        for (const auto& binding : bound_switches_) {
            if (binding.widget == nullptr || binding.path.empty()) {
                continue;
            }
            binding.widget->set_active(config_bool(binding.path, binding.widget->get_active()));
        }
        for (const auto& binding : bound_combos_) {
            if (binding.widget == nullptr || binding.path.empty() || binding.values.empty()) {
                continue;
            }
            binding.widget->set_active(index_for_value(binding.values, config_string(binding.path, binding.values.front())));
        }
        for (const auto& binding : bound_scales_) {
            if (binding.widget == nullptr || binding.path.empty()) {
                continue;
            }
            binding.widget->set_value(config_number(binding.path, binding.widget->get_value()));
        }

        updating_widgets_ = false;
    }

    void write_theme_override() {
        std::error_code error;
        std::filesystem::create_directories(config_manager_.paths().user_config_root, error);

        const auto accent = appearance_config_.accent_color;
        const auto theme = normalize_standard_theme(config_string({"appearance", "theme"}, selected_theme_.empty() ? std::string{"hyalo"} : selected_theme_));
        const auto color_mode = effective_color_mode();
        const auto transparency = std::clamp(appearance_config_.transparency, 0.0, 1.0);
        constexpr int radius = 0;
        const auto tokens = build_theme_tokens(theme, color_mode, accent, transparency);
        const auto swatch_border = color_mode == "dark"
            ? alpha_expr("#ffffff", 0.88)
            : alpha_expr(tokens.text_main, 0.78);

        std::ofstream output(config_manager_.theme_override_path());
        if (!output.is_open()) {
            return;
        }

        output
            << "@define-color hyalo_bg_desktop " << tokens.bg_desktop << ";\n"
            << "@define-color hyalo_bg_window " << tokens.bg_window << ";\n"
            << "@define-color hyalo_bg_panel " << tokens.bg_panel << ";\n"
            << "@define-color hyalo_bg_card " << tokens.bg_card << ";\n"
            << "@define-color hyalo_bg_card_alt " << tokens.bg_card_alt << ";\n"
            << "@define-color hyalo_bg_raised " << tokens.bg_raised << ";\n"
            << "@define-color hyalo_bg_hover " << tokens.bg_hover << ";\n"
            << "@define-color hyalo_bg_pressed " << tokens.bg_pressed << ";\n"
            << "@define-color hyalo_bg_input " << tokens.bg_input << ";\n"
            << "@define-color hyalo_accent " << tokens.accent << ";\n"
            << "@define-color hyalo_accent_strong " << tokens.accent_strong << ";\n"
            << "@define-color hyalo_accent_contrast " << tokens.accent_contrast << ";\n"
            << "@define-color hyalo_text_main " << tokens.text_main << ";\n"
            << "@define-color hyalo_text_secondary " << tokens.text_secondary << ";\n"
            << "@define-color hyalo_text_muted " << tokens.text_muted << ";\n"
            << "@define-color hyalo_border " << tokens.border << ";\n"
            << "@define-color hyalo_border_strong " << tokens.border_strong << ";\n"
            << "@define-color hyalo_panel_border " << tokens.panel_border << ";\n"
            << "@define-color hyalo_focus_ring " << tokens.focus_ring << ";\n"
            << "@define-color hyalo_success " << tokens.success << ";\n"
            << "@define-color hyalo_success_text " << tokens.success_text << ";\n"
            << "@define-color hyalo_danger " << tokens.danger << ";\n"
            << "@define-color hyalo_danger_text " << tokens.danger_text << ";\n"
            << "@define-color hyalo_shadow_panel " << tokens.shadow_panel << ";\n"
            << "@define-color hyalo_shadow_menu " << tokens.shadow_menu << ";\n"
            << "@define-color hyalo_shadow_modal " << tokens.shadow_modal << ";\n"
            << "@define-color hyalo_overlay_subtle " << tokens.overlay_subtle << ";\n"
            << "@define-color hyalo_overlay_soft " << tokens.overlay_soft << ";\n"
            << "@define-color hyalo_overlay_strong " << tokens.overlay_strong << ";\n\n";

        const auto r = std::to_string(radius) + "px";
        const auto rh = std::to_string(radius > 0 ? std::max(1, radius / 2) : 0) + "px";

        output
            << ".settings-sidebar,\n"
            << ".settings-hero,\n"
            << ".settings-card,\n"
            << ".settings-nav-button,\n"
            << ".settings-status-card,\n"
            << ".settings-wallpaper-preview,\n"
            << ".launcher-hero,\n"
            << ".launcher-system-card,\n"
            << ".launcher-pinned-list,\n"
            << ".launcher-context-menu,\n"
            << ".launcher-context-box,\n"
            << ".launcher-results,\n"
            << ".quick-panel-status-box,\n"
            << ".quick-panel-device-list,\n"
            << ".task-group-box,\n"
            << ".workspace-overview-entry,\n"
            << ".workspace-quick-controls,\n"
            << ".hyalo-modal,\n"
            << ".store-panel,\n"
            << ".store-results-list,\n"
            << ".clock-panel-time-box,\n"
            << ".panel-minimized-tray {\n"
            << "    border-radius: " << r << ";\n"
            << "}\n\n"
            << ".settings-input,\n"
            << ".settings-picker,\n"
            << ".launcher-search,\n"
            << ".launcher-filter,\n"
            << ".launcher-entry,\n"
            << ".launcher-entry-badge,\n"
            << ".launcher-pinned-entry,\n"
            << ".launcher-power-button,\n"
            << ".launcher-sidebar-action,\n"
            << ".launcher-sidebar-secondary-action,\n"
            << ".launcher-context-action,\n"
            << ".quick-panel-toggle,\n"
            << ".quick-panel-power-action,\n"
            << ".quick-panel-action,\n"
            << ".quick-panel-device-button,\n"
            << ".task-group-entry,\n"
            << ".task-row,\n"
            << ".panel-pill,\n"
            << ".store-chip-button,\n"
            << ".display-preview-output,\n"
            << ".update-auth-modal-entry,\n"
            << ".update-auth-modal-surface {\n"
            << "    border-radius: " << rh << ";\n"
            << "}\n\n"
            << ".settings-swatch.active-swatch {\n"
            << "    border-color: " << swatch_border << ";\n"
            << "    box-shadow: 0 0 0 3px " << alpha_expr(tokens.accent, 0.18) << ";\n"
            << "}\n";

        output.flush();
        hyalo::core::StyleManager::apply(config_manager_);
    }

    void refresh_accent_buttons() {
        for (std::size_t index = 0; index < accent_buttons_.size() && index < accent_options_.size(); ++index) {
            if (accent_options_[index].color == appearance_config_.accent_color) {
                accent_buttons_[index]->add_css_class("active-swatch");
            } else {
                accent_buttons_[index]->remove_css_class("active-swatch");
            }
        }
    }

    hyalo::core::ConfigManager& config_manager_;
    hyalo::core::Localization& localization_;
    hyalo::core::AppearanceConfig appearance_config_;
    hyalo::core::PanelConfig panel_config_;
    std::string language_code_;
    std::string selected_theme_;
    std::string selected_icon_pack_;
    std::string selected_cursor_theme_;
    std::string selected_window_theme_;
    std::string selected_color_mode_;
    bool updating_widgets_ = false;
    std::vector<std::string> theme_labels_;
    std::vector<std::string> theme_values_;
    std::vector<std::string> icon_pack_labels_;
    std::vector<std::string> icon_pack_values_;
    std::vector<std::string> cursor_theme_labels_;
    std::vector<std::string> cursor_theme_values_;
    std::vector<std::string> window_theme_labels_;
    std::vector<std::string> window_theme_values_;
    std::vector<std::string> font_labels_;
    std::vector<std::string> font_values_;
    std::vector<std::string> wallpaper_labels_;
    std::vector<std::string> wallpaper_values_;
    std::vector<std::string> keyboard_layout_labels_;
    std::vector<std::string> keyboard_layout_values_;
    std::vector<std::string> region_locale_labels_;
    std::vector<std::string> region_locale_values_;
    std::vector<std::string> language_labels_;
    std::vector<std::string> language_values_;

    const std::array<AccentOption, 5> accent_options_{
        AccentOption{"cc_accent_emerald", "#8dd8b3", "swatch-emerald"},
        AccentOption{"cc_accent_sky", "#73c7f2", "swatch-sky"},
        AccentOption{"cc_accent_amber", "#d9a54d", "swatch-amber"},
        AccentOption{"cc_accent_rose", "#de7f95", "swatch-rose"},
        AccentOption{"cc_accent_violet", "#8d73d8", "swatch-violet"},
    };

    Gtk::Box root_{Gtk::Orientation::HORIZONTAL, 20};
    Gtk::ScrolledWindow window_scroll_;
    Gtk::Box sidebar_{Gtk::Orientation::VERTICAL, 14};
    Gtk::SearchEntry search_entry_;
    Gtk::Box nav_list_{Gtk::Orientation::VERTICAL, 8};
    Gtk::ScrolledWindow content_scroll_;
    Gtk::Box content_box_{Gtk::Orientation::VERTICAL, 20};
    Gtk::Box header_{Gtk::Orientation::HORIZONTAL, 20};
    Gtk::Box header_text_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label section_badge_;
    Gtk::Label section_title_;
    Gtk::Label section_subtitle_;
    Gtk::Label status_title_;
    Gtk::Label status_value_;
    Gtk::Label status_caption_;
    Gtk::Stack content_stack_;
    std::vector<NavItem> nav_items_;
    std::vector<Gtk::Button*> accent_buttons_;
    std::vector<BoundSwitch> bound_switches_;
    std::vector<BoundCombo> bound_combos_;
    std::vector<BoundScale> bound_scales_;
    Glib::RefPtr<Gtk::FileChooserNative> wallpaper_import_dialog_;

    Gtk::ComboBoxText* theme_combo_ = nullptr;
    Gtk::ComboBoxText* icon_pack_combo_ = nullptr;
    Gtk::ComboBoxText* cursor_theme_combo_ = nullptr;
    Gtk::ComboBoxText* window_theme_combo_ = nullptr;
    Gtk::ComboBoxText* color_mode_combo_ = nullptr;
    Gtk::ComboBoxText* wallpaper_combo_ = nullptr;
    Gtk::ComboBoxText* wallpaper_interval_combo_ = nullptr;
    Gtk::Picture* wallpaper_preview_ = nullptr;
    Gtk::ComboBoxText* font_combo_ = nullptr;
    Gtk::Scale* font_scale_ = nullptr;
    Gtk::Label* font_value_label_ = nullptr;
    Gtk::Scale* transparency_scale_ = nullptr;
    Gtk::Label* transparency_value_label_ = nullptr;
    Gtk::Scale* deco_button_size_scale_ = nullptr;
    Gtk::Label* deco_button_size_label_ = nullptr;
    Gtk::Scale* deco_button_spacing_scale_ = nullptr;
    Gtk::Label* deco_button_spacing_label_ = nullptr;
    Gtk::Scale* deco_shadow_scale_ = nullptr;
    Gtk::Label* deco_shadow_label_ = nullptr;
    Gtk::Scale* deco_titlebar_padding_scale_ = nullptr;
    Gtk::Label* deco_titlebar_padding_label_ = nullptr;
    Gtk::ComboBoxText* hinting_combo_ = nullptr;
    Gtk::ComboBoxText* display_output_combo_ = nullptr;
    std::vector<std::string> display_output_values_;
    Gtk::ComboBoxText* resolution_combo_ = nullptr;
    Gtk::ComboBoxText* orientation_combo_ = nullptr;
    Gtk::ComboBoxText* scaling_combo_ = nullptr;
    Gtk::Fixed* display_layout_canvas_ = nullptr;
    std::vector<DisplayOutputPreview> display_outputs_preview_;
    std::map<std::string, Gtk::Widget*> display_layout_widgets_;
    std::string primary_output_name_;
    std::string display_dragged_output_name_;
    double display_drag_origin_x_ = 0.0;
    double display_drag_origin_y_ = 0.0;
    int display_preview_min_x_ = 0;
    int display_preview_min_y_ = 0;
    double display_preview_scale_ = 1.0;
    Gtk::ComboBoxText* refresh_combo_ = nullptr;
    DisplayCapabilities cached_capabilities_;
    Gtk::Scale* color_temperature_scale_ = nullptr;
    Gtk::Label* color_temperature_value_label_ = nullptr;
    Gtk::Switch* vrr_switch_ = nullptr;
    Gtk::Scale* pointer_speed_scale_ = nullptr;
    Gtk::Label* pointer_speed_value_label_ = nullptr;
    Gtk::ComboBoxText* acceleration_combo_ = nullptr;
    Gtk::ComboBoxText* keyboard_layout_combo_ = nullptr;
    Gtk::DrawingArea* keyboard_preview_ = nullptr;
    Gtk::Scale* repeat_delay_scale_ = nullptr;
    Gtk::Label* repeat_delay_value_label_ = nullptr;
    Gtk::Scale* repeat_rate_scale_ = nullptr;
    Gtk::Label* repeat_rate_value_label_ = nullptr;
    Gtk::ComboBoxText* launcher_shortcut_combo_ = nullptr;
    Gtk::ComboBoxText* screenshot_full_shortcut_combo_ = nullptr;
    Gtk::ComboBoxText* screenshot_area_shortcut_combo_ = nullptr;
    Gtk::Scale* pressure_scale_ = nullptr;
    Gtk::Label* pressure_value_label_ = nullptr;
    Gtk::ComboBoxText* hot_corners_combo_ = nullptr;
    Gtk::ComboBoxText* workspace_count_combo_ = nullptr;
    Gtk::Box* workspace_list_box_ = nullptr;
    Gtk::FlowBox* app_pool_flowbox_ = nullptr;
    std::vector<DesktopAppEntry> available_apps_;
    Gtk::ComboBoxText* panel_position_combo_ = nullptr;
    Gtk::Button* appearance_apply_button_ = nullptr;
    Gtk::Scale* panel_height_scale_ = nullptr;
    Gtk::Label* panel_height_value_label_ = nullptr;
    Gtk::ComboBoxText* notifications_timeout_combo_ = nullptr;
    Gtk::ComboBoxText* ethernet_combo_ = nullptr;
    Gtk::ComboBoxText* proxy_mode_combo_ = nullptr;
    Gtk::ComboBoxText* audio_output_combo_ = nullptr;
    Gtk::ComboBoxText* audio_input_combo_ = nullptr;
    Gtk::Scale* output_volume_scale_ = nullptr;
    Gtk::Label* output_volume_value_label_ = nullptr;
    Gtk::Scale* input_volume_scale_ = nullptr;
    Gtk::Label* input_volume_value_label_ = nullptr;
    Gtk::ComboBoxText* power_profile_combo_ = nullptr;
    Gtk::ComboBoxText* lid_action_combo_ = nullptr;
    Gtk::ComboBoxText* idle_suspend_combo_ = nullptr;
    Gtk::ComboBoxText* screen_blank_combo_ = nullptr;
    Gtk::ComboBoxText* browser_combo_ = nullptr;
    Gtk::ComboBoxText* mail_combo_ = nullptr;
    Gtk::ComboBoxText* video_combo_ = nullptr;
    Gtk::ComboBoxText* region_combo_ = nullptr;
    Gtk::ComboBoxText* language_combo_ = nullptr;
    Gtk::Label* gpu_installed_drivers_label_ = nullptr;
    Gtk::ComboBoxText* gpu_vendor_filter_combo_ = nullptr;
    Gtk::ComboBoxText* gpu_driver_candidate_combo_ = nullptr;
    Gtk::Button* gpu_driver_install_button_ = nullptr;
    std::vector<std::string> gpu_driver_candidates_;
    std::string selected_gpu_vendor_filter_ = "all";
    sigc::connection gpu_install_status_watch_;
    bool gpu_install_in_progress_ = false;
    std::vector<std::string> connected_outputs_;
    std::vector<OutputWallpaperBinding> output_wallpaper_combos_;
    bool appearance_changes_pending_ = false;
    bool wallpaper_changes_pending_ = false;
};

}  // namespace

int main(int argc, char* argv[]) {
    configure_stable_gsk_renderer();

    auto application = Gtk::Application::create(
        "org.hyalo.ControlCenter",
        Gio::Application::Flags::NON_UNIQUE
    );

    auto config_manager = hyalo::core::ConfigManager(hyalo::core::detect_runtime_paths());
    config_manager.load();

    auto localization = hyalo::core::Localization(config_manager);
    localization.load();

    hyalo::core::StyleManager::apply(config_manager);

    return application->make_window_and_run<ControlCenterWindow>(argc, argv, config_manager, localization);
}