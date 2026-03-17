#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hyalo::core {

struct AppearanceConfig {
    int border_radius = 0;
    std::string accent_color = "#8dd8b3";
    double transparency = 0.85;
};

struct PanelConfig {
    std::string position = "top";
    int height = 36;
    bool show_all_workspaces = false;
};

struct WorkspaceConfig {
    std::vector<std::string> labels = {"1", "2", "3", "4"};
};

struct RuntimePaths {
    std::filesystem::path assets_root;
    std::filesystem::path defaults_root;
    std::filesystem::path locales_root;
    std::filesystem::path user_config_root;
};

RuntimePaths detect_runtime_paths();

class ConfigManager {
public:
    explicit ConfigManager(RuntimePaths paths);

    bool load();
    bool load_defaults();
    bool save() const;
    void set_value(const std::vector<std::string>& path, const nlohmann::json& value);

    [[nodiscard]] const RuntimePaths& paths() const;
    [[nodiscard]] const nlohmann::json& raw() const;
    [[nodiscard]] std::string language() const;
    [[nodiscard]] AppearanceConfig appearance() const;
    [[nodiscard]] PanelConfig panel() const;
    [[nodiscard]] WorkspaceConfig workspaces() const;
    [[nodiscard]] std::filesystem::path base_style_path() const;
    [[nodiscard]] std::filesystem::path theme_style_path() const;
    [[nodiscard]] std::filesystem::path theme_override_path() const;
    [[nodiscard]] std::filesystem::path config_path() const;

private:
    RuntimePaths paths_;
    nlohmann::json config_data_;
};

}  // namespace hyalo::core