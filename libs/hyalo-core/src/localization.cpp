#include "hyalo-core/localization.hpp"

#include <fstream>

namespace hyalo::core {

namespace {

bool load_json_file(const std::filesystem::path& path, nlohmann::json& target) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    input >> target;
    return true;
}

}  // namespace

Localization::Localization(const ConfigManager& config_manager)
    : config_manager_(config_manager) {
}

bool Localization::load() {
    translations_.clear();

    const auto language = config_manager_.language();
    const auto user_locale = config_manager_.paths().user_config_root / "locales" / (language + ".json");
    if (load_json_file(user_locale, translations_)) {
        return true;
    }

    return load_json_file(config_manager_.paths().locales_root / (language + ".json"), translations_);
}

std::string Localization::translate(const std::string& key) const {
    return translations_.value(key, key);
}

}  // namespace hyalo::core