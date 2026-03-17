#pragma once

#include "hyalo-core/config_manager.hpp"

namespace hyalo::core {

class StyleManager {
public:
    static bool apply(const ConfigManager& config_manager);
};

}  // namespace hyalo::core