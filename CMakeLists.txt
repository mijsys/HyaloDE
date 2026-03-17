cmake_minimum_required(VERSION 3.24)

project(HyaloOS VERSION 0.2.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HYALO_BUILD_CONTROL_CENTER "Build the control center prototype" ON)
option(HYALO_BUILD_TERMINAL "Build the Hyalo Terminal application" ON)
option(HYALO_BUILD_UPDATE_CENTER "Build the Hyalo Update Center application" ON)
option(HYALO_BUILD_SOFTWARE_STORE "Build the Hyalo Software Store application" ON)
option(HYALO_BUILD_FILES "Build the Hyalo Files application" ON)
option(HYALO_BUILD_GREETER "Build the Hyalo LightDM greeter" ON)

include(FetchContent)
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
find_package(nlohmann_json 3.11 QUIET CONFIG)

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_path(HYALO_NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)

    if(HYALO_NLOHMANN_JSON_INCLUDE_DIR)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
        set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${HYALO_NLOHMANN_JSON_INCLUDE_DIR}"
        )
    else()
        message(STATUS "nlohmann_json package config not found; fetching nlohmann/json v3.11.3")
        FetchContent_Declare(
            nlohmann_json
            URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        )
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endif()

pkg_check_modules(GTKMM4 REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(GTK4_LAYER_SHELL QUIET IMPORTED_TARGET gtk4-layer-shell-0)

add_subdirectory(libs/hyalo-core)
add_subdirectory(apps/hyalo-panel)
add_subdirectory(apps/hyalo-wallpaper)

if(HYALO_BUILD_GREETER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/hyalo-greeter/CMakeLists.txt")
    add_subdirectory(apps/hyalo-greeter)
elseif(HYALO_BUILD_GREETER)
    message(STATUS "HYALO_BUILD_GREETER=ON, but apps/hyalo-greeter is missing; skipping greeter build")
endif()

if(HYALO_BUILD_CONTROL_CENTER)
    add_subdirectory(apps/hyalo-control-center)
endif()

if(HYALO_BUILD_TERMINAL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/hyalo-terminal/CMakeLists.txt")
    add_subdirectory(apps/hyalo-terminal)
elseif(HYALO_BUILD_TERMINAL)
    message(STATUS "HYALO_BUILD_TERMINAL=ON, but apps/hyalo-terminal is missing; skipping terminal build")
endif()

if(HYALO_BUILD_UPDATE_CENTER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/hyalo-update-center/CMakeLists.txt")
    add_subdirectory(apps/hyalo-update-center)
elseif(HYALO_BUILD_UPDATE_CENTER)
    message(STATUS "HYALO_BUILD_UPDATE_CENTER=ON, but apps/hyalo-update-center is missing; skipping update center build")
endif()

if(HYALO_BUILD_FILES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/hyalo-files/CMakeLists.txt")
    add_subdirectory(apps/hyalo-files)
elseif(HYALO_BUILD_FILES)
    message(STATUS "HYALO_BUILD_FILES=ON, but apps/hyalo-files is missing; skipping files build")
endif()

if(HYALO_BUILD_SOFTWARE_STORE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/hyalo-software-store/CMakeLists.txt")
    add_subdirectory(apps/hyalo-software-store)
elseif(HYALO_BUILD_SOFTWARE_STORE)
    message(STATUS "HYALO_BUILD_SOFTWARE_STORE=ON, but apps/hyalo-software-store is missing; skipping software store build")
endif()

install(DIRECTORY assets/
    DESTINATION share/hyalo
)

install(DIRECTORY assets/icons/hyalo-icons/
    DESTINATION share/icons/hyalo-icons
)

install(DIRECTORY config/defaults/
    DESTINATION share/hyalo/defaults
)

install(DIRECTORY config/locales/
    DESTINATION share/hyalo/locales
)

install(PROGRAMS
    config/labwc/hyalo-wallpaperd
    DESTINATION bin
)