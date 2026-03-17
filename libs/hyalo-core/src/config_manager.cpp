#include "hyalo-core/config_manager.hpp"

#include <cstdlib>
#include <fstream>
#include <utility>

namespace hyalo::core {

namespace {

std::filesystem::path prefer_existing_path(const std::filesystem::path& installed_path,
                                          const std::filesystem::path& source_path) {
    if (std::filesystem::exists(installed_path)) {
        return installed_path;
    }

    return source_path;
}

std::string normalize_theme_name(std::string value) {
    if (value == "hyalo-glass" || value == "glass") {
        return "glass";
    }
    if (value == "forest-mist" || value == "white" || value == "pastel") {
        return "pastel";
    }
    if (value == "graphite") {
        return "graphite";
    }

    return "pastel";
}

std::filesystem::path resolve_user_config_root() {
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg_config_home) / "hyalo";
    }

    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / "hyalo";
    }

    return std::filesystem::current_path() / ".hyalo";
}

bool load_json_file(const std::filesystem::path& path, nlohmann::json& target) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    input >> target;
    return true;
}

}  // namespace

RuntimePaths detect_runtime_paths() {
    return RuntimePaths{
        .assets_root = prefer_existing_path(
            std::filesystem::path(HYALO_INSTALLED_ASSETS_DIR),
            std::filesystem::path(HYALO_SOURCE_ASSETS_DIR)
        ),
        .defaults_root = prefer_existing_path(
            std::filesystem::path(HYALO_INSTALLED_DEFAULTS_DIR),
            std::filesystem::path(HYALO_SOURCE_DEFAULTS_DIR)
        ),
        .locales_root = prefer_existing_path(
            std::filesystem::path(HYALO_INSTALLED_LOCALES_DIR),
            std::filesystem::path(HYALO_SOURCE_LOCALES_DIR)
        ),
        .user_config_root = resolve_user_config_root(),
    };
}

ConfigManager::ConfigManager(RuntimePaths paths)
    : paths_(std::move(paths)) {
}

bool ConfigManager::load() {
    config_data_.clear();

    if (load_json_file(config_path(), config_data_)) {
        return true;
    }

    return load_json_file(paths_.defaults_root / "config.json", config_data_);
}

bool ConfigManager::load_defaults() {
    config_data_.clear();
    return load_json_file(paths_.defaults_root / "config.json", config_data_);
}

bool ConfigManager::save() const {
    std::error_code error;
    std::filesystem::create_directories(paths_.user_config_root, error);

    std::ofstream output(config_path());
    if (!output.is_open()) {
        return false;
    }

    output << config_data_.dump(2) << '\n';
    return output.good();
}

void ConfigManager::set_value(const std::vector<std::string>& path, const nlohmann::json& value) {
    if (path.empty()) {
        return;
    }

    if (!config_data_.is_object()) {
        config_data_ = nlohmann::json::object();
    }

    auto* cursor = &config_data_;
    for (std::size_t index = 0; index + 1 < path.size(); ++index) {
        const auto& key = path[index];
        if (!cursor->contains(key) || !(*cursor)[key].is_object()) {
            (*cursor)[key] = nlohmann::json::object();
        }

        cursor = &(*cursor)[key];
    }

    (*cursor)[path.back()] = value;
}

const RuntimePaths& ConfigManager::paths() const {
    return paths_;
}

const nlohmann::json& ConfigManager::raw() const {
    return config_data_;
}

std::string ConfigManager::language() const {
    return config_data_.value("language", std::string{"pl"});
}

AppearanceConfig ConfigManager::appearance() const {
    const auto appearance_json = config_data_.value("appearance", nlohmann::json::object());

    return AppearanceConfig{
        .border_radius = appearance_json.value("border_radius", 10),
        .accent_color = appearance_json.value("accent_color", std::string{"#8dd8b3"}),
        .transparency = appearance_json.value("transparency", 0.85),
    };
}

PanelConfig ConfigManager::panel() const {
    const auto panel_json = config_data_.value("panel", nlohmann::json::object());

    return PanelConfig{
        .position = panel_json.value("position", std::string{"top"}),
        .height = panel_json.value("height", 36),
        .show_all_workspaces = panel_json.value("show_all_workspaces", false),
    };
}

WorkspaceConfig ConfigManager::workspaces() const {
    const auto workspaces_json = config_data_.value("workspaces", nlohmann::json::object());

    auto config = WorkspaceConfig{};
    if (workspaces_json.contains("labels") && workspaces_json["labels"].is_array()) {
        config.labels.clear();
        for (const auto& entry : workspaces_json["labels"]) {
            if (entry.is_string()) {
                config.labels.push_back(entry.get<std::string>());
            }
        }
    }

    if (config.labels.empty()) {
        config.labels = {"1", "2", "3", "4"};
    }

    return config;
}

std::filesystem::path ConfigManager::base_style_path() const {
    return paths_.assets_root / "base.css";
}

std::filesystem::path ConfigManager::theme_style_path() const {
    const auto appearance_json = config_data_.value("appearance", nlohmann::json::object());
    const auto theme = normalize_theme_name(appearance_json.value("theme", std::string{"pastel"}));
    const auto preferred_path = paths_.assets_root / (theme + ".css");
    if (std::filesystem::exists(preferred_path)) {
        return preferred_path;
    }

    return paths_.assets_root / "pastel.css";
}

std::filesystem::path ConfigManager::theme_override_path() const {
    return paths_.user_config_root / "theme.css";
}

std::filesystem::path ConfigManager::config_path() const {
    return paths_.user_config_root / "config.json";
}

}  // namespace hyalo::core