#include "hyalo-core/style_manager.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

#include <sys/statvfs.h>

#include <gdkmm/display.h>
#include <giomm/settings.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/settings.h>
#include <gtkmm/stylecontext.h>

#include <glib-object.h>

#include <nlohmann/json.hpp>

namespace hyalo::core {

namespace {

Glib::RefPtr<Gtk::CssProvider> g_theme_bundle_provider;
bool g_theme_bundle_provider_registered = false;

std::string gtk_base_theme_name() {
    return "Adwaita";
}

bool system_prefers_dark() {
    const auto* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || runtime_dir[0] == '\0') {
        return false;
    }

    struct statvfs fs_stat {};
    if (statvfs(runtime_dir, &fs_stat) != 0) {
        return false;
    }

    // Avoid dconf access when runtime tmpfs has no free blocks/inodes.
    if (fs_stat.f_bavail == 0 || fs_stat.f_favail == 0) {
        return false;
    }

    try {
        const auto settings = Gio::Settings::create("org.gnome.desktop.interface");
        return settings && settings->get_string("color-scheme") == "prefer-dark";
    } catch (...) {
        return false;
    }
}

std::string normalize_css_syntax(std::string css) {
    static const auto localized_alpha_pattern = std::regex(
        R"(alpha\(([^,\)]+),\s*([0-9]+)\s*,\s*([0-9]+)\))"
    );
    return std::regex_replace(css, localized_alpha_pattern, "alpha($1, $2.$3)");
}

std::string read_css_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return {};
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto original_css = buffer.str();
    auto css = normalize_css_syntax(original_css);

    if (css != original_css) {
        std::ofstream output(path, std::ios::trunc);
        if (output.is_open()) {
            output << css;
        }
    }

    css += '\n';
    return css;
}

bool prefers_dark_mode(const ConfigManager& config_manager) {
    const auto appearance_json = config_manager.raw().value("appearance", nlohmann::json::object());
    const auto color_mode = appearance_json.value("color_mode", std::string{"auto"});
    return color_mode == "dark"
        || (color_mode == "auto" && system_prefers_dark());
}

std::string mode_css_overrides(const ConfigManager& config_manager) {
    if (prefers_dark_mode(config_manager)) {
        return {};
    }

    return R"css(
/* Light mode readability overrides */
.panel-nav-button,
.panel-utility-button,
.panel-clock-button,
.panel-quick-button,
.workspace-button,
.task-button,
.workspace-overview-entry-button,
.workspace-quick-action,
.launcher-filter,
.launcher-entry,
.launcher-pinned-entry,
.launcher-power-button,
.launcher-sidebar-secondary-action,
.launcher-context-action,
.quick-panel-device-button,
.quick-panel-action,
.quick-panel-power-action {
    background-color: @hyalo_bg_card_alt;
    border-color: @hyalo_border;
    color: @hyalo_text_main;
}

.panel-nav-button:hover,
.panel-utility-button:hover,
.panel-clock-button:hover,
.panel-quick-button:hover,
.workspace-button:hover,
.task-button:hover,
.workspace-overview-entry-button:hover,
.workspace-quick-action:hover,
.launcher-filter:hover,
.launcher-entry:hover,
.launcher-pinned-entry:hover,
.launcher-power-button:hover,
.launcher-sidebar-secondary-action:hover,
.launcher-context-action:hover,
.quick-panel-device-button:hover,
.quick-panel-action:hover,
.quick-panel-power-action:hover {
    background-color: @hyalo_bg_hover;
    border-color: @hyalo_border_strong;
}
 )css";
}

bool apply_provider_bundle(const std::vector<std::filesystem::path>& paths, const std::string& extra_css) {
    std::string css;
    for (const auto& path : paths) {
        css += read_css_file(path);
    }

    css += extra_css;

    if (css.empty()) {
        return false;
    }

    const auto display = Gdk::Display::get_default();
    if (!display) {
        return false;
    }

    if (!g_theme_bundle_provider) {
        g_theme_bundle_provider = Gtk::CssProvider::create();
    }

    g_theme_bundle_provider->load_from_string(css);

    if (!g_theme_bundle_provider_registered) {
        Gtk::StyleContext::add_provider_for_display(
            display,
            g_theme_bundle_provider,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
        g_theme_bundle_provider_registered = true;
    }

    return true;
}

void apply_icon_theme(const ConfigManager& config_manager, const Glib::RefPtr<Gdk::Display>& display) {
    const auto icons_root = config_manager.paths().assets_root.parent_path() / "icons";
    if (std::filesystem::exists(icons_root / "hyalo-icons")) {
        const auto icon_theme = Gtk::IconTheme::get_for_display(display);
        if (icon_theme) {
            icon_theme->add_search_path(icons_root.string());
        }
    }

    const auto appearance_json = config_manager.raw().value("appearance", nlohmann::json::object());
    const auto icon_pack = appearance_json.value("icon_pack", std::string{"hyalo-icons"});

    const auto settings = Gtk::Settings::get_default();
    if (!settings) {
        return;
    }

    g_object_set(settings->gobj(), "gtk-icon-theme-name", icon_pack.c_str(), nullptr);
}

void apply_gtk_preferences(const ConfigManager& config_manager) {
    const auto settings = Gtk::Settings::get_default();
    if (!settings) {
        return;
    }

    const auto appearance_json = config_manager.raw().value("appearance", nlohmann::json::object());
    const auto cursor_theme = appearance_json.value("cursor_theme", std::string{"hyalo-cursor"});
    const auto font_family = appearance_json.value("font_family", std::string{"Sans"});
    const auto font_size = appearance_json.value("font_size", 10);
    const auto prefer_dark = prefers_dark_mode(config_manager);
    const auto font_name = font_family + " " + std::to_string(std::max(1, font_size));

    g_object_set(
        settings->gobj(),
        "gtk-theme-name", gtk_base_theme_name().c_str(),
        "gtk-cursor-theme-name", cursor_theme.c_str(),
        "gtk-font-name", font_name.c_str(),
        "gtk-application-prefer-dark-theme", prefer_dark,
        nullptr
    );
}

}  // namespace

bool StyleManager::apply(const ConfigManager& config_manager) {
    const auto display = Gdk::Display::get_default();
    if (!display) {
        return false;
    }

    apply_icon_theme(config_manager, display);
    apply_gtk_preferences(config_manager);

    return apply_provider_bundle(
        {
            config_manager.theme_style_path(),
            config_manager.base_style_path(),
            config_manager.theme_override_path(),
        },
        mode_css_overrides(config_manager)
    );
}

}  // namespace hyalo::core