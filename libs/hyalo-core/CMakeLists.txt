add_library(hyalo-core
    src/config_manager.cpp
    src/localization.cpp
    src/style_manager.cpp
)

target_include_directories(hyalo-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(hyalo-core
    PUBLIC
    HYALO_SOURCE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets/themes"
    HYALO_SOURCE_DEFAULTS_DIR="${CMAKE_SOURCE_DIR}/config/defaults"
    HYALO_SOURCE_LOCALES_DIR="${CMAKE_SOURCE_DIR}/config/locales"
    HYALO_INSTALLED_ASSETS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/hyalo/themes"
    HYALO_INSTALLED_DEFAULTS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/hyalo/defaults"
    HYALO_INSTALLED_LOCALES_DIR="${CMAKE_INSTALL_FULL_DATADIR}/hyalo/locales"
)

target_link_libraries(hyalo-core
    PUBLIC
        PkgConfig::GTKMM4
        nlohmann_json::nlohmann_json
)