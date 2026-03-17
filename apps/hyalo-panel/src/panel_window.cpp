#include "hyalo-panel/panel_window.hpp"

#include <chrono>
#include <ctime>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <gio/gdesktopappinfo.h>
#include <giomm/file.h>
#include <glib-unix.h>
#include <glibmm/main.h>
#include <gdk/gdkkeysyms.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/dialog.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/image.h>

#include "hyalo-core/style_manager.hpp"

#if HYALO_PANEL_HAS_GTK4_LAYER_SHELL
#include <gtk4-layer-shell.h>
#endif

namespace hyalo::panel {

namespace {

constexpr int kPanelHorizontalMargin = 0;
constexpr int kPanelVerticalMargin = 0;
constexpr bool kQuickPanelEnabled = false;
constexpr int kLauncherScreenMargin = 12;
constexpr int kLauncherPanelGap = -18;
constexpr int kLauncherWidth = 700;
constexpr int kLauncherMaxVisibleHeight = 420;
constexpr int kLauncherReservedChromeHeight = 250;
constexpr int kLauncherMaxWindowHeight = 560;
constexpr std::size_t kLauncherRenderBatchSize = 32;
constexpr std::size_t kLauncherMaxPinnedEntries = 16;
constexpr std::size_t kLauncherDefaultPinnedCount = 5;

Gtk::Widget& make_button_content(const char* icon_name, const std::string& label, int icon_size = 16) {
    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name(icon_name);
    icon->set_pixel_size(icon_size);

    auto* text = Gtk::make_managed<Gtk::Label>(label);
    text->set_xalign(0.0f);

    content->append(*icon);
    content->append(*text);
    return *content;
}

    Gtk::Widget& make_icon_widget(const char* icon_name, int icon_size = 18) {
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(icon_name);
        icon->set_pixel_size(icon_size);
        return *icon;
    }

    struct LauncherFilterDefinition {
        std::string_view key;
        std::string_view translation_key;
    };

    constexpr auto kLauncherFilters = std::array{
        LauncherFilterDefinition{"all", "launcher_filter_all"},
        LauncherFilterDefinition{"internet", "launcher_filter_internet"},
        LauncherFilterDefinition{"work", "launcher_filter_work"},
        LauncherFilterDefinition{"media", "launcher_filter_media"},
        LauncherFilterDefinition{"development", "launcher_filter_development"},
        LauncherFilterDefinition{"system", "launcher_filter_system"},
    };

    constexpr auto kPinnedDesktopIds = std::array{
        "firefox.desktop",
        "org.mozilla.firefox.desktop",
        "org.gnome.Nautilus.desktop",
        "org.gnome.Console.desktop",
        "org.gnome.Terminal.desktop",
        "kitty.desktop",
        "code.desktop",
        "codium.desktop",
        "thunar.desktop",
        "pcmanfm.desktop",
        "org.gnome.Calculator.desktop",
    };

    bool layer_shell_disabled() {
        const auto* value = std::getenv("HYALO_PANEL_DISABLE_LAYER_SHELL");
        if (!value) {
            value = std::getenv("HYALO_PANEL_DISABLE_LAYER_SHELL");
        }
        if (!value) {
            return false;
        }

        const auto text = std::string{value};
        return text == "1" || text == "true" || text == "TRUE" || text == "yes" || text == "on";
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

    std::string make_valid_utf8(std::string_view text) {
        if (g_utf8_validate(text.data(), static_cast<gssize>(text.size()), nullptr)) {
            return std::string(text);
        }

        auto* valid = g_utf8_make_valid(text.data(), static_cast<gssize>(text.size()));
        if (!valid) {
            return {};
        }

        std::string normalized(valid);
        g_free(valid);
        return normalized;
    }

    std::string sanitize_single_line_text(std::string_view text) {
        auto sanitized = make_valid_utf8(text);
        std::replace(sanitized.begin(), sanitized.end(), '\n', ' ');
        std::replace(sanitized.begin(), sanitized.end(), '\r', ' ');
        std::replace(sanitized.begin(), sanitized.end(), '\t', ' ');
        return sanitized;
    }

    std::string join_drag_window_identifiers(const std::vector<std::string>& identifiers) {
        auto payload = std::string{};
        for (const auto& identifier : identifiers) {
            if (identifier.empty()) {
                continue;
            }
            if (!payload.empty()) {
                payload.push_back('\n');
            }
            payload += sanitize_single_line_text(identifier);
        }
        return payload;
    }

    std::vector<std::string> parse_drag_window_identifiers(const Glib::ValueBase& value) {
        const auto* raw = g_value_get_string(value.gobj());
        if (!raw || raw[0] == '\0') {
            return {};
        }

        auto identifiers = std::vector<std::string>{};
        auto stream = std::stringstream(std::string{raw});
        auto line = std::string{};
        while (std::getline(stream, line)) {
            const auto identifier = sanitize_single_line_text(line);
            if (!identifier.empty()) {
                identifiers.push_back(identifier);
            }
        }
        return identifiers;
    }

    Glib::RefPtr<Gdk::ContentProvider> create_drag_content_provider(const std::vector<std::string>& identifiers) {
        const auto payload = join_drag_window_identifiers(identifiers);
        if (payload.empty()) {
            return {};
        }

        auto value = Glib::Value<Glib::ustring>{};
        value.init(G_TYPE_STRING);
        value.set(payload);
        return Gdk::ContentProvider::create(value);
    }

std::string compact_label(std::string_view text, std::size_t max_length) {
    const auto valid_text = make_valid_utf8(text);
    const auto character_count = static_cast<std::size_t>(g_utf8_strlen(valid_text.c_str(), static_cast<gssize>(valid_text.size())));
    if (character_count <= max_length) {
        return valid_text;
    }

    if (max_length <= 3) {
        const auto* end = g_utf8_offset_to_pointer(valid_text.c_str(), static_cast<glong>(max_length));
        return std::string(valid_text.c_str(), end);
    }

    const auto* end = g_utf8_offset_to_pointer(valid_text.c_str(), static_cast<glong>(max_length - 3));
    auto label = std::string(valid_text.c_str(), end);
    label += "...";
    return label;
}

bool panel_diagnostics_enabled() {
    const auto* value = std::getenv("HYALO_PANEL_DIAGNOSTICS");
    if (!value) {
        value = std::getenv("HYALO_PANEL_DEBUG");
    }
    if (!value) {
        return false;
    }

    const auto text = std::string{value};
    return text == "1" || text == "true" || text == "TRUE" || text == "yes" || text == "on";
}

void log_panel_diagnostic(bool enabled, const std::string& message) {
    if (!enabled) {
        return;
    }

    g_message("hyalo-panel diagnostic: %s", message.c_str());
}

std::string widget_debug_name(GtkWidget* widget) {
    if (!widget) {
        return "none";
    }

    const auto* name = gtk_widget_get_name(widget);
    const auto* type_name = G_OBJECT_TYPE_NAME(widget);
    if (name && name[0] != '\0') {
        return std::string(type_name ? type_name : "GtkWidget") + ":" + name;
    }
    return type_name ? type_name : "GtkWidget";
}

std::string casefold(std::string_view text) {
    const auto valid_text = make_valid_utf8(text);
    auto* folded = g_utf8_casefold(valid_text.c_str(), static_cast<gssize>(valid_text.size()));
    if (!folded) {
        return {};
    }

    std::string normalized(folded);
    g_free(folded);
    return normalized;
}

void append_search_part(std::string& target, std::string_view value) {
    if (value.empty()) {
        return;
    }

    if (!target.empty()) {
        target.push_back(' ');
    }

    target += casefold(value);
}

std::string window_group_key(const WindowSnapshot& window) {
    if (!window.app_id.empty()) {
        return casefold(window.app_id);
    }
    if (!window.title.empty()) {
        return casefold(window.title);
    }
    return casefold(window.identifier);
}

std::string prettify_app_identifier(std::string_view text) {
    auto label = make_valid_utf8(text);
    if (label.empty()) {
        return {};
    }

    if (const auto separator = label.find_last_of("./"); separator != std::string::npos && separator + 1 < label.size()) {
        label = label.substr(separator + 1);
    }
    if (const auto separator = label.find_last_of('.'); separator != std::string::npos && separator + 1 < label.size()) {
        label = label.substr(separator + 1);
    }

    for (auto& character : label) {
        if (character == '.' || character == '-' || character == '_') {
            character = ' ';
        }
    }

    auto uppercase_next = true;
    for (auto& character : label) {
        if (std::isspace(static_cast<unsigned char>(character))) {
            uppercase_next = true;
            continue;
        }
        if (uppercase_next) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            uppercase_next = false;
        }
    }

    const auto first = label.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    const auto last = label.find_last_not_of(' ');
    return label.substr(first, last - first + 1);
}

std::vector<std::filesystem::path> desktop_entry_directories() {
    auto directories = std::vector<std::filesystem::path>{};
    auto seen = std::unordered_set<std::string>{};

    const auto append_directory = [&](const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }

        const auto normalized = path.lexically_normal().string();
        if (seen.insert(normalized).second) {
            directories.push_back(path / "applications");
        }
    };

    if (const auto* data_home = std::getenv("XDG_DATA_HOME"); data_home && *data_home) {
        append_directory(data_home);
    } else if (const auto* home = std::getenv("HOME"); home && *home) {
        append_directory(std::filesystem::path(home) / ".local/share");
    }

    if (const auto* data_dirs = std::getenv("XDG_DATA_DIRS"); data_dirs && *data_dirs) {
        std::stringstream stream(data_dirs);
        auto segment = std::string{};
        while (std::getline(stream, segment, ':')) {
            append_directory(segment);
        }
    } else {
        append_directory("/usr/local/share");
        append_directory("/usr/share");
    }

    return directories;
}

bool has_category_token(std::string_view categories, std::string_view token) {
    auto start = std::size_t{0};
    while (start < categories.size()) {
        const auto end = categories.find(';', start);
        const auto part = categories.substr(start, end == std::string_view::npos ? categories.size() - start : end - start);
        if (part == token) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

std::string launcher_category_from_desktop_entry(std::string_view categories) {
    if (has_category_token(categories, "Network")
        || has_category_token(categories, "WebBrowser")
        || has_category_token(categories, "Email")
        || has_category_token(categories, "InstantMessaging")
        || has_category_token(categories, "FileTransfer")
        || has_category_token(categories, "Telephony")
        || has_category_token(categories, "Chat")) {
        return "internet";
    }

    if (has_category_token(categories, "Office")
        || has_category_token(categories, "Spreadsheet")
        || has_category_token(categories, "WordProcessor")
        || has_category_token(categories, "Calendar")
        || has_category_token(categories, "Presentation")
        || has_category_token(categories, "Database")
        || has_category_token(categories, "Finance")
        || has_category_token(categories, "Education")
        || has_category_token(categories, "Science")) {
        return "work";
    }

    if (has_category_token(categories, "AudioVideo")
        || has_category_token(categories, "Audio")
        || has_category_token(categories, "Video")
        || has_category_token(categories, "Graphics")
        || has_category_token(categories, "Photography")
        || has_category_token(categories, "Viewer")
        || has_category_token(categories, "Player")
        || has_category_token(categories, "Recorder")
        || has_category_token(categories, "Game")) {
        return "media";
    }

    if (has_category_token(categories, "Development")
        || has_category_token(categories, "IDE")
        || has_category_token(categories, "GUIDesigner")
        || has_category_token(categories, "RevisionControl")
        || has_category_token(categories, "Debugger")
        || has_category_token(categories, "Building")) {
        return "development";
    }

    if (has_category_token(categories, "Settings")
        || has_category_token(categories, "System")
        || has_category_token(categories, "Utility")
        || has_category_token(categories, "Development")
        || has_category_token(categories, "FileManager")
        || has_category_token(categories, "TerminalEmulator")
        || has_category_token(categories, "Monitor")
        || has_category_token(categories, "Security")
        || has_category_token(categories, "PackageManager")) {
        return "system";
    }

    return "system";
}

std::string launcher_category_label(const std::string& key, const core::Localization& localization) {
    if (key == "internet") {
        return localization.translate("launcher_filter_internet");
    }
    if (key == "work") {
        return localization.translate("launcher_filter_work");
    }
    if (key == "media") {
        return localization.translate("launcher_filter_media");
    }
    if (key == "development") {
        return localization.translate("launcher_filter_development");
    }
    return localization.translate("launcher_filter_system");
}

bool contains_token(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

std::string hyalo_icon_from_tokens(const std::vector<std::string>& tokens, std::string_view category) {
    auto has_any_token = [&tokens](std::initializer_list<std::string_view> needles) {
        for (const auto& token : tokens) {
            for (const auto& needle : needles) {
                if (contains_token(token, needle)) {
                    return true;
                }
            }
        }
        return false;
    };

    if (has_any_token({"hyalo-control-center", "gnome-control-center", "settings", "systemsettings", "pavucontrol"})) {
        return "hyalo-control-center";
    }
    if (has_any_token({"hyalo-terminal", "terminal", "console", "kitty", "wezterm", "alacritty", "foot", "xterm"})) {
        return "hyalo-terminal";
    }
    if (has_any_token({"nautilus", "thunar", "pcmanfm", "dolphin", "filemanager", "files"})) {
        return "hyalo-files";
    }
    if (has_any_token({"loupe", "eog", "gwenview", "image", "photoshotwell", "pix"})) {
        return "hyalo-image-viewer";
    }
    if (has_any_token({"firefox", "chromium", "brave", "vivaldi", "browser", "epiphany", "web"})) {
        return "hyalo-browser";
    }
    if (has_any_token({"calendar", "org.gnome.calendar"})) {
        return "hyalo-calendar";
    }
    if (has_any_token({"notes", "gedit", "texteditor", "kate", "writer", "libreoffice-writer"})) {
        return "hyalo-notes";
    }
    if (has_any_token({"system-monitor", "btop", "htop", "missioncenter", "resources"})) {
        return "hyalo-system-monitor";
    }
    if (has_any_token({"discover", "software", "pamac", "store", "package"})) {
        return "hyalo-software";
    }
    if (has_any_token({"vlc", "mpv", "spotify", "music", "video", "player", "rhythmbox"})) {
        return "hyalo-media-player";
    }
    if (has_any_token({"camera", "cheese"})) {
        return "hyalo-camera";
    }
    if (has_any_token({"archive", "file-roller", "ark", "engrampa"})) {
        return "hyalo-archive";
    }
    if (has_any_token({"network", "nm-connection", "wifi", "bluetooth"})) {
        return "hyalo-network";
    }
    if (has_any_token({"power", "battery"})) {
        return "hyalo-power";
    }
    if (has_any_token({"security", "keyring", "keepass", "vault"})) {
        return "hyalo-security";
    }
    if (has_any_token({"display", "monitor", "arandr", "wdisplays"})) {
        return "hyalo-display";
    }
    if (has_any_token({"workspace", "overview"})) {
        return "hyalo-workspaces";
    }
    if (category == "internet") {
        return "hyalo-browser";
    }
    if (category == "work") {
        return "hyalo-notes";
    }
    if (category == "media") {
        return "hyalo-media-player";
    }
    if (category == "development") {
        return "hyalo-terminal";
    }
    return "";
}

std::string remap_icon_name(std::string_view primary, std::string_view desktop_id = {}, std::string_view app_name = {}, std::string_view category = {}) {
    auto tokens = std::vector<std::string>{};
    const auto add_token = [&tokens](std::string_view value) {
        if (!value.empty()) {
            tokens.push_back(casefold(std::string(value)));
        }
    };

    add_token(primary);
    add_token(desktop_id);
    add_token(app_name);
    add_token(category);

    const auto mapped = hyalo_icon_from_tokens(tokens, casefold(std::string(category)));
    if (!mapped.empty()) {
        return mapped;
    }

    if (!primary.empty()) {
        return std::string(primary);
    }

    return "hyalo-launcher";
}

std::string launcher_icon_name(GIcon* icon) {
    if (icon && G_IS_THEMED_ICON(icon)) {
        const auto* names = g_themed_icon_get_names(G_THEMED_ICON(icon));
        if (names && names[0]) {
            return remap_icon_name(names[0]);
        }
    }

    return "hyalo-launcher";
}

bool is_pinned_launcher(std::string_view desktop_id) {
    return std::find(kPinnedDesktopIds.begin(), kPinnedDesktopIds.end(), desktop_id) != kPinnedDesktopIds.end();
}

std::filesystem::path launcher_state_path(const core::ConfigManager& config_manager) {
    return config_manager.paths().user_config_root / "launcher-state.json";
}

std::string window_title(const WindowSnapshot& window, const core::Localization& localization) {
    if (!window.title.empty()) {
        return make_valid_utf8(window.title);
    }

    if (!window.app_id.empty()) {
        return make_valid_utf8(window.app_id);
    }

    return localization.translate("window_untitled");
}

std::string session_label() {
    if (const auto* session_type = std::getenv("XDG_SESSION_TYPE"); session_type && *session_type) {
        return make_valid_utf8(session_type);
    }

    return "wayland";
}

std::string current_user_name() {
    if (const auto* user = std::getenv("USER"); user && *user) {
        return make_valid_utf8(user);
    }

    const auto* user = g_get_user_name();
    return user ? make_valid_utf8(user) : std::string{"user"};
}

std::string current_display_name() {
    const auto* real_name = g_get_real_name();
    const auto display_name = real_name ? make_valid_utf8(real_name) : std::string{};
    if (!display_name.empty() && display_name != "Unknown") {
        return display_name;
    }

    return current_user_name();
}

std::string launcher_avatar_text() {
    const auto source = current_display_name();
    std::stringstream stream(source);
    auto part = std::string{};
    auto initials = std::string{};

    while (stream >> part && initials.size() < 4) {
        auto* first = g_utf8_substring(part.c_str(), 0, 1);
        if (!first) {
            continue;
        }

        auto initial = std::string(first);
        g_free(first);
        initial = make_valid_utf8(initial);
        if (!initial.empty()) {
            initials += initial;
        }

        if (initials.size() >= 2) {
            break;
        }
    }

    if (initials.empty()) {
        auto username = current_user_name();
        auto* first = g_utf8_substring(username.c_str(), 0, 1);
        if (first) {
            initials = make_valid_utf8(first);
            g_free(first);
        }
    }

    return initials.empty() ? std::string{"L"} : initials;
}

std::string logout_command() {
    if (const auto* session_id = std::getenv("XDG_SESSION_ID"); session_id && *session_id) {
        return "sh -lc 'loginctl terminate-session \"$XDG_SESSION_ID\"'";
    }

    return "sh -lc 'loginctl terminate-user \"$USER\"'";
}

std::string trim_copy(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

std::optional<std::string> command_output(const std::string& command_line) {
    gchar* standard_output = nullptr;
    gchar* standard_error = nullptr;
    gint exit_status = 0;
    GError* error = nullptr;

    const auto guarded_command = std::string{"timeout 2s "} + command_line;

    const auto success = g_spawn_command_line_sync(
        guarded_command.c_str(),
        &standard_output,
        &standard_error,
        &exit_status,
        &error);

    std::string output = standard_output ? standard_output : "";

    if (standard_output) {
        g_free(standard_output);
    }
    if (standard_error) {
        g_free(standard_error);
    }
    if (error) {
        g_error_free(error);
    }

    if (!success || exit_status != 0) {
        return std::nullopt;
    }

    output = trim_copy(output);
    if (output.empty()) {
        return std::nullopt;
    }

    return output;
}

bool command_exists(const char* command_name) {
    static auto cache = std::unordered_map<std::string, bool>{};

    const auto key = std::string{command_name};
    if (const auto iterator = cache.find(key); iterator != cache.end()) {
        return iterator->second;
    }

    auto* path = g_find_program_in_path(command_name);
    const auto exists = path != nullptr;
    g_free(path);
    cache.emplace(key, exists);
    return exists;
}

std::optional<int> parse_first_percentage(std::string_view text) {
    auto digits = std::string{};
    for (char character : text) {
        if (std::isdigit(static_cast<unsigned char>(character))) {
            digits.push_back(character);
            continue;
        }

        if (character == '%' && !digits.empty()) {
            try {
                return std::stoi(digits);
            } catch (...) {
                return std::nullopt;
            }
        }

        digits.clear();
    }

    return std::nullopt;
}

std::optional<int> parse_comma_percentage(std::string_view text) {
    std::stringstream stream{std::string(text)};
    auto segment = std::string{};
    auto index = 0;

    while (std::getline(stream, segment, ',')) {
        ++index;
        if (index != 4) {
            continue;
        }

        segment = trim_copy(segment);
        if (!segment.empty() && segment.back() == '%') {
            segment.pop_back();
        }

        try {
            return std::stoi(segment);
        } catch (...) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::vector<std::string> split_lines(const std::string& text) {
    auto lines = std::vector<std::string>{};
    auto stream = std::stringstream(text);
    auto line = std::string{};
    while (std::getline(stream, line)) {
        line = trim_copy(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::string unescape_nmcli_field(std::string_view text) {
    auto output = std::string{};
    output.reserve(text.size());

    auto escaped = false;
    for (char ch : text) {
        if (escaped) {
            output.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        output.push_back(ch);
    }

    if (escaped) {
        output.push_back('\\');
    }

    return output;
}

std::vector<std::string> split_nmcli_fields(std::string_view line) {
    auto fields = std::vector<std::string>{};
    auto current = std::string{};
    auto escaped = false;

    for (char ch : line) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (ch == ':') {
            fields.push_back(unescape_nmcli_field(current));
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    fields.push_back(unescape_nmcli_field(current));
    return fields;
}

std::string normalize_whitespace(std::string_view text) {
    auto output = std::string{};
    auto last_space = true;

    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            if (!last_space && !output.empty()) {
                output.push_back(' ');
            }
            last_space = true;
            continue;
        }

        output.push_back(static_cast<char>(ch));
        last_space = false;
    }

    if (!output.empty() && output.back() == ' ') {
        output.pop_back();
    }
    return output;
}

std::string strip_status_glyphs(std::string_view text) {
    auto output = std::string{};
    for (unsigned char ch : text) {
        if (std::isalnum(ch) || std::ispunct(ch) || std::isspace(ch)) {
            output.push_back(static_cast<char>(ch));
        }
    }
    return normalize_whitespace(output);
}

std::optional<std::pair<std::string, std::string>> parse_bluetooth_device_line(const std::string& line) {
    static constexpr auto prefix = std::string_view{"Device "};
    if (!line.starts_with(prefix)) {
        return std::nullopt;
    }

    const auto payload = line.substr(prefix.size());
    const auto separator = payload.find(' ');
    if (separator == std::string::npos) {
        return std::nullopt;
    }

    auto address = trim_copy(payload.substr(0, separator));
    auto name = trim_copy(payload.substr(separator + 1));
    if (address.empty() || name.empty()) {
        return std::nullopt;
    }

    return std::make_pair(address, make_valid_utf8(name));
}

std::optional<int> parse_bluetooth_battery_percent(const std::string& text) {
    static constexpr auto marker = std::string_view{"Battery Percentage:"};
    const auto position = text.find(marker);
    if (position == std::string::npos) {
        return std::nullopt;
    }

    return parse_first_percentage(text.substr(position + marker.size()));
}

}  // namespace

PanelWindow::PanelWindow(core::ConfigManager& config_manager, const core::Localization& localization)
    : config_manager_(config_manager)
    , localization_(localization)
    , current_panel_config_(config_manager.panel()) {
    const auto panel_config = config_manager_.panel();
    diagnostics_enabled_ = panel_diagnostics_enabled();

    set_title("Hyalo Panel");
    set_name("hyalo-panel");
    set_default_size(1280, panel_config.height + (kPanelVerticalMargin * 2));
    set_size_request(-1, panel_config.height + (kPanelVerticalMargin * 2));
    add_css_class("hyalo-panel");
    log_panel_diagnostic(diagnostics_enabled_, "startup: constructing main panel window");

#if HYALO_PANEL_HAS_GTK4_LAYER_SHELL
    set_decorated(false);
    set_resizable(false);
#else
    set_decorated(true);
    set_resizable(true);
#endif

    root_box_.set_hexpand(true);
    root_box_.set_margin_start(kPanelHorizontalMargin);
    root_box_.set_margin_end(kPanelHorizontalMargin);
    root_box_.set_margin_top(kPanelVerticalMargin);
    root_box_.set_margin_bottom(kPanelVerticalMargin);
    root_box_.set_size_request(-1, panel_config.height);
    root_box_.set_valign(Gtk::Align::CENTER);
    root_box_.add_css_class("panel-shell");
    set_child(root_box_);

    if constexpr (kQuickPanelEnabled) {
        quick_panel_refresh_dispatcher_.connect(sigc::mem_fun(*this, &PanelWindow::on_quick_panel_refresh_ready));
    }

    left_box_.add_css_class("panel-section");
    center_box_.add_css_class("panel-section");
    right_box_.add_css_class("panel-section");
    tasks_box_.add_css_class("panel-task-shelf");

    left_box_.set_size_request(-1, panel_config.height);
    center_box_.set_size_request(-1, panel_config.height);
    right_box_.set_size_request(-1, panel_config.height);
    tasks_box_.set_size_request(-1, panel_config.height);
    empty_state_apps_box_.set_size_request(-1, panel_config.height);

    left_box_.set_valign(Gtk::Align::CENTER);
    center_box_.set_valign(Gtk::Align::CENTER);
    right_box_.set_valign(Gtk::Align::CENTER);
    tasks_box_.set_valign(Gtk::Align::CENTER);
    empty_state_apps_box_.set_valign(Gtk::Align::CENTER);

    center_box_.set_hexpand(true);
    center_box_.set_halign(Gtk::Align::FILL);
    tasks_box_.set_hexpand(true);
    tasks_box_.set_halign(Gtk::Align::FILL);
    right_box_.set_halign(Gtk::Align::END);
    minimized_tray_box_.add_css_class("panel-minimized-tray");
    empty_state_apps_box_.add_css_class("panel-running-apps");
    empty_state_apps_box_.set_hexpand(true);
    empty_state_apps_box_.set_halign(Gtk::Align::FILL);

    menu_button_icon_.set_from_icon_name("hyalo-symbolic");
    menu_button_icon_.set_pixel_size(20);
    menu_button_icon_.add_css_class("panel-brand-icon");
    menu_button_content_.add_css_class("panel-brand-content");
    menu_button_content_.append(menu_button_icon_);
    menu_button_.set_child(menu_button_content_);
    menu_button_.set_tooltip_text(localization_.translate("menu"));
    menu_button_.add_css_class("accent");
    menu_button_.add_css_class("panel-brand-button");
    menu_button_.signal_clicked().connect([this]() {
        toggle_launcher();
    });

    settings_button_.set_child(make_button_content("hyalo-control-center-symbolic", localization_.translate("settings")));
    settings_button_.add_css_class("panel-utility-button");

    clock_label_.add_css_class("panel-clock-label");
    clock_button_.add_css_class("panel-clock-button");
    clock_button_.set_child(clock_label_);
    if constexpr (kQuickPanelEnabled) {
        quick_panel_button_.add_css_class("panel-quick-button");
    }

    configure_clock_panel();
    if constexpr (kQuickPanelEnabled) {
        configure_quick_panel();
    }

    load_launcher_state();

    configure_launcher();

    left_box_.append(menu_button_);

    center_box_.append(tasks_box_);
    center_box_.append(minimized_tray_box_);

    right_box_.append(clock_button_);
    if constexpr (kQuickPanelEnabled) {
        right_box_.append(quick_panel_button_);
    }

    root_box_.append(left_box_);
    root_box_.append(center_box_);
    root_box_.append(right_box_);

    window_tracker_ = std::make_unique<WindowTracker>();
    window_tracker_->set_callback([this](const std::vector<WindowSnapshot>& windows) {
        update_task_summary(windows);
    });
    window_tracker_->start();

    settings_button_.signal_clicked().connect([this]() {
        handle_settings_activated();
    });

    auto key_controller = Gtk::EventControllerKey::create();
    key_controller->signal_key_pressed().connect([this](guint keyval, guint, Gdk::ModifierType state) {
        const bool ctrl  = (state & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
        const bool shift = (state & Gdk::ModifierType::SHIFT_MASK)   != Gdk::ModifierType{};

        if (keyval == GDK_KEY_Escape) {
            if (launcher_window_.is_visible()) {
                hide_launcher();
                return true;
            }
            if constexpr (kQuickPanelEnabled) {
                if (quick_panel_popover_.is_visible()) {
                quick_panel_popover_.popdown();
                return true;
            }
            }
            if (clock_popover_.is_visible()) {
                clock_popover_.popdown();
                return true;
            }
        }

        return false;
    }, false);
    add_controller(key_controller);

    auto click_controller = Gtk::GestureClick::create();
    click_controller->set_button(0);
    click_controller->signal_pressed().connect([this](int, double, double) {
        if (launcher_window_.is_visible()) {
            hide_launcher();
        }
    });
    add_controller(click_controller);

    g_unix_signal_add(SIGUSR1, [](gpointer data) -> gboolean {
        static_cast<PanelWindow*>(data)->toggle_launcher();
        return G_SOURCE_CONTINUE;
    }, this);

    configure_layer_shell();
    configure_launcher_window();
    setup_hot_corners();
    start_runtime_watchers();
    update_clock();
    update_clock_panel_status();

    if constexpr (kQuickPanelEnabled) {
        Glib::signal_timeout().connect_once([this]() {
            quick_panel_refresh_ready_ = true;
            update_quick_panel_status(true);
        }, 1);
    }

    // Pre-warm launcher rows shortly after startup so first open is instant.
    Glib::signal_timeout().connect_once([this]() {
        if (!launcher_window_.is_visible() && launcher_rows_.empty()) {
            request_launcher_refresh();
        }
    }, 300);

    Glib::signal_timeout().connect_seconds(sigc::mem_fun(*this, &PanelWindow::update_clock), 1);
}

PanelWindow::~PanelWindow() {
    if (quick_panel_refresh_thread_.joinable()) {
        quick_panel_refresh_thread_.request_stop();
        quick_panel_refresh_thread_.join();
    }
}

void PanelWindow::start_runtime_watchers() {
    std::error_code error;
    std::filesystem::create_directories(config_manager_.paths().user_config_root, error);

    try {
        auto runtime_dir = Gio::File::create_for_path(config_manager_.paths().user_config_root.string());
        runtime_directory_monitor_ = runtime_dir->monitor_directory();
    } catch (...) {
        runtime_directory_monitor_.reset();
    }

    if (!runtime_directory_monitor_) {
        return;
    }

    const auto config_name = config_manager_.config_path().filename().string();
    const auto theme_name = config_manager_.theme_override_path().filename().string();

    runtime_directory_monitor_->signal_changed().connect([this, config_name, theme_name](
        const Glib::RefPtr<Gio::File>& file,
        const Glib::RefPtr<Gio::File>&,
        Gio::FileMonitor::Event
    ) {
        if (!file) {
            return;
        }

        const auto basename = file->get_basename();
        if (basename != config_name && basename != theme_name) {
            return;
        }

        schedule_runtime_reload();
    });
}

void PanelWindow::schedule_runtime_reload() {
    if (runtime_reload_pending_) {
        return;
    }

    runtime_reload_pending_ = true;
    Glib::signal_timeout().connect_once([this]() {
        runtime_reload_pending_ = false;
        reload_runtime_config();
    }, 150);
}

void PanelWindow::reload_runtime_config() {
    if (!config_manager_.load()) {
        return;
    }

    const auto next_panel_config = config_manager_.panel();
    const auto panel_config_changed = next_panel_config.position != current_panel_config_.position
        || next_panel_config.height != current_panel_config_.height;

    current_panel_config_ = next_panel_config;

    core::StyleManager::apply(config_manager_);

    // Refresh hot corners setting
    try {
        const auto& raw = config_manager_.raw();
        if (raw.contains("workspace") && raw["workspace"].contains("hot_corners")) {
            hot_corners_mode_ = raw["workspace"]["hot_corners"].get<std::string>();
        }
    } catch (...) {}

    if (panel_config_changed) {
        apply_panel_config();
    }

    update_clock_panel_status();
    update_quick_panel_status();
    queue_draw();
}

void PanelWindow::apply_panel_config() {
    const auto panel_config = config_manager_.panel();
    const auto panel_total_height = panel_config.height + (kPanelVerticalMargin * 2);

    set_default_size(1280, panel_total_height);
    set_size_request(-1, panel_total_height);
    root_box_.set_size_request(-1, panel_config.height);
    left_box_.set_size_request(-1, panel_config.height);
    center_box_.set_size_request(-1, panel_config.height);
    right_box_.set_size_request(-1, panel_config.height);
    tasks_box_.set_size_request(-1, panel_config.height);
    empty_state_apps_box_.set_size_request(-1, panel_config.height);

    configure_layer_shell();
    configure_launcher_window();
}

void PanelWindow::configure_clock_panel() {
    clock_panel_title_label_.set_text(localization_.translate("clock_panel_title"));
    clock_panel_title_label_.add_css_class("clock-panel-title");
    clock_panel_subtitle_label_.set_text(localization_.translate("clock_panel_subtitle"));
    clock_panel_subtitle_label_.add_css_class("clock-panel-subtitle");
    clock_panel_time_label_.add_css_class("clock-panel-time");
    clock_panel_date_label_.add_css_class("clock-panel-date");
    clock_panel_placeholder_label_.set_text(localization_.translate("clock_panel_placeholder"));
    clock_panel_placeholder_label_.add_css_class("clock-panel-placeholder");

    clock_panel_box_.add_css_class("clock-panel-surface");
    clock_panel_header_box_.add_css_class("clock-panel-header");
    clock_panel_time_box_.add_css_class("clock-panel-time-box");
    clock_panel_footer_box_.add_css_class("clock-panel-footer");

    clock_panel_header_box_.append(clock_panel_title_label_);
    clock_panel_header_box_.append(clock_panel_subtitle_label_);
    clock_panel_time_box_.append(clock_panel_time_label_);
    clock_panel_time_box_.append(clock_panel_date_label_);
    clock_panel_footer_box_.append(clock_panel_placeholder_label_);

    clock_panel_box_.append(clock_panel_header_box_);
    clock_panel_box_.append(clock_panel_time_box_);
    clock_panel_box_.append(clock_panel_footer_box_);

    clock_popover_.set_has_arrow(false);
    clock_popover_.set_position(Gtk::PositionType::BOTTOM);
    clock_popover_.set_autohide(true);
    clock_popover_.add_css_class("clock-panel-popover");
    clock_popover_.set_child(clock_panel_box_);
    clock_button_.set_popover(clock_popover_);
}

void PanelWindow::configure_quick_panel() {
    quick_panel_title_label_.set_text(localization_.translate("quick_panel_title"));
    quick_panel_title_label_.add_css_class("quick-panel-title");
    quick_panel_subtitle_label_.set_text(localization_.translate("quick_panel_subtitle"));
    quick_panel_subtitle_label_.add_css_class("quick-panel-subtitle");
    quick_panel_header_box_.add_css_class("quick-panel-header");
    quick_panel_status_box_.add_css_class("quick-panel-status-box");
    quick_panel_controls_box_.add_css_class("quick-panel-controls-box");
    quick_panel_wifi_row_.add_css_class("quick-panel-control-row");
    quick_panel_bluetooth_row_.add_css_class("quick-panel-control-row");
    quick_panel_dnd_row_.add_css_class("quick-panel-control-row");
    quick_panel_audio_box_.add_css_class("quick-panel-audio-box");
    quick_panel_brightness_box_.add_css_class("quick-panel-brightness-box");
    quick_panel_networks_box_.add_css_class("quick-panel-device-list");
    quick_panel_audio_devices_box_.add_css_class("quick-panel-device-list");
    quick_panel_audio_sources_box_.add_css_class("quick-panel-device-list");
    quick_panel_bluetooth_devices_box_.add_css_class("quick-panel-device-list");
    quick_panel_actions_box_.add_css_class("quick-panel-actions-box");
    quick_panel_power_box_.add_css_class("quick-panel-power-box");
    quick_panel_box_.add_css_class("quick-panel-surface");

    quick_panel_network_label_.add_css_class("quick-panel-status-row");
    quick_panel_volume_label_.add_css_class("quick-panel-status-row");
    quick_panel_time_label_.add_css_class("quick-panel-status-row");
    quick_panel_brightness_label_.add_css_class("quick-panel-status-row");
    quick_panel_network_details_label_.add_css_class("quick-panel-network-details");
    quick_panel_network_details_label_.set_wrap(true);
    quick_panel_network_details_label_.set_xalign(0.0f);
    quick_panel_network_details_label_.set_hexpand(true);
    quick_panel_audio_slider_label_.add_css_class("quick-panel-status-row");
    quick_panel_wifi_row_label_.add_css_class("quick-panel-control-label");
    quick_panel_bluetooth_row_label_.add_css_class("quick-panel-control-label");
    quick_panel_dnd_row_label_.add_css_class("quick-panel-control-label");
    quick_panel_wifi_row_label_.set_xalign(0.0f);
    quick_panel_bluetooth_row_label_.set_xalign(0.0f);
    quick_panel_dnd_row_label_.set_xalign(0.0f);
    quick_panel_wifi_row_label_.set_hexpand(true);
    quick_panel_bluetooth_row_label_.set_hexpand(true);
    quick_panel_dnd_row_label_.set_hexpand(true);
    quick_panel_networks_title_label_.set_text(localization_.translate("quick_panel_networks"));
    quick_panel_networks_title_label_.add_css_class("quick-panel-section-title");
    quick_panel_audio_devices_title_label_.set_text(localization_.translate("quick_panel_audio_devices"));
    quick_panel_audio_devices_title_label_.add_css_class("quick-panel-section-title");
    quick_panel_audio_sources_title_label_.set_text(localization_.translate("quick_panel_audio_sources"));
    quick_panel_audio_sources_title_label_.add_css_class("quick-panel-section-title");
    quick_panel_bluetooth_devices_title_label_.set_text(localization_.translate("quick_panel_bluetooth_devices"));
    quick_panel_bluetooth_devices_title_label_.add_css_class("quick-panel-section-title");

    quick_panel_volume_scale_.set_draw_value(false);
    quick_panel_volume_scale_.set_range(0.0, 150.0);
    quick_panel_volume_scale_.set_increments(5.0, 10.0);
    quick_panel_volume_scale_.add_css_class("quick-panel-brightness-scale");
    quick_panel_volume_scale_.signal_value_changed().connect([this]() {
        if (volume_change_in_progress_) {
            return;
        }
        set_volume(static_cast<int>(std::lround(quick_panel_volume_scale_.get_value())));
    });

    quick_panel_brightness_scale_.set_draw_value(false);
    quick_panel_brightness_scale_.set_range(1.0, 100.0);
    quick_panel_brightness_scale_.set_increments(5.0, 10.0);
    quick_panel_brightness_scale_.add_css_class("quick-panel-brightness-scale");
    quick_panel_brightness_scale_.signal_value_changed().connect([this]() {
        if (brightness_change_in_progress_) {
            return;
        }
        set_brightness(static_cast<int>(std::lround(quick_panel_brightness_scale_.get_value())));
    });

    quick_panel_wifi_switch_.property_active().signal_changed().connect([this]() {
        if (quick_panel_switch_sync_in_progress_) {
            return;
        }
        toggle_wifi();
    });
    quick_panel_bluetooth_switch_.property_active().signal_changed().connect([this]() {
        if (quick_panel_switch_sync_in_progress_) {
            return;
        }
        toggle_bluetooth();
    });
    quick_panel_dnd_switch_.property_active().signal_changed().connect([this]() {
        if (quick_panel_switch_sync_in_progress_) {
            return;
        }
        toggle_do_not_disturb();
    });

    const auto configure_action_button = [this](Gtk::Button& button, const std::string& label, const char* css_class, const std::string& command_line, const char* icon_name = nullptr) {
        if (icon_name && icon_name[0] != '\0') {
            button.set_child(make_button_content(icon_name, label));
        } else {
            button.set_label(label);
        }
        button.add_css_class(css_class);
        button.signal_clicked().connect([this, command_line]() {
            if (launch_command(command_line)) {
                quick_panel_popover_.popdown();
            }
        });
    };

    configure_action_button(
        quick_panel_network_button_,
        localization_.translate("network"),
        "quick-panel-action",
        "sh -lc 'command -v nm-connection-editor >/dev/null 2>&1 && nm-connection-editor || hyalo-control-center'",
        "network-wireless-symbolic");
    configure_action_button(
        quick_panel_audio_button_,
        localization_.translate("audio"),
        "quick-panel-action",
        "sh -lc 'command -v pavucontrol >/dev/null 2>&1 && pavucontrol || hyalo-control-center'",
        "audio-speakers-symbolic");
    configure_action_button(
        quick_panel_settings_button_,
        localization_.translate("settings"),
        "quick-panel-action accent",
        "hyalo-control-center",
        "hyalo-control-center-symbolic");
    configure_action_button(
        quick_panel_logout_button_,
        localization_.translate("launcher_power_logout"),
        "quick-panel-power-action",
        logout_command(),
        "system-log-out-symbolic");
    configure_action_button(
        quick_panel_suspend_button_,
        localization_.translate("launcher_power_suspend"),
        "quick-panel-power-action",
        "systemctl suspend",
        "weather-clear-night-symbolic");
    configure_action_button(
        quick_panel_poweroff_button_,
        localization_.translate("launcher_power_poweroff"),
        "quick-panel-power-action danger",
        "systemctl poweroff",
        "system-shutdown-symbolic");

    quick_panel_header_box_.append(quick_panel_title_label_);
    quick_panel_header_box_.append(quick_panel_subtitle_label_);

    quick_panel_status_box_.append(quick_panel_network_label_);
    quick_panel_status_box_.append(quick_panel_volume_label_);
    quick_panel_status_box_.append(quick_panel_time_label_);

    quick_panel_wifi_row_.append(quick_panel_wifi_row_label_);
    quick_panel_wifi_row_.append(quick_panel_wifi_switch_);
    quick_panel_bluetooth_row_.append(quick_panel_bluetooth_row_label_);
    quick_panel_bluetooth_row_.append(quick_panel_bluetooth_switch_);
    quick_panel_dnd_row_.append(quick_panel_dnd_row_label_);
    quick_panel_dnd_row_.append(quick_panel_dnd_switch_);
    quick_panel_controls_box_.append(quick_panel_wifi_row_);
    quick_panel_controls_box_.append(quick_panel_bluetooth_row_);
    quick_panel_controls_box_.append(quick_panel_dnd_row_);

    quick_panel_audio_box_.append(quick_panel_audio_slider_label_);
    quick_panel_audio_box_.append(quick_panel_volume_scale_);

    quick_panel_box_.append(quick_panel_header_box_);
    quick_panel_box_.append(quick_panel_status_box_);
    quick_panel_box_.append(quick_panel_controls_box_);
    quick_panel_box_.append(quick_panel_audio_box_);
    quick_panel_brightness_box_.append(quick_panel_brightness_label_);
    quick_panel_brightness_box_.append(quick_panel_brightness_scale_);
    quick_panel_brightness_box_.append(quick_panel_network_details_label_);

    quick_panel_box_.append(quick_panel_brightness_box_);

    quick_panel_actions_box_.append(quick_panel_network_button_);
    quick_panel_actions_box_.append(quick_panel_audio_button_);
    quick_panel_actions_box_.append(quick_panel_settings_button_);

    quick_panel_power_box_.append(quick_panel_logout_button_);
    quick_panel_power_box_.append(quick_panel_suspend_button_);
    quick_panel_power_box_.append(quick_panel_poweroff_button_);

    quick_panel_box_.append(quick_panel_networks_title_label_);
    quick_panel_box_.append(quick_panel_networks_box_);
    quick_panel_box_.append(quick_panel_audio_devices_title_label_);
    quick_panel_box_.append(quick_panel_audio_devices_box_);
    quick_panel_box_.append(quick_panel_audio_sources_title_label_);
    quick_panel_box_.append(quick_panel_audio_sources_box_);
    quick_panel_box_.append(quick_panel_bluetooth_devices_title_label_);
    quick_panel_box_.append(quick_panel_bluetooth_devices_box_);
    quick_panel_box_.append(quick_panel_actions_box_);
    quick_panel_box_.append(quick_panel_power_box_);

    quick_panel_popover_.set_has_arrow(false);
    quick_panel_popover_.set_position(Gtk::PositionType::BOTTOM);
    quick_panel_popover_.set_autohide(true);
    quick_panel_popover_.add_css_class("quick-panel-popover");
    quick_panel_popover_.set_child(quick_panel_box_);
    quick_panel_popover_.signal_show().connect([this]() {
        request_quick_panel_refresh(true);
    });
    quick_panel_button_.set_popover(quick_panel_popover_);
}

void PanelWindow::configure_launcher() {
    launcher_box_.add_css_class("launcher-surface");
    launcher_box_.set_size_request(kLauncherWidth, -1);
    launcher_box_.set_overflow(Gtk::Overflow::HIDDEN);
    launcher_content_box_.set_overflow(Gtk::Overflow::HIDDEN);
    launcher_sidebar_box_.set_overflow(Gtk::Overflow::HIDDEN);
    launcher_main_box_.set_overflow(Gtk::Overflow::HIDDEN);
    launcher_header_box_.set_overflow(Gtk::Overflow::HIDDEN);
    launcher_pinned_box_.set_overflow(Gtk::Overflow::HIDDEN);
    launcher_list_box_.set_overflow(Gtk::Overflow::HIDDEN);
    launcher_scroll_.set_overflow(Gtk::Overflow::HIDDEN);

    launcher_avatar_label_.set_text(launcher_avatar_text());
    launcher_avatar_label_.set_halign(Gtk::Align::CENTER);
    launcher_avatar_label_.set_valign(Gtk::Align::CENTER);
    launcher_avatar_label_.add_css_class("launcher-avatar");

    launcher_title_label_.set_text(current_display_name());
    launcher_title_label_.set_halign(Gtk::Align::START);
    launcher_title_label_.add_css_class("launcher-title");
    launcher_title_label_.add_css_class("launcher-user-name");

    launcher_subtitle_label_.set_text("@" + current_user_name());
    launcher_subtitle_label_.set_halign(Gtk::Align::START);
    launcher_subtitle_label_.add_css_class("launcher-subtitle");
    launcher_subtitle_label_.add_css_class("launcher-user-handle");

    launcher_pinned_label_.set_text(localization_.translate("launcher_pinned"));
    launcher_pinned_label_.set_halign(Gtk::Align::START);
    launcher_pinned_label_.add_css_class("launcher-section-label");

    launcher_apps_title_label_.set_text(localization_.translate("launcher_all_apps"));
    launcher_apps_title_label_.set_halign(Gtk::Align::START);
    launcher_apps_title_label_.add_css_class("launcher-section-label");

    launcher_settings_button_.set_child(make_button_content("hyalo-control-center-symbolic", localization_.translate("settings"), 18));
    launcher_settings_button_.add_css_class("launcher-sidebar-action");
    launcher_settings_button_.signal_clicked().connect([this]() {
        handle_settings_activated();
        hide_launcher();
    });

    launcher_restore_hidden_button_.set_label(localization_.translate("launcher_restore_hidden"));
    launcher_restore_hidden_button_.add_css_class("launcher-sidebar-secondary-action");
    launcher_restore_hidden_button_.signal_clicked().connect([this]() {
        restore_hidden_launcher_entries();
    });

    const auto configure_power_button = [this](Gtk::Button& button, const char* icon_name, const std::string& tooltip, const std::function<void()>& action) {
        button.add_css_class("launcher-power-button");
        button.set_tooltip_text(tooltip);

        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(icon_name);
        icon->set_pixel_size(18);
        icon->add_css_class("launcher-power-icon");
        button.set_child(*icon);

        button.signal_clicked().connect(action);
    };

    configure_power_button(
        launcher_logout_button_,
        "system-log-out-symbolic",
        localization_.translate("launcher_power_logout"),
        [this]() {
            present_launcher_power_dialog(true);
        });
    configure_power_button(
        launcher_suspend_button_,
        "weather-clear-night-symbolic",
        localization_.translate("launcher_power_suspend"),
        [this]() {
            if (launch_command("systemctl suspend")) {
                hide_launcher();
            }
        });
    configure_power_button(
        launcher_poweroff_button_,
        "system-shutdown-symbolic",
        localization_.translate("launcher_power_poweroff"),
        [this]() {
            present_launcher_power_dialog(false);
        });

    launcher_identity_box_.append(launcher_title_label_);
    launcher_identity_box_.append(launcher_subtitle_label_);
    launcher_profile_box_.add_css_class("launcher-profile-box");
    launcher_profile_box_.append(launcher_avatar_label_);
    launcher_profile_box_.append(launcher_identity_box_);
    launcher_power_box_.add_css_class("launcher-power-row");
    launcher_power_box_.append(launcher_logout_button_);
    launcher_power_box_.append(launcher_suspend_button_);
    launcher_power_box_.append(launcher_poweroff_button_);
    launcher_header_box_.append(launcher_profile_box_);
    launcher_header_box_.append(launcher_power_box_);

    launcher_sidebar_box_.add_css_class("launcher-sidebar");
    launcher_header_box_.add_css_class("launcher-hero");
    launcher_main_box_.add_css_class("launcher-main-pane");
    launcher_sidebar_box_.set_size_request(232, -1);
    launcher_sidebar_box_.set_vexpand(true);
    launcher_main_box_.set_hexpand(true);
    launcher_main_box_.set_vexpand(true);
    launcher_box_.set_vexpand(true);
    launcher_content_box_.set_vexpand(true);

    launcher_search_entry_.set_placeholder_text(localization_.translate("launcher_search"));
    launcher_search_entry_.add_css_class("launcher-search");
    launcher_search_entry_.signal_search_changed().connect([this]() {
        request_launcher_refresh();
    });
    launcher_search_entry_.signal_activate().connect([this]() {
        if (!launcher_rows_.empty()) {
            launcher_rows_.front()->activate();
        }
    });

    launcher_empty_label_.set_halign(Gtk::Align::START);
    launcher_empty_label_.add_css_class("launcher-empty");

    launcher_filter_box_.add_css_class("launcher-filters");
    launcher_pinned_box_.add_css_class("launcher-pinned-row");
    launcher_pinned_box_.add_css_class("launcher-pinned-list");
    launcher_list_box_.add_css_class("launcher-app-list");
    launcher_list_box_.set_vexpand(true);
    build_launcher_filter_buttons();

    launcher_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    launcher_scroll_.add_css_class("launcher-results");
    launcher_scroll_.set_min_content_width(460);
    launcher_scroll_.set_max_content_height(kLauncherMaxVisibleHeight);
    launcher_scroll_.set_vexpand(true);
    launcher_scroll_.set_hexpand(true);
    launcher_scroll_.set_child(launcher_list_box_);

    launcher_sidebar_box_.append(launcher_header_box_);
    launcher_sidebar_box_.append(launcher_settings_button_);
    launcher_sidebar_box_.append(launcher_restore_hidden_button_);
    launcher_sidebar_box_.append(launcher_pinned_label_);
    launcher_sidebar_box_.append(launcher_pinned_box_);

    launcher_main_box_.append(launcher_search_entry_);
    launcher_main_box_.append(launcher_filter_box_);
    launcher_main_box_.append(launcher_apps_title_label_);

    launcher_scroll_overlay_.set_child(launcher_scroll_);
    launcher_scroll_overlay_.set_vexpand(true);
    launcher_scroll_overlay_.set_hexpand(true);
    launcher_context_menu_.set_visible(false);
    launcher_context_menu_.set_halign(Gtk::Align::END);
    launcher_context_menu_.set_valign(Gtk::Align::START);
    launcher_context_menu_.add_css_class("launcher-context-menu");
    launcher_scroll_overlay_.add_overlay(launcher_context_menu_);
    launcher_scroll_overlay_.set_clip_overlay(launcher_context_menu_, false);

    launcher_main_box_.append(launcher_scroll_overlay_);
    launcher_main_box_.append(launcher_empty_label_);

    launcher_content_box_.append(launcher_sidebar_box_);
    launcher_content_box_.append(launcher_main_box_);
    launcher_box_.append(launcher_content_box_);

    launcher_window_.set_title("Hyalo Launcher");
    launcher_window_.set_name("hyalo-launcher");
    launcher_window_.set_decorated(false);
    launcher_window_.set_resizable(false);
    launcher_window_.add_css_class("launcher-window");
    launcher_window_.set_child(launcher_box_);

    load_launcher_entries();
    launcher_loaded_ = true;

    auto launcher_key_controller = Gtk::EventControllerKey::create();
    launcher_key_controller->signal_key_pressed().connect([this](guint keyval, guint, Gdk::ModifierType) {
        if (keyval == GDK_KEY_Escape) {
            hide_launcher();
            return true;
        }
        return false;
    }, false);
    launcher_window_.add_controller(launcher_key_controller);

    auto launcher_focus_controller = Gtk::EventControllerFocus::create();
    launcher_focus_controller->signal_leave().connect([this]() {
        if (launcher_window_.is_visible() && !any_launcher_popover_visible()) {
            hide_launcher();
        }
    });
    launcher_window_.add_controller(launcher_focus_controller);

    launcher_window_.property_is_active().signal_changed().connect([this]() {
        if (!launcher_window_.is_visible()) {
            return;
        }

        if (!gtk_window_is_active(GTK_WINDOW(launcher_window_.gobj()))
            && !any_launcher_popover_visible()) {
            hide_launcher();
        }
    });
}

void PanelWindow::present_launcher_power_dialog(bool logout_flow) {
    if (launcher_window_.is_visible()) {
        hide_launcher();
    }

    auto* win = new Gtk::Window();
    win->set_modal(true);
    win->set_hide_on_close(true);
    win->set_decorated(false);
    win->set_resizable(false);
    win->set_default_size(460, -1);
    win->add_css_class("launcher-power-modal");
    win->set_title(localization_.translate(
        logout_flow ? "launcher_power_dialog_logout_title"
                    : "launcher_power_dialog_shutdown_title"));
    if (launcher_window_.is_visible()) {
        win->set_transient_for(launcher_window_);
    } else {
        win->set_transient_for(*this);
    }

    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    root->add_css_class("launcher-power-modal-surface");
    win->set_child(*root);

    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name(logout_flow ? "system-log-out-symbolic"
                                         : "system-shutdown-symbolic");
    icon->set_pixel_size(22);
    icon->add_css_class("launcher-power-modal-icon");
    root->append(*icon);

    auto* title = Gtk::make_managed<Gtk::Label>(localization_.translate(
        logout_flow ? "launcher_power_dialog_logout_title"
                    : "launcher_power_dialog_shutdown_title"));
    title->set_xalign(0.0f);
    title->set_wrap(true);
    title->add_css_class("launcher-power-modal-title");
    root->append(*title);

    auto* description = Gtk::make_managed<Gtk::Label>(localization_.translate(
        logout_flow ? "launcher_power_dialog_logout_body"
                    : "launcher_power_dialog_shutdown_body"));
    description->set_wrap(true);
    description->set_xalign(0.0f);
    description->add_css_class("launcher-power-modal-subtitle");
    root->append(*description);

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    btn_box->set_homogeneous(true);
    btn_box->set_margin_top(8);

    auto* primary_btn = Gtk::make_managed<Gtk::Button>(localization_.translate(
        logout_flow ? "launcher_power_logout" : "launcher_power_reboot"));
    primary_btn->add_css_class("hyalo-button");
    primary_btn->add_css_class("hyalo-button-primary");
    primary_btn->add_css_class("launcher-power-modal-action");
    primary_btn->add_css_class("launcher-power-modal-primary");

    auto* danger_btn = Gtk::make_managed<Gtk::Button>(localization_.translate(
        logout_flow ? "launcher_power_suspend" : "launcher_power_poweroff"));
    danger_btn->add_css_class("hyalo-button");
    danger_btn->add_css_class("hyalo-button-danger");
    danger_btn->add_css_class("launcher-power-modal-action");
    danger_btn->add_css_class("launcher-power-modal-danger");

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>(
        localization_.translate("cc_action_cancel"));
    cancel_btn->add_css_class("hyalo-button");
    cancel_btn->add_css_class("hyalo-button-secondary");
    cancel_btn->add_css_class("launcher-power-modal-action");
    cancel_btn->add_css_class("launcher-power-modal-cancel");

    btn_box->append(*primary_btn);
    btn_box->append(*danger_btn);
    btn_box->append(*cancel_btn);
    root->append(*btn_box);

    primary_btn->signal_clicked().connect([this, win, logout_flow]() {
        auto command = std::string{};
        if (logout_flow) {
            command = logout_command();
        } else {
            command = "systemctl reboot";
        }
        if (!command.empty() && launch_command(command)) {
            hide_launcher();
        }
        win->close();
    });

    danger_btn->signal_clicked().connect([this, win, logout_flow]() {
        auto command = std::string{};
        if (logout_flow) {
            command = "systemctl suspend";
        } else {
            command = "systemctl poweroff";
        }
        if (!command.empty() && launch_command(command)) {
            hide_launcher();
        }
        win->close();
    });

    cancel_btn->signal_clicked().connect([win]() {
        win->close();
    });

    win->signal_hide().connect([win]() {
        delete win;
    });

    win->present();
}

void PanelWindow::show_launcher() {
    launcher_refresh_suppressed_ = true;
    launcher_refresh_pending_ = false;

    if (!launcher_search_entry_.get_text().empty()) {
        launcher_search_entry_.set_text("");
    }
    if (active_launcher_filter_ != "all") {
        set_launcher_filter("all");
    }

    launcher_refresh_suppressed_ = false;
    const auto requires_refresh = launcher_refresh_pending_ || launcher_rows_.empty();
    launcher_refresh_pending_ = false;

    configure_launcher_window();
    launcher_window_.present();
    launcher_window_.grab_focus();
    launcher_search_entry_.grab_focus();
    launcher_inactive_ticks_ = 0;
    log_panel_diagnostic(diagnostics_enabled_, "launcher: show + focus request");

    if (requires_refresh) {
        request_launcher_refresh();
    }
}

void PanelWindow::hide_launcher() {
    clear_launcher_context_popovers();
    launcher_window_.hide();
    launcher_inactive_ticks_ = 0;
    log_panel_diagnostic(diagnostics_enabled_, "launcher: hide");
}

void PanelWindow::toggle_launcher() {
    if (launcher_window_.is_visible()) {
        hide_launcher();
    } else {
        show_launcher();
    }
}

void PanelWindow::load_launcher_state() {
    launcher_favorite_ids_.clear();
    launcher_hidden_ids_.clear();

    const auto path = launcher_state_path(config_manager_);
    std::ifstream input(path);
    if (!input.is_open()) {
        for (std::size_t i = 0; i < std::min(kLauncherDefaultPinnedCount, kPinnedDesktopIds.size()); ++i) {
            launcher_favorite_ids_.insert(std::string(kPinnedDesktopIds[i]));
        }
        save_launcher_state();
        return;
    }

    nlohmann::json state;
    try {
        input >> state;
    } catch (...) {
        state = nlohmann::json::object();
    }

    if (const auto favorites = state.find("favorites"); favorites != state.end() && favorites->is_array()) {
        for (const auto& entry : *favorites) {
            if (entry.is_string()) {
                launcher_favorite_ids_.insert(entry.get<std::string>());
            }
        }
    }

    if (const auto hidden = state.find("hidden"); hidden != state.end() && hidden->is_array()) {
        for (const auto& entry : *hidden) {
            if (entry.is_string()) {
                launcher_hidden_ids_.insert(entry.get<std::string>());
            }
        }
    }

    if (launcher_favorite_ids_.empty()) {
        for (std::size_t i = 0; i < std::min(kLauncherDefaultPinnedCount, kPinnedDesktopIds.size()); ++i) {
            launcher_favorite_ids_.insert(std::string(kPinnedDesktopIds[i]));
        }
        save_launcher_state();
    }
}

void PanelWindow::save_launcher_state() const {
    const auto path = launcher_state_path(config_manager_);
    std::filesystem::create_directories(path.parent_path());

    auto favorites = std::vector<std::string>(launcher_favorite_ids_.begin(), launcher_favorite_ids_.end());
    auto hidden = std::vector<std::string>(launcher_hidden_ids_.begin(), launcher_hidden_ids_.end());
    std::sort(favorites.begin(), favorites.end());
    std::sort(hidden.begin(), hidden.end());

    const auto state = nlohmann::json{{"favorites", favorites}, {"hidden", hidden}};
    std::ofstream output(path);
    output << state.dump(2) << '\n';
}

void PanelWindow::build_launcher_filter_buttons() {
    for (const auto& definition : kLauncherFilters) {
        auto* button = Gtk::make_managed<Gtk::Button>(localization_.translate(std::string(definition.translation_key)));
        button->add_css_class("launcher-filter");
        button->signal_clicked().connect([this, key = std::string(definition.key)]() {
            set_launcher_filter(key);
        });
        launcher_filter_box_.append(*button);
        launcher_filter_buttons_.push_back(button);
    }

    update_launcher_filter_buttons();
}

void PanelWindow::load_launcher_entries() {
    launcher_entries_.clear();

    auto seen_ids = std::unordered_set<std::string>{};
    const auto directories = desktop_entry_directories();

    for (const auto& directory : directories) {
        if (!std::filesystem::exists(directory)) {
            continue;
        }

        std::error_code error_code;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 directory,
                 std::filesystem::directory_options::skip_permission_denied,
                 error_code)) {
            if (error_code || !entry.is_regular_file()) {
                continue;
            }

            if (entry.path().extension() != ".desktop") {
                continue;
            }

            auto* app_info = g_desktop_app_info_new_from_filename(entry.path().c_str());
            if (!app_info) {
                continue;
            }

            auto* base_info = G_APP_INFO(app_info);
            if (!g_app_info_should_show(base_info)) {
                g_object_unref(app_info);
                continue;
            }

            const auto desktop_id = make_valid_utf8(
                g_app_info_get_id(base_info) ? g_app_info_get_id(base_info) : entry.path().filename().string());
            if (!seen_ids.insert(desktop_id).second) {
                g_object_unref(app_info);
                continue;
            }

            const auto name = make_valid_utf8(
                g_app_info_get_display_name(base_info) ? g_app_info_get_display_name(base_info) : g_app_info_get_name(base_info));
            const auto description = make_valid_utf8(
                g_app_info_get_description(base_info) ? g_app_info_get_description(base_info) : "");
            const auto generic_name = make_valid_utf8(
                g_desktop_app_info_get_generic_name(app_info) ? g_desktop_app_info_get_generic_name(app_info) : "");
            const auto executable = make_valid_utf8(
                g_app_info_get_executable(base_info) ? g_app_info_get_executable(base_info) : "");
            const auto categories = make_valid_utf8(
                g_desktop_app_info_get_categories(app_info) ? g_desktop_app_info_get_categories(app_info) : "");
            const auto category = launcher_category_from_desktop_entry(categories);
            const auto icon_name = remap_icon_name(
                launcher_icon_name(g_app_info_get_icon(base_info)),
                desktop_id,
                name,
                category
            );

            auto search_blob = std::string{};
            append_search_part(search_blob, name);
            append_search_part(search_blob, description);
            append_search_part(search_blob, generic_name);
            append_search_part(search_blob, executable);
            append_search_part(search_blob, desktop_id);
            append_search_part(search_blob, categories);
            append_search_part(search_blob, category);

            launcher_entries_.push_back(LauncherEntry{
                .desktop_id = desktop_id,
                .desktop_file = entry.path().string(),
                .name = name,
                .description = description.empty() ? generic_name : description,
                .executable = executable,
                .icon_name = icon_name,
                .category = category,
                .search_blob = std::move(search_blob),
                .pinned = is_pinned_launcher(desktop_id),
            });

            g_object_unref(app_info);
        }
    }

    std::sort(launcher_entries_.begin(), launcher_entries_.end(), [](const auto& left, const auto& right) {
        if (left.pinned != right.pinned) {
            return left.pinned > right.pinned;
        }
        return casefold(left.name) < casefold(right.name);
    });
}

void PanelWindow::clear_launcher_pinned_rows() {
    for (auto* row : launcher_pinned_rows_) {
        launcher_pinned_box_.remove(*row);
    }
    launcher_pinned_rows_.clear();
}

void PanelWindow::clear_launcher_rows() {
    for (auto* row : launcher_rows_) {
        launcher_list_box_.remove(*row);
    }
    launcher_rows_.clear();
}

void PanelWindow::clear_launcher_context_popovers() {
    launcher_context_menu_.set_visible(false);
    while (auto* child = launcher_context_menu_.get_first_child()) {
        launcher_context_menu_.remove(*child);
    }
}

bool PanelWindow::any_launcher_popover_visible() const {
    return launcher_context_menu_.is_visible();
}

void PanelWindow::clear_task_group_popovers() {
    for (auto* popover : task_group_popovers_) {
        if (popover) {
            popover->unparent();
        }
    }
    task_group_popovers_.clear();
}

void PanelWindow::set_launcher_filter(const std::string& filter_key) {
    if (active_launcher_filter_ == filter_key) {
        return;
    }

    active_launcher_filter_ = filter_key;
    update_launcher_filter_buttons();
    request_launcher_refresh();
}

void PanelWindow::request_launcher_refresh() {
    if (!launcher_window_.is_visible()) {
        launcher_refresh_pending_ = true;
        return;
    }

    if (launcher_refresh_suppressed_) {
        launcher_refresh_pending_ = true;
        return;
    }

    if (launcher_refresh_scheduled_) {
        launcher_refresh_pending_ = true;
        return;
    }

    launcher_refresh_scheduled_ = true;
    Glib::signal_timeout().connect_once([this]() {
        launcher_refresh_scheduled_ = false;

        if (launcher_refresh_suppressed_) {
            launcher_refresh_pending_ = true;
            return;
        }

        launcher_refresh_pending_ = false;
        refresh_launcher_results();
    }, 16);
}

void PanelWindow::update_launcher_filter_buttons() {
    for (std::size_t index = 0; index < launcher_filter_buttons_.size() && index < kLauncherFilters.size(); ++index) {
        if (active_launcher_filter_ == kLauncherFilters[index].key) {
            launcher_filter_buttons_[index]->add_css_class("active-launcher-filter");
        } else {
            launcher_filter_buttons_[index]->remove_css_class("active-launcher-filter");
        }
    }
}

bool PanelWindow::is_launcher_favorite(const LauncherEntry& entry) const {
    return launcher_favorite_ids_.contains(entry.desktop_id);
}

void PanelWindow::toggle_launcher_favorite(const LauncherEntry& entry) {
    if (launcher_favorite_ids_.contains(entry.desktop_id)) {
        launcher_favorite_ids_.erase(entry.desktop_id);
    } else {
        if (launcher_favorite_ids_.size() >= kLauncherMaxPinnedEntries) {
            return;
        }
        launcher_favorite_ids_.insert(entry.desktop_id);
    }

    save_launcher_state();
    request_launcher_refresh();
}

void PanelWindow::hide_launcher_entry(const LauncherEntry& entry) {
    launcher_hidden_ids_.insert(entry.desktop_id);
    launcher_favorite_ids_.erase(entry.desktop_id);
    save_launcher_state();
    request_launcher_refresh();
}

void PanelWindow::restore_hidden_launcher_entries() {
    launcher_hidden_ids_.clear();
    save_launcher_state();
    request_launcher_refresh();
}

bool PanelWindow::matches_launcher_filter(const LauncherEntry& entry) const {
    return active_launcher_filter_ == "all" || entry.category == active_launcher_filter_;
}

Gtk::Button* PanelWindow::create_launcher_entry_button(const LauncherEntry& entry, bool compact) {
    auto* button = Gtk::make_managed<Gtk::Button>();
    button->set_hexpand(true);
    button->set_halign(Gtk::Align::FILL);
    auto tooltip = entry.description.empty() ? entry.name : entry.name + " - " + entry.description;
    if (!entry.executable.empty()) {
        const auto executable_folded = casefold(entry.executable);
        if (casefold(tooltip).find(executable_folded) == std::string::npos) {
            tooltip += " [" + entry.executable + "]";
        }
    }
    button->set_tooltip_text(tooltip);
    button->add_css_class(compact ? "launcher-pinned-entry" : "launcher-entry");

    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, compact ? 10 : 12);
    content->add_css_class(compact ? "launcher-pinned-content" : "launcher-entry-content");

    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name(entry.icon_name);
    icon->set_pixel_size(compact ? 22 : 24);
    icon->add_css_class(compact ? "launcher-pinned-icon" : "launcher-entry-icon");
    content->append(*icon);

    if (compact) {
        auto* title = Gtk::make_managed<Gtk::Label>(compact_label(entry.name, 20));
        title->set_halign(Gtk::Align::START);
        title->set_xalign(0.0f);
        title->add_css_class("launcher-pinned-title");
        content->append(*title);
    } else {
        auto* text_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        text_box->set_hexpand(true);

        auto* title = Gtk::make_managed<Gtk::Label>(compact_label(entry.name, 42));
        title->set_halign(Gtk::Align::START);
        title->set_xalign(0.0f);
        title->add_css_class("launcher-entry-title");

        auto secondary = entry.description.empty()
            ? launcher_category_label(entry.category, localization_)
            : entry.description;
        if (!entry.executable.empty()) {
            const auto executable_folded = casefold(entry.executable);
            if (casefold(entry.name).find(executable_folded) == std::string::npos
                && casefold(secondary).find(executable_folded) == std::string::npos) {
                if (!secondary.empty()) {
                    secondary += " · ";
                }
                secondary += entry.executable;
            }
        }
        secondary = compact_label(secondary, 56);
        auto* subtitle = Gtk::make_managed<Gtk::Label>(secondary);
        subtitle->set_halign(Gtk::Align::START);
        subtitle->set_xalign(0.0f);
        subtitle->add_css_class("launcher-entry-subtitle");

        text_box->append(*title);
        text_box->append(*subtitle);
        content->append(*text_box);

        auto* badge = Gtk::make_managed<Gtk::Label>(launcher_category_label(entry.category, localization_));
        badge->add_css_class("launcher-entry-badge");
        content->append(*badge);
    }

    button->set_child(*content);
    button->signal_clicked().connect([this, entry]() {
        handle_launcher_activated(entry);
    });

    auto right_click = Gtk::GestureClick::create();
    right_click->set_button(3);
    right_click->signal_pressed().connect([this, entry, button](int, double, double) {
        show_launcher_context_menu(entry, *button);
    });
    button->add_controller(right_click);

    return button;
}

void PanelWindow::show_launcher_context_menu(const LauncherEntry& entry, Gtk::Widget& anchor) {
    clear_launcher_context_popovers();
    launcher_context_entry_ = entry;

    int y_offset = 0;
    graphene_point_t in_pt = GRAPHENE_POINT_INIT(0.f, 0.f);
    graphene_point_t out_pt;
    if (gtk_widget_compute_point(anchor.gobj(), GTK_WIDGET(launcher_scroll_overlay_.gobj()), &in_pt, &out_pt)) {
        y_offset = std::max(0, static_cast<int>(out_pt.y));
    }
    launcher_context_menu_.set_margin_top(y_offset);

    auto* open_btn = Gtk::make_managed<Gtk::Button>(localization_.translate("launcher_action_open"));
    open_btn->add_css_class("launcher-context-action");
    open_btn->signal_clicked().connect([this]() {
        auto e = launcher_context_entry_;
        clear_launcher_context_popovers();
        handle_launcher_activated(e);
    });

    const auto fav_label = is_launcher_favorite(entry)
        ? localization_.translate("launcher_action_remove_favorite")
        : localization_.translate("launcher_action_add_favorite");
    auto* fav_btn = Gtk::make_managed<Gtk::Button>(fav_label);
    fav_btn->add_css_class("launcher-context-action");
    fav_btn->signal_clicked().connect([this]() {
        auto e = launcher_context_entry_;
        clear_launcher_context_popovers();
        toggle_launcher_favorite(e);
    });

    auto* hide_btn = Gtk::make_managed<Gtk::Button>(localization_.translate("launcher_action_hide"));
    hide_btn->add_css_class("launcher-context-action");
    hide_btn->add_css_class("launcher-context-danger");
    hide_btn->signal_clicked().connect([this]() {
        auto e = launcher_context_entry_;
        clear_launcher_context_popovers();
        hide_launcher_entry(e);
    });

    launcher_context_menu_.append(*open_btn);
    launcher_context_menu_.append(*fav_btn);
    launcher_context_menu_.append(*hide_btn);
    launcher_context_menu_.set_visible(true);
}

PanelWindow::QuickPanelSnapshot PanelWindow::build_quick_panel_snapshot(bool detailed) const {
    auto snapshot = QuickPanelSnapshot{};
    snapshot.detailed = detailed;

    auto& network_info_ = snapshot.network_info;
    auto& network_access_points_ = snapshot.network_access_points;
    auto& audio_info_ = snapshot.audio_info;
    auto& audio_devices_ = snapshot.audio_devices;
    auto& audio_sources_ = snapshot.audio_sources;
    auto& bluetooth_devices_ = snapshot.bluetooth_devices;
    auto& brightness_info_ = snapshot.brightness_info;
    auto& do_not_disturb_info_ = snapshot.do_not_disturb_info;

    if (command_exists("nmcli")) {
        network_info_.available = true;

        const auto wifi_state = command_output("sh -lc 'nmcli radio wifi 2>/dev/null'");
        network_info_.wifi_enabled = wifi_state.has_value() && casefold(*wifi_state).find("enabled") != std::string::npos;

        const auto device_status = command_output("sh -lc 'nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status 2>/dev/null'");
        if (device_status) {
            for (const auto& line : split_lines(*device_status)) {
                const auto fields = split_nmcli_fields(line);
                if (fields.size() < 4) {
                    continue;
                }

                const auto device = trim_copy(fields[0]);
                const auto type = trim_copy(fields[1]);
                const auto state = trim_copy(fields[2]);
                const auto connection = trim_copy(fields[3]);

                const auto normalized_type = casefold(type);
                const auto normalized_state = casefold(state);
                if (normalized_state.find("connected") == std::string::npos) {
                    continue;
                }

                if (normalized_type == "wifi") {
                    network_info_.active_ssid = make_valid_utf8(connection.empty() ? device : connection);
                    network_info_.summary = "Wi-Fi: " + network_info_.active_ssid;
                    break;
                }

                if (normalized_type == "ethernet") {
                    network_info_.summary = localization_.translate("network_ethernet") + ": "
                        + make_valid_utf8(connection.empty() ? device : connection);
                }
            }
        }

        if (detailed) {
            const auto wifi_list = command_output("sh -lc 'nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list --rescan no 2>/dev/null'");
            if (wifi_list) {
            auto access_points_by_ssid = std::unordered_map<std::string, NetworkAccessPointInfo>{};
            for (const auto& line : split_lines(*wifi_list)) {
                const auto fields = split_nmcli_fields(line);
                if (fields.size() < 4) {
                    continue;
                }

                const auto active = trim_copy(fields[0]) == "*";
                auto ssid = trim_copy(fields[1]);
                auto security = trim_copy(fields[3]);
                const auto signal = parse_first_percentage(trim_copy(fields[2]) + "%").value_or(-1);

                if (ssid.empty()) {
                    ssid = localization_.translate("quick_panel_network_hidden");
                }
                if (security.empty() || security == "--") {
                    security = localization_.translate("quick_panel_network_open");
                }

                auto entry = NetworkAccessPointInfo{
                    .ssid = make_valid_utf8(ssid),
                    .security = make_valid_utf8(security),
                    .signal_percent = signal,
                    .active = active,
                };

                auto key = casefold(entry.ssid);
                auto iterator = access_points_by_ssid.find(key);
                if (iterator == access_points_by_ssid.end()) {
                    access_points_by_ssid.emplace(std::move(key), std::move(entry));
                    continue;
                }

                if (entry.active || entry.signal_percent > iterator->second.signal_percent) {
                    iterator->second = std::move(entry);
                }
            }

            for (auto& [_, access_point] : access_points_by_ssid) {
                if (access_point.active) {
                    network_info_.active_ssid = access_point.ssid;
                    network_info_.active_security = access_point.security;
                    network_info_.active_signal_percent = access_point.signal_percent;
                }
                network_access_points_.push_back(std::move(access_point));
            }

            std::sort(network_access_points_.begin(), network_access_points_.end(), [](const auto& left, const auto& right) {
                if (left.active != right.active) {
                    return left.active > right.active;
                }
                if (left.signal_percent != right.signal_percent) {
                    return left.signal_percent > right.signal_percent;
                }
                return casefold(left.ssid) < casefold(right.ssid);
            });
            }
        }

        if (!network_info_.active_ssid.empty()) {
            network_info_.summary = "Wi-Fi: " + network_info_.active_ssid;
            if (network_info_.active_signal_percent >= 0) {
                network_info_.summary += " · " + std::to_string(network_info_.active_signal_percent) + "%";
            }
        }

        if (network_info_.summary.empty()) {
            network_info_.summary = network_info_.wifi_enabled
                ? localization_.translate("network_disconnected")
                : localization_.translate("network_off");
        }
    } else {
        network_info_.summary = localization_.translate("state_unavailable");
    }

    if (command_exists("bluetoothctl")) {
        const auto bluetooth_state = command_output("sh -lc 'bluetoothctl show 2>/dev/null | grep Powered: | head -n1'");
        network_info_.bluetooth_enabled = bluetooth_state.has_value()
            && casefold(*bluetooth_state).find("yes") != std::string::npos;

        if (detailed) {
            auto device_map = std::unordered_map<std::string, BluetoothDeviceInfo>{};
            if (const auto paired = command_output("sh -lc 'bluetoothctl paired-devices 2>/dev/null'"); paired) {
                for (const auto& line : split_lines(*paired)) {
                    const auto parsed = parse_bluetooth_device_line(line);
                    if (!parsed) {
                        continue;
                    }
                    auto& device = device_map[parsed->first];
                    device.address = parsed->first;
                    device.name = parsed->second;
                    device.paired = true;
                }
            }
            if (const auto connected = command_output("sh -lc 'bluetoothctl devices Connected 2>/dev/null'"); connected) {
                for (const auto& line : split_lines(*connected)) {
                    const auto parsed = parse_bluetooth_device_line(line);
                    if (!parsed) {
                        continue;
                    }
                    auto& device = device_map[parsed->first];
                    device.address = parsed->first;
                    device.name = parsed->second;
                    device.connected = true;
                }
            }
            if (device_map.empty()) {
                if (const auto devices = command_output("sh -lc 'bluetoothctl devices 2>/dev/null'"); devices) {
                    for (const auto& line : split_lines(*devices)) {
                        const auto parsed = parse_bluetooth_device_line(line);
                        if (!parsed) {
                            continue;
                        }
                        auto& device = device_map[parsed->first];
                        device.address = parsed->first;
                        device.name = parsed->second;
                    }
                }
            }

            for (auto& [_, device] : device_map) {
                if (const auto info = command_output("sh -lc 'bluetoothctl info " + device.address + " 2>/dev/null'"); info) {
                    device.battery_percent = parse_bluetooth_battery_percent(*info).value_or(-1);
                }
                bluetooth_devices_.push_back(std::move(device));
            }
            std::sort(bluetooth_devices_.begin(), bluetooth_devices_.end(), [](const auto& left, const auto& right) {
                if (left.connected != right.connected) {
                    return left.connected > right.connected;
                }
                if (left.paired != right.paired) {
                    return left.paired > right.paired;
                }
                return casefold(left.name) < casefold(right.name);
            });
        }
    }

    if (command_exists("wpctl")) {
        if (detailed) {
            if (const auto status = command_output("sh -lc 'wpctl status 2>/dev/null'"); status) {
            auto in_sinks = false;
            auto in_sources = false;
            for (const auto& raw_line : split_lines(*status)) {
                const auto line = strip_status_glyphs(raw_line);
                if (line == "Sinks:") {
                    in_sinks = true;
                    in_sources = false;
                    continue;
                }
                if (line == "Sources:") {
                    in_sinks = false;
                    in_sources = true;
                    continue;
                }
                if ((in_sinks || in_sources) && (line == "Filters:" || line == "Streams:" || line == "Video" || line == "Settings")) {
                    break;
                }
                if ((!in_sinks && !in_sources) || line.empty()) {
                    continue;
                }

                auto entry_line = line;
                auto is_default = false;
                if (!entry_line.empty() && entry_line.front() == '*') {
                    is_default = true;
                    entry_line = trim_copy(entry_line.substr(1));
                }

                const auto dot = entry_line.find('.');
                if (dot == std::string::npos) {
                    continue;
                }

                const auto identifier = trim_copy(entry_line.substr(0, dot));
                auto name = trim_copy(entry_line.substr(dot + 1));
                if (const auto volume_marker = name.find("[vol:"); volume_marker != std::string::npos) {
                    name = trim_copy(name.substr(0, volume_marker));
                }
                if (identifier.empty() || name.empty()) {
                    continue;
                }

                if (in_sinks) {
                    audio_devices_.push_back(AudioDeviceInfo{
                        .id = identifier,
                        .name = make_valid_utf8(name),
                        .is_default = is_default,
                    });
                } else if (in_sources) {
                    audio_sources_.push_back(AudioSourceInfo{
                        .id = identifier,
                        .name = make_valid_utf8(name),
                        .is_default = is_default,
                    });
                }
            }
            }
        }

        const auto volume = command_output("sh -lc 'wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null'");
        if (volume) {
            audio_info_.available = true;
            audio_info_.muted = volume->find("[MUTED]") != std::string::npos;

            const auto marker = volume->find("Volume:");
            if (marker != std::string::npos) {
                auto value_stream = std::stringstream(volume->substr(marker + 7));
                auto normalized = 0.0;
                value_stream >> normalized;
                audio_info_.volume_percent = std::clamp(static_cast<int>(std::lround(normalized * 100.0)), 0, 150);
            }
        }
    } else if (command_exists("pactl")) {
        if (detailed) {
            const auto default_sink = command_output("sh -lc 'pactl get-default-sink 2>/dev/null'");
            const auto default_source = command_output("sh -lc 'pactl get-default-source 2>/dev/null'");
            const auto sinks = command_output("sh -lc 'pactl list short sinks 2>/dev/null'");
            const auto sources = command_output("sh -lc 'pactl list short sources 2>/dev/null'");
            if (sinks) {
            for (const auto& line : split_lines(*sinks)) {
                auto stream = std::stringstream(line);
                auto numeric_id = std::string{};
                auto sink_name = std::string{};
                std::getline(stream, numeric_id, '\t');
                std::getline(stream, sink_name, '\t');
                if (numeric_id.empty() || sink_name.empty()) {
                    continue;
                }

                audio_devices_.push_back(AudioDeviceInfo{
                    .id = make_valid_utf8(sink_name),
                    .name = make_valid_utf8(sink_name),
                    .is_default = default_sink.has_value() && trim_copy(*default_sink) == sink_name,
                });
            }
            }
            if (sources) {
            for (const auto& line : split_lines(*sources)) {
                auto stream = std::stringstream(line);
                auto numeric_id = std::string{};
                auto source_name = std::string{};
                std::getline(stream, numeric_id, '\t');
                std::getline(stream, source_name, '\t');
                if (numeric_id.empty() || source_name.empty()) {
                    continue;
                }

                audio_sources_.push_back(AudioSourceInfo{
                    .id = make_valid_utf8(source_name),
                    .name = make_valid_utf8(source_name),
                    .is_default = default_source.has_value() && trim_copy(*default_source) == source_name,
                });
            }
            }
        }

        const auto volume = command_output("sh -lc 'pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | head -n1'");
        const auto mute = command_output("sh -lc 'pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null | head -n1'");
        if (volume) {
            audio_info_.available = true;
            audio_info_.volume_percent = parse_first_percentage(*volume).value_or(-1);
            audio_info_.muted = mute.has_value() && casefold(*mute).find("yes") != std::string::npos;
        }
    }

    if (audio_info_.available) {
        if (audio_info_.muted) {
            audio_info_.summary = localization_.translate("audio_muted");
            if (audio_info_.volume_percent >= 0) {
                audio_info_.summary += " · " + std::to_string(audio_info_.volume_percent) + "%";
            }
        } else if (audio_info_.volume_percent >= 0) {
            audio_info_.summary = std::to_string(audio_info_.volume_percent) + "%";
        }
    }

    if (audio_info_.summary.empty()) {
        audio_info_.summary = localization_.translate("state_unavailable");
    }

    if (!audio_devices_.empty()) {
        const auto default_iterator = std::find_if(audio_devices_.begin(), audio_devices_.end(), [](const auto& device) {
            return device.is_default;
        });
        if (default_iterator != audio_devices_.end()) {
            audio_info_.summary = default_iterator->name;
            if (audio_info_.volume_percent >= 0) {
                audio_info_.summary += " · " + std::to_string(audio_info_.volume_percent) + "%";
            }
            if (audio_info_.muted) {
                audio_info_.summary += " · " + localization_.translate("audio_muted");
            }
        }
    }

    if (command_exists("brightnessctl")) {
        const auto brightness = command_output("sh -lc 'brightnessctl -m 2>/dev/null | head -n1'");
        if (brightness) {
            brightness_info_.available = true;
            brightness_info_.percentage = parse_comma_percentage(*brightness).value_or(-1);
        }
    }

    if (!brightness_info_.available) {
        const auto brightness = command_output(R"(sh -lc 'for p in /sys/class/backlight/*; do if [ -r "$p/brightness" ] && [ -r "$p/max_brightness" ]; then cur=$(cat "$p/brightness"); max=$(cat "$p/max_brightness"); if [ "$max" -gt 0 ]; then printf "%s\n" $(( cur * 100 / max )); break; fi; fi; done')");
        if (brightness) {
            brightness_info_.available = true;
            try {
                brightness_info_.percentage = std::stoi(*brightness);
            } catch (...) {
                brightness_info_.percentage = -1;
            }
        }
    }

    if (command_exists("gsettings")) {
        const auto show_banners = command_output("sh -lc 'gsettings get org.gnome.desktop.notifications show-banners 2>/dev/null'");
        if (show_banners) {
            do_not_disturb_info_.available = true;
            do_not_disturb_info_.enabled = casefold(*show_banners).find("false") != std::string::npos;
        }
    }

    return snapshot;
}

void PanelWindow::request_quick_panel_refresh(bool detailed) {
    if constexpr (!kQuickPanelEnabled) {
        return;
    }

    detailed = detailed || quick_panel_popover_.is_visible();

    if (quick_panel_refresh_running_) {
        queued_quick_panel_refresh_ = true;
        queued_quick_panel_refresh_detailed_ = queued_quick_panel_refresh_detailed_ || detailed;
        return;
    }

    quick_panel_refresh_running_ = true;
    if (quick_panel_refresh_thread_.joinable()) {
        quick_panel_refresh_thread_.join();
    }

    quick_panel_refresh_thread_ = std::jthread([this, detailed](std::stop_token stop_token) {
        auto snapshot = build_quick_panel_snapshot(detailed);
        if (stop_token.stop_requested()) {
            return;
        }

        {
            auto lock = std::scoped_lock(quick_panel_refresh_mutex_);
            pending_quick_panel_snapshot_ = std::move(snapshot);
        }

        if (!stop_token.stop_requested()) {
            quick_panel_refresh_dispatcher_.emit();
        }
    });
}

void PanelWindow::apply_quick_panel_snapshot(const QuickPanelSnapshot& snapshot) {
    if constexpr (!kQuickPanelEnabled) {
        return;
    }

    network_info_ = snapshot.network_info;
    audio_info_ = snapshot.audio_info;
    brightness_info_ = snapshot.brightness_info;
    do_not_disturb_info_ = snapshot.do_not_disturb_info;

    if (snapshot.detailed) {
        network_access_points_ = snapshot.network_access_points;
        audio_devices_ = snapshot.audio_devices;
        audio_sources_ = snapshot.audio_sources;
        bluetooth_devices_ = snapshot.bluetooth_devices;
    }

    update_quick_panel_status(false);
}

void PanelWindow::on_quick_panel_refresh_ready() {
    if constexpr (!kQuickPanelEnabled) {
        return;
    }

    auto snapshot = std::optional<QuickPanelSnapshot>{};
    {
        auto lock = std::scoped_lock(quick_panel_refresh_mutex_);
        snapshot = std::move(pending_quick_panel_snapshot_);
        pending_quick_panel_snapshot_.reset();
    }

    quick_panel_refresh_running_ = false;

    if (snapshot) {
        apply_quick_panel_snapshot(*snapshot);
    }

    if (queued_quick_panel_refresh_) {
        const auto detailed = queued_quick_panel_refresh_detailed_;
        queued_quick_panel_refresh_ = false;
        queued_quick_panel_refresh_detailed_ = false;
        request_quick_panel_refresh(detailed);
    }
}

void PanelWindow::update_quick_panel_status(bool refresh_live_state) {
    if constexpr (!kQuickPanelEnabled) {
        return;
    }

    if (refresh_live_state) {
        request_quick_panel_refresh(quick_panel_popover_.is_visible());
    }

    quick_panel_network_label_.set_text(localization_.translate("network") + ": " + make_valid_utf8(network_info_.summary));
    quick_panel_volume_label_.set_text(localization_.translate("audio") + ": " + make_valid_utf8(audio_info_.summary));
    quick_panel_time_label_.set_text(localization_.translate("launcher_system_time") + ": " + clock_label_.get_text());
    quick_panel_brightness_label_.set_text(
        localization_.translate("brightness") + ": "
        + (brightness_info_.available && brightness_info_.percentage >= 0
            ? std::to_string(brightness_info_.percentage) + "%"
            : localization_.translate("state_unavailable")));
    if (!network_info_.active_ssid.empty()) {
        auto network_details = network_info_.active_ssid;
        if (network_info_.active_signal_percent >= 0) {
            network_details += " · " + std::to_string(network_info_.active_signal_percent) + "%";
        }
        if (!network_info_.active_security.empty()) {
            network_details += " · " + network_info_.active_security;
        }
        quick_panel_network_details_label_.set_text(make_valid_utf8(network_details));
    } else {
        quick_panel_network_details_label_.set_text(make_valid_utf8(network_info_.summary));
    }
    quick_panel_summary_label_.set_text(localization_.translate("quick_panel_button"));
    quick_panel_button_.set_label(quick_panel_summary_label_.get_text());

    quick_panel_wifi_row_label_.set_text(
        std::string{"Wi-Fi · "} + localization_.translate(network_info_.wifi_enabled ? "state_on" : "state_off"));
    quick_panel_bluetooth_row_label_.set_text(
        localization_.translate("bluetooth") + " · "
        + localization_.translate(network_info_.bluetooth_enabled ? "state_on" : "state_off"));
    quick_panel_dnd_row_label_.set_text(
        localization_.translate("do_not_disturb") + " · "
        + localization_.translate(do_not_disturb_info_.enabled ? "state_on" : "state_off"));

    quick_panel_switch_sync_in_progress_ = true;
    quick_panel_wifi_switch_.set_sensitive(network_info_.available);
    quick_panel_bluetooth_switch_.set_sensitive(command_exists("bluetoothctl"));
    quick_panel_dnd_switch_.set_sensitive(do_not_disturb_info_.available);
    quick_panel_wifi_switch_.set_active(network_info_.wifi_enabled);
    quick_panel_bluetooth_switch_.set_active(network_info_.bluetooth_enabled);
    quick_panel_dnd_switch_.set_active(do_not_disturb_info_.enabled);
    quick_panel_switch_sync_in_progress_ = false;

    volume_change_in_progress_ = true;
    quick_panel_volume_scale_.set_sensitive(audio_info_.available && audio_info_.volume_percent >= 0);
    if (audio_info_.available && audio_info_.volume_percent >= 0) {
        quick_panel_volume_scale_.set_value(audio_info_.volume_percent);
        quick_panel_audio_slider_label_.set_text(
            localization_.translate("audio") + ": " + std::to_string(audio_info_.volume_percent) + "%");
    } else {
        quick_panel_audio_slider_label_.set_text(
            localization_.translate("audio") + ": " + localization_.translate("state_unavailable"));
    }
    volume_change_in_progress_ = false;

    brightness_change_in_progress_ = true;
    quick_panel_brightness_scale_.set_sensitive(brightness_info_.available && brightness_info_.percentage >= 0);
    if (brightness_info_.available && brightness_info_.percentage >= 0) {
        quick_panel_brightness_scale_.set_value(brightness_info_.percentage);
    }
    brightness_change_in_progress_ = false;

    rebuild_quick_panel_networks();
    rebuild_quick_panel_audio_devices();
    rebuild_quick_panel_audio_sources();
    rebuild_quick_panel_bluetooth_devices();
}

void PanelWindow::update_clock_panel_status() {
    clock_panel_time_label_.set_text(clock_label_.get_text());

    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    const auto* local_time = std::localtime(&now_time);
    std::ostringstream date_stream;
    date_stream << std::put_time(local_time, "%A, %d %B %Y");
    clock_panel_date_label_.set_text(make_valid_utf8(date_stream.str()));
}

void PanelWindow::clear_minimized_tray() {
    for (auto* button : minimized_tray_buttons_) {
        minimized_tray_box_.remove(*button);
    }
    minimized_tray_buttons_.clear();
}

void PanelWindow::clear_empty_state_apps() {
    clear_task_group_popovers();
    for (auto* button : empty_state_app_buttons_) {
        empty_state_apps_box_.remove(*button);
    }
    empty_state_app_buttons_.clear();
}

void PanelWindow::clear_quick_panel_networks() {
    for (auto* button : quick_panel_network_buttons_) {
        quick_panel_networks_box_.remove(*button);
    }
    quick_panel_network_buttons_.clear();
}

void PanelWindow::clear_quick_panel_audio_devices() {
    for (auto* button : quick_panel_audio_device_buttons_) {
        quick_panel_audio_devices_box_.remove(*button);
    }
    quick_panel_audio_device_buttons_.clear();
}

void PanelWindow::clear_quick_panel_audio_sources() {
    for (auto* button : quick_panel_audio_source_buttons_) {
        quick_panel_audio_sources_box_.remove(*button);
    }
    quick_panel_audio_source_buttons_.clear();
}

void PanelWindow::clear_quick_panel_bluetooth_devices() {
    for (auto* button : quick_panel_bluetooth_device_buttons_) {
        quick_panel_bluetooth_devices_box_.remove(*button);
    }
    quick_panel_bluetooth_device_buttons_.clear();
}

void PanelWindow::rebuild_quick_panel_networks() {
    clear_quick_panel_networks();

    if (!network_info_.wifi_enabled || network_access_points_.empty()) {
        auto* button = Gtk::make_managed<Gtk::Button>(
            network_info_.wifi_enabled
                ? localization_.translate("quick_panel_no_networks")
                : localization_.translate("network_off"));
        button->set_sensitive(false);
        button->add_css_class("quick-panel-device-button");
        quick_panel_networks_box_.append(*button);
        quick_panel_network_buttons_.push_back(button);
        return;
    }

    for (const auto& access_point : network_access_points_) {
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("quick-panel-device-button");
        if (access_point.active) {
            button->add_css_class("quick-panel-device-button-active");
        }

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(access_point.active ? "network-wireless-signal-excellent-symbolic" : "network-wireless-signal-good-symbolic");
        icon->set_pixel_size(16);

        auto secondary = access_point.ssid;
        if (access_point.signal_percent >= 0) {
            secondary += " · " + std::to_string(access_point.signal_percent) + "%";
        }
        if (!access_point.security.empty()) {
            secondary += " · " + access_point.security;
        }
        auto* label = Gtk::make_managed<Gtk::Label>(compact_label(secondary, 42));
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        label->add_css_class("quick-panel-device-label");

        content->append(*icon);
        content->append(*label);
        button->set_child(*content);
        button->set_tooltip_text(secondary);
        button->signal_clicked().connect([this]() {
            if (launch_command("sh -lc 'command -v nm-connection-editor >/dev/null 2>&1 && nm-connection-editor || hyalo-control-center'")) {
                quick_panel_popover_.popdown();
            }
        });
        quick_panel_networks_box_.append(*button);
        quick_panel_network_buttons_.push_back(button);
    }
}

void PanelWindow::rebuild_quick_panel_audio_devices() {
    clear_quick_panel_audio_devices();

    if (audio_devices_.empty()) {
        auto* button = Gtk::make_managed<Gtk::Button>(localization_.translate("quick_panel_no_audio_devices"));
        button->set_sensitive(false);
        button->add_css_class("quick-panel-device-button");
        quick_panel_audio_devices_box_.append(*button);
        quick_panel_audio_device_buttons_.push_back(button);
        return;
    }

    for (const auto& device : audio_devices_) {
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("quick-panel-device-button");
        if (device.is_default) {
            button->add_css_class("quick-panel-device-button-active");
        }

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(device.is_default ? "audio-speakers-symbolic" : "audio-card-symbolic");
        icon->set_pixel_size(16);

        auto* label = Gtk::make_managed<Gtk::Label>(compact_label(device.name, 42));
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        label->add_css_class("quick-panel-device-label");

        content->append(*icon);
        content->append(*label);
        button->set_child(*content);
        button->set_tooltip_text(device.name);
        button->signal_clicked().connect([this, device]() {
            set_default_audio_device(device);
        });
        quick_panel_audio_devices_box_.append(*button);
        quick_panel_audio_device_buttons_.push_back(button);
    }
}

void PanelWindow::rebuild_quick_panel_audio_sources() {
    clear_quick_panel_audio_sources();

    if (audio_sources_.empty()) {
        auto* button = Gtk::make_managed<Gtk::Button>(localization_.translate("quick_panel_no_audio_sources"));
        button->set_sensitive(false);
        button->add_css_class("quick-panel-device-button");
        quick_panel_audio_sources_box_.append(*button);
        quick_panel_audio_source_buttons_.push_back(button);
        return;
    }

    for (const auto& source : audio_sources_) {
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("quick-panel-device-button");
        if (source.is_default) {
            button->add_css_class("quick-panel-device-button-active");
        }

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(source.is_default ? "audio-input-microphone-symbolic" : "microphone-sensitivity-medium-symbolic");
        icon->set_pixel_size(16);

        auto* label = Gtk::make_managed<Gtk::Label>(compact_label(source.name, 42));
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        label->add_css_class("quick-panel-device-label");

        content->append(*icon);
        content->append(*label);
        button->set_child(*content);
        button->set_tooltip_text(source.name);
        button->signal_clicked().connect([this, source]() {
            set_default_audio_source(source);
        });
        quick_panel_audio_sources_box_.append(*button);
        quick_panel_audio_source_buttons_.push_back(button);
    }
}

void PanelWindow::rebuild_quick_panel_bluetooth_devices() {
    clear_quick_panel_bluetooth_devices();

    if (bluetooth_devices_.empty()) {
        auto* button = Gtk::make_managed<Gtk::Button>(localization_.translate("quick_panel_no_bluetooth_devices"));
        button->set_sensitive(false);
        button->add_css_class("quick-panel-device-button");
        quick_panel_bluetooth_devices_box_.append(*button);
        quick_panel_bluetooth_device_buttons_.push_back(button);
        return;
    }

    for (const auto& device : bluetooth_devices_) {
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("quick-panel-device-button");
        if (device.connected) {
            button->add_css_class("quick-panel-device-button-active");
        }

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(device.connected ? "bluetooth-active-symbolic" : "bluetooth-symbolic");
        icon->set_pixel_size(16);

        auto status_text = device.connected
            ? localization_.translate("state_on")
            : localization_.translate("state_off");
        auto secondary = device.name + " · " + status_text;
        if (device.battery_percent >= 0) {
            secondary += " · " + std::to_string(device.battery_percent) + "%";
        }
        auto* label = Gtk::make_managed<Gtk::Label>(compact_label(secondary, 42));
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        label->add_css_class("quick-panel-device-label");

        content->append(*icon);
        content->append(*label);
        button->set_child(*content);
        button->set_tooltip_text(device.name + " (" + device.address + ")");
        button->signal_clicked().connect([this, address = device.address, connected = device.connected]() {
            connect_bluetooth_device(address, connected);
        });
        quick_panel_bluetooth_devices_box_.append(*button);
        quick_panel_bluetooth_device_buttons_.push_back(button);
    }
}

void PanelWindow::rebuild_minimized_tray(const std::vector<WindowSnapshot>& windows) {
    clear_minimized_tray();

    for (const auto& window : windows) {
        if (!window.minimized) {
            continue;
        }

        const auto title = window_title(window, localization_);
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("panel-tray-item");
        button->set_tooltip_text(title);

        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(resolve_window_icon_name(window));
        icon->set_pixel_size(18);
        icon->add_css_class("panel-tray-icon");
        button->set_child(*icon);

        button->signal_clicked().connect([this, identifier = window.identifier]() {
            handle_window_primary_action(identifier);
        });
        minimized_tray_box_.append(*button);
        minimized_tray_buttons_.push_back(button);
    }

    minimized_tray_box_.set_visible(!minimized_tray_buttons_.empty());
}

void PanelWindow::rebuild_empty_state_apps(const std::vector<WindowSnapshot>& windows) {
    clear_empty_state_apps();

    empty_state_apps_box_.remove_css_class("panel-running-apps-compact");
    empty_state_apps_box_.remove_css_class("panel-running-apps-icon-only");
    empty_state_apps_box_.set_spacing(6);
    empty_state_apps_box_.set_homogeneous(false);

    if (windows.empty()) {
        if (empty_state_apps_box_.get_parent() == &tasks_box_) {
            tasks_box_.remove(empty_state_apps_box_);
        }
        return;
    }

    struct WindowGroup {
        const WindowSnapshot* representative = nullptr;
        std::vector<const WindowSnapshot*> windows;
        std::string label;
        std::string icon_name;
        bool active = false;
        bool minimized = true;
    };

    auto grouped_windows = std::vector<WindowGroup>{};
    auto group_lookup = std::unordered_map<std::string, std::size_t>{};
    for (const auto& window : windows) {
        const auto key = window_group_key(window);
        auto iterator = group_lookup.find(key);
        if (iterator == group_lookup.end()) {
            auto group = WindowGroup{};
            group.label = resolve_window_group_label(window);
            group.icon_name = resolve_window_icon_name(window);
            group.representative = &window;
            group_lookup.emplace(key, grouped_windows.size());
            grouped_windows.push_back(std::move(group));
            iterator = group_lookup.find(key);
        }

        auto& group = grouped_windows[iterator->second];
        group.windows.push_back(&window);
        if (!window.minimized) {
            group.minimized = false;
            if (!group.representative || group.representative->minimized) {
                group.representative = &window;
            }
        }
        if (window.active) {
            group.active = true;
            group.representative = &window;
        }
    }

    const auto window_count = grouped_windows.size();
    const auto compact_mode = window_count >= 4;
    const auto icon_only = window_count >= 7;
    const auto max_visible = icon_only ? std::size_t{8} : std::size_t{6};
    const auto visible_count = std::min(window_count, max_visible);

    if (compact_mode) {
        empty_state_apps_box_.add_css_class("panel-running-apps-compact");
        empty_state_apps_box_.set_spacing(4);
    }
    if (icon_only) {
        empty_state_apps_box_.add_css_class("panel-running-apps-icon-only");
        empty_state_apps_box_.set_spacing(3);
    } else if (window_count <= 3) {
        empty_state_apps_box_.set_homogeneous(true);
    }

    for (std::size_t index = 0; index < visible_count; ++index) {
        const auto& group = grouped_windows[index];
        const auto& window = *group.representative;
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->add_css_class("panel-running-app");
        if (compact_mode) {
            button->add_css_class("panel-running-app-compact");
        }
        if (icon_only) {
            button->add_css_class("panel-running-app-icon-only");
        }
        if (group.active) {
            button->add_css_class("panel-running-app-active");
        }
        if (group.minimized) {
            button->add_css_class("panel-running-app-minimized");
        }

        auto tooltip = group.label;
        if (group.windows.size() > 1) {
            for (std::size_t item_index = 0; item_index < group.windows.size(); ++item_index) {
                if (item_index >= 5) {
                    tooltip += "\n+" + std::to_string(group.windows.size() - item_index);
                    break;
                }
                tooltip += "\n• " + window_title(*group.windows[item_index], localization_);
            }
        }
        button->set_tooltip_text(tooltip);

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        if (icon_only) {
            content->set_spacing(0);
        } else if (compact_mode) {
            content->set_spacing(6);
        }
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(group.icon_name);
        icon->set_pixel_size(18);
        icon->add_css_class("panel-running-app-icon");
        content->append(*icon);

        if (!icon_only) {
            const auto max_label_length = compact_mode ? std::size_t{11} : std::size_t{16};
            auto* label = Gtk::make_managed<Gtk::Label>(compact_label(group.label, max_label_length));
            label->set_xalign(0.0f);
            label->set_hexpand(true);
            label->add_css_class("panel-running-app-label");
            content->append(*label);
        }

        if (group.windows.size() > 1) {
            auto* count = Gtk::make_managed<Gtk::Label>(std::to_string(group.windows.size()));
            count->add_css_class("panel-running-app-count");
            content->append(*count);
        }

        button->set_child(*content);
        auto group_identifiers = std::vector<std::string>{};
        group_identifiers.reserve(group.windows.size());
        for (const auto* grouped_window : group.windows) {
            group_identifiers.push_back(grouped_window->identifier);
        }
        auto drag_source = Gtk::DragSource::create();
        drag_source->set_actions(Gdk::DragAction::MOVE);
        drag_source->signal_prepare().connect([group_identifiers](double, double) {
            return create_drag_content_provider(group_identifiers);
        }, false);
        button->add_controller(drag_source);
        button->signal_clicked().connect([this, group_identifiers = std::move(group_identifiers), primary_identifier = window.identifier]() {
            auto active_position = group_identifiers.size();
            for (std::size_t item_index = 0; item_index < group_identifiers.size(); ++item_index) {
                const auto iterator = std::find_if(last_windows_.begin(), last_windows_.end(), [&](const auto& current_window) {
                    return current_window.identifier == group_identifiers[item_index];
                });
                if (iterator != last_windows_.end() && iterator->active && !iterator->minimized) {
                    active_position = item_index;
                    break;
                }
            }

            if (active_position < group_identifiers.size() && group_identifiers.size() > 1 && window_tracker_) {
                for (std::size_t offset = 1; offset <= group_identifiers.size(); ++offset) {
                    const auto candidate_index = (active_position + offset) % group_identifiers.size();
                    const auto iterator = std::find_if(last_windows_.begin(), last_windows_.end(), [&](const auto& current_window) {
                        return current_window.identifier == group_identifiers[candidate_index];
                    });
                    if (iterator == last_windows_.end()) {
                        continue;
                    }
                    if (iterator->minimized) {
                        window_tracker_->set_minimized(iterator->identifier, false);
                    }
                    window_tracker_->activate(iterator->identifier);
                    return;
                }
            }

            handle_window_primary_action(primary_identifier);
        });

        if (group.windows.size() > 1) {
            auto* group_popover = Gtk::make_managed<Gtk::Popover>();
            group_popover->set_has_arrow(false);
            group_popover->set_autohide(true);
            group_popover->set_position(Gtk::PositionType::BOTTOM);
            group_popover->add_css_class("task-group-popover");
            group_popover->set_parent(*button);

            auto* popover_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
            popover_box->add_css_class("task-group-box");

            auto* popover_title = Gtk::make_managed<Gtk::Label>(group.label);
            popover_title->set_xalign(0.0f);
            popover_title->add_css_class("task-group-title");

            auto* popover_subtitle = Gtk::make_managed<Gtk::Label>(
                localization_.translate("task_group_windows") + ": " + std::to_string(group.windows.size()));
            popover_subtitle->set_xalign(0.0f);
            popover_subtitle->add_css_class("task-group-subtitle");

            popover_box->append(*popover_title);
            popover_box->append(*popover_subtitle);

            for (const auto* grouped_window : group.windows) {
                auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
                row->add_css_class("task-group-row");

                auto* activate_button = Gtk::make_managed<Gtk::Button>();
                activate_button->add_css_class("task-group-entry");
                if (grouped_window->active && !grouped_window->minimized) {
                    activate_button->add_css_class("task-group-entry-active");
                }

                auto* activate_content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
                auto* activate_icon = Gtk::make_managed<Gtk::Image>();
                activate_icon->set_from_icon_name(resolve_window_icon_name(*grouped_window));
                activate_icon->set_pixel_size(16);

                auto* activate_label = Gtk::make_managed<Gtk::Label>(compact_label(window_title(*grouped_window, localization_), 26));
                activate_label->set_xalign(0.0f);
                activate_label->set_hexpand(true);
                activate_label->add_css_class("task-group-entry-title");

                activate_content->append(*activate_icon);
                activate_content->append(*activate_label);
                activate_button->set_child(*activate_content);
                activate_button->set_hexpand(true);
                activate_button->signal_clicked().connect([this, group_popover, identifier = grouped_window->identifier]() {
                    group_popover->popdown();
                    handle_window_primary_action(identifier);
                });

                auto* close_button = Gtk::make_managed<Gtk::Button>();
                close_button->set_child(make_icon_widget("window-close-symbolic", 14));
                close_button->add_css_class("task-group-entry-close");
                close_button->set_tooltip_text(localization_.translate("task_close"));
                close_button->signal_clicked().connect([this, group_popover, identifier = grouped_window->identifier]() {
                    group_popover->popdown();
                    handle_window_close(identifier);
                });

                row->append(*activate_button);
                row->append(*close_button);
                popover_box->append(*row);
            }

            group_popover->set_child(*popover_box);
            task_group_popovers_.push_back(group_popover);

            auto right_click = Gtk::GestureClick::create();
            right_click->set_button(3);
            right_click->signal_pressed().connect([group_popover](int, double, double) {
                group_popover->popup();
            });
            button->add_controller(right_click);
        }

        empty_state_apps_box_.append(*button);
        empty_state_app_buttons_.push_back(button);
    }

    if (window_count > visible_count) {
        auto* overflow_button = Gtk::make_managed<Gtk::Button>();
        overflow_button->add_css_class("panel-running-app");
        overflow_button->add_css_class("panel-running-app-overflow");

        auto* overflow_label = Gtk::make_managed<Gtk::Label>("+" + std::to_string(window_count - visible_count));
        overflow_label->add_css_class("panel-running-app-label");
        overflow_button->set_child(*overflow_label);
        overflow_button->set_tooltip_text(localization_.translate("tasks_overflow"));
        overflow_button->signal_clicked().connect([this]() {
        });

        empty_state_apps_box_.append(*overflow_button);
        empty_state_app_buttons_.push_back(overflow_button);
    }

    if (empty_state_apps_box_.get_parent() != &tasks_box_) {
        tasks_box_.append(empty_state_apps_box_);
    }
}

void PanelWindow::refresh_launcher_results() {
    clear_launcher_context_popovers();
    clear_launcher_pinned_rows();
    clear_launcher_rows();
    pending_launcher_entries_.clear();
    launcher_render_index_ = 0;
    launcher_batch_render_scheduled_ = false;

    const auto query = casefold(launcher_search_entry_.get_text().raw());
    auto pinned_count = std::size_t{0};

    const auto show_pinned = true;
    launcher_restore_hidden_button_.set_visible(!launcher_hidden_ids_.empty());

    for (const auto& entry : launcher_entries_) {
        if (launcher_hidden_ids_.contains(entry.desktop_id)) {
            continue;
        }

        if (show_pinned && is_launcher_favorite(entry)) {
            auto* pinned_button = create_launcher_entry_button(entry, true);
            launcher_pinned_box_.append(*pinned_button);
            launcher_pinned_rows_.push_back(pinned_button);
            ++pinned_count;
        }

        if (!matches_launcher_filter(entry)) {
            continue;
        }

        if (!query.empty() && entry.search_blob.find(query) == std::string::npos) {
            continue;
        }
        pending_launcher_entries_.push_back(&entry);
    }

    if (show_pinned && pinned_count > 0) {
        launcher_pinned_label_.show();
        launcher_pinned_box_.show();
    } else {
        launcher_pinned_label_.hide();
        launcher_pinned_box_.hide();
    }

    if (pending_launcher_entries_.empty()) {
        launcher_empty_label_.set_text(
            query.empty()
                ? localization_.translate("launcher_empty")
                : localization_.translate("launcher_no_results"));
        launcher_empty_label_.show();
    } else {
        launcher_empty_label_.hide();
        ++launcher_refresh_generation_;
        render_launcher_batch(launcher_refresh_generation_);
    }

}

void PanelWindow::render_launcher_batch(std::uint64_t generation) {
    if (generation != launcher_refresh_generation_) {
        return;
    }

    auto appended = std::size_t{0};
    while (launcher_render_index_ < pending_launcher_entries_.size() && appended < kLauncherRenderBatchSize) {
        const auto* entry = pending_launcher_entries_[launcher_render_index_];
        auto* button = create_launcher_entry_button(*entry, false);
        launcher_list_box_.append(*button);
        launcher_rows_.push_back(button);
        ++launcher_render_index_;
        ++appended;
    }

    if (launcher_render_index_ >= pending_launcher_entries_.size()) {
        pending_launcher_entries_.clear();
        launcher_batch_render_scheduled_ = false;
        return;
    }

    if (launcher_batch_render_scheduled_) {
        return;
    }

    launcher_batch_render_scheduled_ = true;
    Glib::signal_timeout().connect_once([this, generation]() {
        launcher_batch_render_scheduled_ = false;
        render_launcher_batch(generation);
    }, 1);
}

void PanelWindow::handle_launcher_activated(const LauncherEntry& entry) {
    if (launch_desktop_entry(entry)) {
        hide_launcher();
    }
}

void PanelWindow::handle_settings_activated() {
    if (launch_command("hyalo-control-center")) {
        hide_launcher();
    }
}

bool PanelWindow::launch_desktop_entry(const LauncherEntry& entry) {
    auto* app_info = g_desktop_app_info_new_from_filename(entry.desktop_file.c_str());
    if (!app_info) {
        return false;
    }

    GError* error = nullptr;
    GAppLaunchContext* launch_context = nullptr;
    if (auto* display = gdk_display_get_default()) {
        launch_context = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(display));
    }

    const auto launched = g_app_info_launch(G_APP_INFO(app_info), nullptr, launch_context, &error);

    if (launch_context) {
        g_object_unref(launch_context);
    }
    g_object_unref(app_info);

    if (!launched) {
        if (error) {
            g_error_free(error);
        }
        return false;
    }

    return true;
}

bool PanelWindow::launch_command(const std::string& command_line) {
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

// ── Hot corners ──────────────────────────────────────────────────────
void PanelWindow::setup_hot_corners() {
    // Read hot_corners setting from config
    try {
        const auto& raw = config_manager_.raw();
        if (raw.contains("workspace") && raw["workspace"].contains("hot_corners")) {
            hot_corners_mode_ = raw["workspace"]["hot_corners"].get<std::string>();
        }
    } catch (...) {
        hot_corners_mode_ = "off";
    }

    if (hot_corners_mode_ == "off") return;

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        // Panel is anchored to top (or bottom). Hot corners = top-left corner of the panel.
        // Trigger when cursor is within 2px of the left edge AND within 2px of the top edge.
        const bool at_left_edge = (x <= 2.0);
        const bool at_top_edge  = (y <= 2.0);

        if (at_left_edge && at_top_edge) {
            if (!hot_corner_armed_) {
                hot_corner_armed_ = true;
                // Delay to prevent accidental triggers
                hot_corner_timer_ = Glib::signal_timeout().connect([this]() -> bool {
                    trigger_hot_corner_action();
                    hot_corner_armed_ = false;
                    return false;
                }, 200);
            }
        } else {
            if (hot_corner_armed_) {
                hot_corner_timer_.disconnect();
                hot_corner_armed_ = false;
            }
        }
    });
    add_controller(motion);
}

void PanelWindow::trigger_hot_corner_action() {
    if (hot_corners_mode_ == "launcher" || hot_corners_mode_ == "overview") {
        if (!launcher_window_.is_visible()) {
            toggle_launcher();
        }
    } else if (hot_corners_mode_ == "notifications") {
        if (!clock_popover_.is_visible()) {
            clock_popover_.popup();
        }
    }
}

void PanelWindow::configure_layer_shell() {
#if HYALO_PANEL_HAS_GTK4_LAYER_SHELL
    if (layer_shell_disabled()) {
        set_decorated(false);
        set_resizable(false);
        set_default_size(1280, config_manager_.panel().height + (kPanelVerticalMargin * 2));
        set_size_request(-1, config_manager_.panel().height + (kPanelVerticalMargin * 2));
        log_panel_diagnostic(diagnostics_enabled_, "panel layer-shell: disabled by environment");
        return;
    }

    const auto panel_config = config_manager_.panel();
    const auto panel_total_height = panel_config.height + (kPanelVerticalMargin * 2);
    auto* window = gobj();
    auto* display = gtk_widget_get_display(GTK_WIDGET(window));

    if (!panel_layer_shell_initialized_) {
        gtk_layer_init_for_window(window);
        panel_layer_shell_initialized_ = true;
        log_panel_diagnostic(diagnostics_enabled_, "panel layer-shell: init_for_window (first time)");
    } else {
        log_panel_diagnostic(diagnostics_enabled_, "panel layer-shell: reconfigure without re-init");
    }

    if (auto* monitor = select_primary_monitor(display)) {
        GdkRectangle geometry{};
        gdk_monitor_get_geometry(monitor, &geometry);
        set_default_size(geometry.width, panel_total_height);
        set_size_request(geometry.width, panel_total_height);
        gtk_layer_set_monitor(window, monitor);
        g_object_unref(monitor);
    }

    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, true);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, true);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, panel_config.position != "bottom");
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, panel_config.position == "bottom");
    gtk_layer_auto_exclusive_zone_enable(window);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    log_panel_diagnostic(
        diagnostics_enabled_,
        "panel layer-shell: anchors/exclusive-zone/keyboard-mode updated"
    );
#else
    set_decorated(false);
    set_resizable(false);
    log_panel_diagnostic(diagnostics_enabled_, "panel layer-shell: unavailable, using normal Gtk window mode");
#endif
}

void PanelWindow::configure_launcher_window() {
    launcher_window_.set_transient_for(*this);

#if HYALO_PANEL_HAS_GTK4_LAYER_SHELL
    if (layer_shell_disabled()) {
        launcher_window_.set_default_size(kLauncherWidth, -1);
        launcher_window_.set_size_request(kLauncherWidth, -1);
        log_panel_diagnostic(diagnostics_enabled_, "launcher layer-shell: disabled by environment");
        return;
    }

    const auto panel_config = config_manager_.panel();
    const auto panel_total_height = panel_config.height + (kPanelVerticalMargin * 2);
    auto* window = launcher_window_.gobj();
    auto* display = gtk_widget_get_display(GTK_WIDGET(gobj()));

    if (!launcher_layer_shell_initialized_) {
        gtk_layer_init_for_window(window);
        launcher_layer_shell_initialized_ = true;
        log_panel_diagnostic(diagnostics_enabled_, "launcher layer-shell: init_for_window (first time)");
    } else {
        log_panel_diagnostic(diagnostics_enabled_, "launcher layer-shell: reconfigure without re-init");
    }

    if (auto* monitor = select_primary_monitor(display)) {
        GdkRectangle geometry{};
        gdk_monitor_get_geometry(monitor, &geometry);
        const auto launcher_width = std::max(320, std::min(kLauncherWidth, geometry.width - (kLauncherScreenMargin * 2)));
        const auto available_height = std::max(320, geometry.height - panel_total_height - (kLauncherPanelGap * 2));
        const auto launcher_height = std::min(available_height, kLauncherMaxWindowHeight);
        const auto launcher_list_height = std::max(260, launcher_height - kLauncherReservedChromeHeight);
        launcher_window_.set_default_size(launcher_width, launcher_height);
        launcher_window_.set_size_request(launcher_width, -1);
        launcher_scroll_.set_max_content_height(std::min(kLauncherMaxVisibleHeight, launcher_list_height));
        gtk_layer_set_monitor(window, monitor);
        g_object_unref(monitor);
    } else {
        launcher_window_.set_default_size(kLauncherWidth, -1);
        launcher_window_.set_size_request(kLauncherWidth, -1);
        launcher_scroll_.set_max_content_height(kLauncherMaxVisibleHeight);
    }

    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, true);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, false);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, panel_config.position != "bottom");
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, panel_config.position == "bottom");
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, kLauncherScreenMargin);
    if (panel_config.position == "bottom") {
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, panel_total_height + kLauncherPanelGap);
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, 0);
    } else {
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, panel_total_height + kLauncherPanelGap);
        gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
    }
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    log_panel_diagnostic(
        diagnostics_enabled_,
        "launcher layer-shell: anchors/margins/keyboard-mode(ON_DEMAND) updated"
    );
#else
    launcher_window_.set_default_size(kLauncherWidth, -1);
    launcher_window_.set_size_request(kLauncherWidth, -1);
    log_panel_diagnostic(diagnostics_enabled_, "launcher layer-shell: unavailable, using normal Gtk window mode");
#endif
}

bool PanelWindow::update_clock() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    const auto* local_time = std::localtime(&now_time);

    std::ostringstream stream;
    stream << std::put_time(local_time, "%H:%M:%S");
    clock_label_.set_text(stream.str());
    update_clock_panel_status();
    ++status_refresh_tick_;

    if constexpr (kQuickPanelEnabled) {
        const auto quick_panel_visible = quick_panel_popover_.is_visible();
        const auto hidden_refresh_due = status_refresh_tick_ % 15 == 0;
        const auto visible_refresh_due = status_refresh_tick_ % 3 == 0;

        if (quick_panel_refresh_ready_ || quick_panel_visible) {
            update_quick_panel_status(
                quick_panel_visible ? visible_refresh_due : hidden_refresh_due
            );
        }
    }

    if (window_tracker_) {
        window_tracker_->poll();
    }

    const auto panel_active = gtk_window_is_active(GTK_WINDOW(gobj()));
    const auto launcher_visible = launcher_window_.is_visible();
    const auto launcher_active = launcher_visible
        ? gtk_window_is_active(GTK_WINDOW(launcher_window_.gobj()))
        : false;

    if (diagnostics_enabled_ && (panel_active != last_panel_active_state_
            || launcher_visible != last_launcher_visible_state_
            || launcher_active != last_launcher_active_state_)) {
        auto stream = std::ostringstream{};
        stream << "state: panel_active=" << (panel_active ? "1" : "0")
               << " launcher_visible=" << (launcher_visible ? "1" : "0")
               << " launcher_active=" << (launcher_active ? "1" : "0")
               << " launcher_inactive_ticks=" << launcher_inactive_ticks_;

        auto* panel_focus = gtk_window_get_focus(GTK_WINDOW(gobj()));
        auto* launcher_focus = gtk_window_get_focus(GTK_WINDOW(launcher_window_.gobj()));
        stream << " panel_focus=" << widget_debug_name(panel_focus)
               << " launcher_focus=" << widget_debug_name(launcher_focus);
        log_panel_diagnostic(diagnostics_enabled_, stream.str());
    }

    last_panel_active_state_ = panel_active;
    last_launcher_visible_state_ = launcher_visible;
    last_launcher_active_state_ = launcher_active;

    if (launcher_window_.is_visible()) {
        const auto launcher_active = gtk_window_is_active(GTK_WINDOW(launcher_window_.gobj()));
        if (!launcher_active && !any_launcher_popover_visible()) {
            ++launcher_inactive_ticks_;
            if (launcher_inactive_ticks_ >= 1) {
                log_panel_diagnostic(diagnostics_enabled_, "launcher watchdog: auto-hide after inactive ticks threshold");
                hide_launcher();
            }
        } else {
            launcher_inactive_ticks_ = 0;
        }
    }

    return true;
}

void PanelWindow::update_task_summary(const std::vector<WindowSnapshot>& windows) {
    last_windows_ = windows;

    auto main_task_windows = std::vector<WindowSnapshot>{};

    for (const auto& window : windows) {
        if (!window.minimized) {
            main_task_windows.push_back(window);
        }
    }

    rebuild_minimized_tray(windows);

    clear_task_rows();
    clear_empty_state_apps();

    if (main_task_windows.empty()) {
        rebuild_empty_state_apps(windows);
        return;
    }

    rebuild_empty_state_apps(main_task_windows);
}

void PanelWindow::clear_task_rows() {
    for (auto* row : task_rows_) {
        tasks_box_.remove(*row);
    }
    task_rows_.clear();
}

void PanelWindow::handle_window_primary_action(const std::string& identifier) {
    const auto iterator = std::find_if(last_windows_.begin(), last_windows_.end(), [&](const auto& window) {
        return window.identifier == identifier;
    });
    if (iterator == last_windows_.end() || !window_tracker_) {
        return;
    }

    if (iterator->active && !iterator->minimized) {
        window_tracker_->set_minimized(identifier, true);
        return;
    }

    if (iterator->minimized) {
        window_tracker_->set_minimized(identifier, false);
    }

    window_tracker_->activate(identifier);
}

void PanelWindow::handle_window_close(const std::string& identifier) {
    if (!window_tracker_) return;

    // Find app_id before closing
    const auto it = std::find_if(last_windows_.begin(), last_windows_.end(),
        [&](const auto& w) { return w.identifier == identifier; });
    const auto app_id  = (it != last_windows_.end()) ? it->app_id : std::string{};
    const auto title   = (it != last_windows_.end()) ? it->title  : std::string{};

    window_tracker_->close(identifier);

    // Start hang detection: if the window still exists after timeout, show force-quit dialog
    if (!app_id.empty()) {
        Glib::signal_timeout().connect_once([this, identifier, app_id, title]() {
            const auto still = std::find_if(last_windows_.begin(), last_windows_.end(),
                [&](const auto& w) { return w.identifier == identifier; });
            if (still != last_windows_.end()) {
                present_force_quit_dialog(identifier, app_id, title);
            }
        }, 5000);
    }
}

void PanelWindow::present_force_quit_dialog(const std::string& identifier,
                                            const std::string& app_id,
                                            const std::string& title) {
    auto* win = new Gtk::Window();
    win->set_modal(true);
    win->set_hide_on_close(true);
    win->set_decorated(false);
    win->set_resizable(false);
    win->set_default_size(460, -1);
    win->add_css_class("hyalo-modal-window");
    win->set_title("Aplikacja nie odpowiada");
    win->set_transient_for(*this);

    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    root->add_css_class("hyalo-modal-surface");
    win->set_child(*root);

    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name("dialog-warning-symbolic");
    icon->set_pixel_size(22);
    icon->add_css_class("hyalo-modal-icon");
    root->append(*icon);

    auto display_name = title.empty() ? app_id : title;
    auto* title_lbl = Gtk::make_managed<Gtk::Label>(display_name + " \xe2\x80\x94 nie odpowiada");
    title_lbl->set_xalign(0.0f);
    title_lbl->set_wrap(true);
    title_lbl->add_css_class("hyalo-modal-title");
    root->append(*title_lbl);

    auto* desc = Gtk::make_managed<Gtk::Label>(
        "Aplikacja przestała odpowiadać. Możesz wymusić zamknięcie procesu, "
        "poczekać aż odpowie lub anulować.");
    desc->set_wrap(true);
    desc->set_xalign(0.0f);
    desc->add_css_class("hyalo-modal-subtitle");
    root->append(*desc);

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    btn_box->set_homogeneous(true);
    btn_box->set_margin_top(8);

    auto* kill_btn = Gtk::make_managed<Gtk::Button>("Zamknij proces");
    kill_btn->add_css_class("hyalo-button");
    kill_btn->add_css_class("hyalo-button-danger");

    auto* wait_btn = Gtk::make_managed<Gtk::Button>("Poczekaj");
    wait_btn->add_css_class("hyalo-button");
    wait_btn->add_css_class("hyalo-button-primary");

    auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Anuluj");
    cancel_btn->add_css_class("hyalo-button");
    cancel_btn->add_css_class("hyalo-button-secondary");

    btn_box->append(*kill_btn);
    btn_box->append(*wait_btn);
    btn_box->append(*cancel_btn);
    root->append(*btn_box);

    kill_btn->signal_clicked().connect([this, win, app_id, identifier]() {
        // Force-kill by app_id using pkill -9
        if (!app_id.empty()) {
            launch_command("pkill -9 -f " + app_id);
        }
        win->close();
    });

    wait_btn->signal_clicked().connect([this, win, identifier, app_id, title]() {
        win->close();
        // Re-check after another 5s
        Glib::signal_timeout().connect_once([this, identifier, app_id, title]() {
            const auto still = std::find_if(last_windows_.begin(), last_windows_.end(),
                [&](const auto& w) { return w.identifier == identifier; });
            if (still != last_windows_.end()) {
                present_force_quit_dialog(identifier, app_id, title);
            }
        }, 5000);
    });

    cancel_btn->signal_clicked().connect([win]() {
        win->close();
    });

    win->signal_hide().connect([win]() {
        delete win;
    });

    win->present();
}

void PanelWindow::toggle_wifi() {
    if (!network_info_.available) {
        return;
    }

    launch_command(std::string{"sh -lc 'nmcli radio wifi "} + (network_info_.wifi_enabled ? "off" : "on") + "'");
    Glib::signal_timeout().connect_once([this]() {
        update_quick_panel_status(true);
    }, 300);
}

void PanelWindow::toggle_bluetooth() {
    if (!command_exists("bluetoothctl")) {
        return;
    }

    launch_command(std::string{"sh -lc 'bluetoothctl power "} + (network_info_.bluetooth_enabled ? "off" : "on") + " >/dev/null 2>&1'");
    Glib::signal_timeout().connect_once([this]() {
        update_quick_panel_status(true);
    }, 500);
}

void PanelWindow::connect_bluetooth_device(const std::string& address, bool connected) {
    if (!command_exists("bluetoothctl") || address.empty()) {
        return;
    }

    launch_command(std::string{"sh -lc 'bluetoothctl "} + (connected ? "disconnect " : "connect ") + address + " >/dev/null 2>&1'");
    Glib::signal_timeout().connect_once([this]() {
        update_quick_panel_status(true);
    }, 700);
}

void PanelWindow::set_default_audio_device(const AudioDeviceInfo& device) {
    if (device.id.empty()) {
        return;
    }

    if (command_exists("wpctl")) {
        launch_command("sh -lc 'wpctl set-default " + device.id + " >/dev/null 2>&1'");
    } else if (command_exists("pactl")) {
        launch_command("sh -lc 'pactl set-default-sink " + device.id + " >/dev/null 2>&1'");
    }

    Glib::signal_timeout().connect_once([this]() {
        update_quick_panel_status(true);
    }, 300);
}

void PanelWindow::set_default_audio_source(const AudioSourceInfo& source) {
    if (source.id.empty()) {
        return;
    }

    if (command_exists("wpctl")) {
        launch_command("sh -lc 'wpctl set-default " + source.id + " >/dev/null 2>&1'");
    } else if (command_exists("pactl")) {
        launch_command("sh -lc 'pactl set-default-source " + source.id + " >/dev/null 2>&1'");
    }

    Glib::signal_timeout().connect_once([this]() {
        update_quick_panel_status(true);
    }, 300);
}

void PanelWindow::toggle_do_not_disturb() {
    if (!do_not_disturb_info_.available) {
        return;
    }

    launch_command(std::string{"sh -lc 'gsettings set org.gnome.desktop.notifications show-banners "}
        + (do_not_disturb_info_.enabled ? "true" : "false") + "'");
    Glib::signal_timeout().connect_once([this]() {
        update_quick_panel_status(true);
    }, 200);
}

void PanelWindow::set_volume(int percentage) {
    if (percentage < 0 || !audio_info_.available) {
        return;
    }

    const auto clamped = std::clamp(percentage, 0, 150);
    if (command_exists("wpctl")) {
        launch_command("sh -lc 'wpctl set-volume @DEFAULT_AUDIO_SINK@ " + std::to_string(clamped) + "% >/dev/null 2>&1'");
    } else if (command_exists("pactl")) {
        launch_command("sh -lc 'pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(clamped) + "% >/dev/null 2>&1'");
    }

    Glib::signal_timeout().connect_once([this]() {
        update_quick_panel_status(true);
    }, 250);
}

void PanelWindow::set_brightness(int percentage) {
    if (percentage < 1 || !brightness_info_.available) {
        return;
    }

    launch_command("sh -lc 'command -v brightnessctl >/dev/null 2>&1 && brightnessctl set "
        + std::to_string(std::clamp(percentage, 1, 100)) + "% >/dev/null 2>&1'");
    Glib::signal_timeout().connect_once([this]() {
        update_quick_panel_status(true);
    }, 250);
}

std::string PanelWindow::resolve_window_group_label(const WindowSnapshot& window) const {
    auto candidates = std::vector<std::string>{};
    const auto push_candidate = [&candidates](const std::string& value) {
        if (!value.empty()) {
            candidates.push_back(casefold(value));
        }
    };

    push_candidate(window.app_id);
    push_candidate(window.app_id + ".desktop");
    push_candidate(window.title);

    if (const auto separator = window.app_id.find_last_of('.'); separator != std::string::npos) {
        const auto suffix = window.app_id.substr(separator + 1);
        push_candidate(suffix);
        push_candidate(suffix + ".desktop");
    }

    for (const auto& entry : launcher_entries_) {
        const auto desktop_id = casefold(entry.desktop_id);
        const auto desktop_id_without_suffix = desktop_id.ends_with(".desktop")
            ? desktop_id.substr(0, desktop_id.size() - 8)
            : desktop_id;
        const auto entry_name = casefold(entry.name);

        for (const auto& candidate : candidates) {
            if (candidate == desktop_id || candidate == desktop_id_without_suffix || candidate == entry_name) {
                return entry.name;
            }
        }
    }

    if (!window.app_id.empty()) {
        const auto label = prettify_app_identifier(window.app_id);
        if (!label.empty()) {
            return label;
        }
    }

    return window_title(window, localization_);
}

std::string PanelWindow::resolve_window_icon_name(const WindowSnapshot& window) const {
    auto candidates = std::vector<std::string>{};
    const auto push_candidate = [&candidates](const std::string& value) {
        if (value.empty()) {
            return;
        }
        const auto normalized = casefold(value);
        if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end()) {
            candidates.push_back(normalized);
        }
    };

    push_candidate(window.app_id);
    push_candidate(window.app_id + ".desktop");
    push_candidate(window.title);

    if (const auto separator = window.app_id.find_last_of('.'); separator != std::string::npos) {
        const auto suffix = window.app_id.substr(separator + 1);
        push_candidate(suffix);
        push_candidate(suffix + ".desktop");
    }

    for (const auto& entry : launcher_entries_) {
        const auto desktop_id = casefold(entry.desktop_id);
        const auto desktop_id_without_suffix = desktop_id.ends_with(".desktop")
            ? desktop_id.substr(0, desktop_id.size() - 8)
            : desktop_id;
        const auto entry_name = casefold(entry.name);

        for (const auto& candidate : candidates) {
            if (candidate == desktop_id || candidate == desktop_id_without_suffix || candidate == entry_name) {
                return entry.icon_name;
                return remap_icon_name(entry.icon_name, entry.desktop_id, entry.name, entry.category);
            }
        }
    }

    return remap_icon_name({}, window.app_id, window_title(window, localization_), {});
}

}  // namespace hyalo::panel