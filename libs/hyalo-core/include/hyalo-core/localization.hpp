#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "hyalo-core/config_manager.hpp"

namespace hyalo::core {

class Localization {
public:
    explicit Localization(const ConfigManager& config_manager);

    bool load();
    [[nodiscard]] std::string translate(const std::string& key) const;

private:
    const ConfigManager& config_manager_;
    nlohmann::json translations_;
};

}  // namespace hyalo::core