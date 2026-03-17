pkg_check_modules(LIGHTDM REQUIRED IMPORTED_TARGET liblightdm-gobject-1)

add_executable(hyalo-greeter
    src/main.cpp
)

target_link_libraries(hyalo-greeter
    PRIVATE
        PkgConfig::GTKMM4
        PkgConfig::LIGHTDM
)

if(GTK4_LAYER_SHELL_FOUND)
    target_link_libraries(hyalo-greeter PRIVATE PkgConfig::GTK4_LAYER_SHELL)
    target_compile_definitions(hyalo-greeter PRIVATE HYALO_HAS_GTK4_LAYER_SHELL=1)
else()
    target_compile_definitions(hyalo-greeter PRIVATE HYALO_HAS_GTK4_LAYER_SHELL=0)
endif()

set(GREETER_CSS_DIR "${CMAKE_INSTALL_FULL_DATADIR}/hyalo/greeter")

target_compile_definitions(hyalo-greeter PRIVATE
    HYALO_GREETER_CSS_DIR="${GREETER_CSS_DIR}"
    HYALO_SOURCE_GREETER_CSS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

install(TARGETS hyalo-greeter
    RUNTIME DESTINATION bin
)

install(FILES data/hyalo-greeter.css
    DESTINATION share/hyalo/greeter
)

install(FILES data/hyalo-greeter.desktop
    DESTINATION share/xgreeters
)

install(FILES data/lightdm-hyalo-greeter.conf
    DESTINATION share/hyalo/greeter
)
