pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)

find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)

set(HYALO_PANEL_SOURCES
    src/main.cpp
    src/panel_window.cpp
    src/window_tracker.cpp
)

set(HYALO_PANEL_PROTOCOL_SOURCES)
set(HYALO_PANEL_PROTOCOL_INCLUDE_DIRS)
set(HYALO_PANEL_PROTOCOL_LINK_LIBS)
set(HYALO_PANEL_LINK_LIBS hyalo-core PkgConfig::GIO_UNIX)
set(HYALO_PANEL_WLR_FOREIGN_TOPLEVEL_ENABLED OFF)
set(HYALO_PANEL_EXT_WORKSPACE_ENABLED OFF)
# Workspace support removed
set(HYALO_PANEL_GTK4_LAYER_SHELL_ENABLED OFF)

if(GTK4_LAYER_SHELL_FOUND)
    set(HYALO_PANEL_GTK4_LAYER_SHELL_ENABLED ON)
    list(APPEND HYALO_PANEL_LINK_LIBS PkgConfig::GTK4_LAYER_SHELL)
else()
    message(STATUS "gtk4-layer-shell not found; hyalo-panel will build as a regular GTK4 window")
endif()

set(HYALO_PANEL_VENDOR_WLR_FOREIGN_TOPLEVEL_XML
    "${CMAKE_SOURCE_DIR}/third_party/wlr-protocols/unstable/wlr-foreign-toplevel-management-unstable-v1.xml"
)


set(WLR_FOREIGN_TOPLEVEL_XML "")


if(EXISTS "/usr/share/wlr-protocols/unstable/wlr-foreign-toplevel-management-unstable-v1.xml")
    set(WLR_FOREIGN_TOPLEVEL_XML "/usr/share/wlr-protocols/unstable/wlr-foreign-toplevel-management-unstable-v1.xml")
elseif(EXISTS "${HYALO_PANEL_VENDOR_WLR_FOREIGN_TOPLEVEL_XML}")
    set(WLR_FOREIGN_TOPLEVEL_XML "${HYALO_PANEL_VENDOR_WLR_FOREIGN_TOPLEVEL_XML}")
endif()



if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER_EXECUTABLE AND EXISTS "${WLR_FOREIGN_TOPLEVEL_XML}")
        set(HYALO_PANEL_WLR_FOREIGN_TOPLEVEL_ENABLED ON)
        set(HYALO_PANEL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
        set(WLR_FOREIGN_TOPLEVEL_CLIENT_HEADER
            "${HYALO_PANEL_GENERATED_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
        )
        set(WLR_FOREIGN_TOPLEVEL_CLIENT_CODE
            "${HYALO_PANEL_GENERATED_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c"
        )

        add_custom_command(
            OUTPUT "${WLR_FOREIGN_TOPLEVEL_CLIENT_HEADER}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${HYALO_PANEL_GENERATED_DIR}"
            COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header
                "${WLR_FOREIGN_TOPLEVEL_XML}"
                "${WLR_FOREIGN_TOPLEVEL_CLIENT_HEADER}"
            DEPENDS "${WLR_FOREIGN_TOPLEVEL_XML}"
            VERBATIM
        )

        add_custom_command(
            OUTPUT "${WLR_FOREIGN_TOPLEVEL_CLIENT_CODE}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${HYALO_PANEL_GENERATED_DIR}"
            COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code
                "${WLR_FOREIGN_TOPLEVEL_XML}"
                "${WLR_FOREIGN_TOPLEVEL_CLIENT_CODE}"
            DEPENDS "${WLR_FOREIGN_TOPLEVEL_XML}"
            VERBATIM
        )

        list(APPEND HYALO_PANEL_PROTOCOL_SOURCES
            "${WLR_FOREIGN_TOPLEVEL_CLIENT_CODE}"
            "${WLR_FOREIGN_TOPLEVEL_CLIENT_HEADER}"
        )
        list(APPEND HYALO_PANEL_PROTOCOL_INCLUDE_DIRS "${HYALO_PANEL_GENERATED_DIR}")
        list(APPEND HYALO_PANEL_PROTOCOL_LINK_LIBS PkgConfig::WAYLAND_CLIENT)
else()
    message(STATUS "wayland-client, wayland-scanner, or wlr-foreign-toplevel XML missing; hyalo-panel will build without interactive taskbar actions")
endif()



add_executable(hyalo-panel
    ${HYALO_PANEL_SOURCES}
    ${HYALO_PANEL_PROTOCOL_SOURCES}
)

target_include_directories(hyalo-panel
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${HYALO_PANEL_PROTOCOL_INCLUDE_DIRS}
)

target_compile_definitions(hyalo-panel
    PRIVATE
        HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL=$<BOOL:${HYALO_PANEL_WLR_FOREIGN_TOPLEVEL_ENABLED}>
        HYALO_PANEL_HAS_GTK4_LAYER_SHELL=$<BOOL:${HYALO_PANEL_GTK4_LAYER_SHELL_ENABLED}>
)

target_link_libraries(hyalo-panel
    PRIVATE
        ${HYALO_PANEL_LINK_LIBS}
        ${HYALO_PANEL_PROTOCOL_LINK_LIBS}
)

install(TARGETS hyalo-panel
    RUNTIME DESTINATION bin
)

install(FILES data/org.hyalo.Panel.desktop
    DESTINATION share/applications
)