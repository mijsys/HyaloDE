pkg_check_modules(VTE4 REQUIRED IMPORTED_TARGET vte-2.91-gtk4)

add_executable(hyalo-terminal
    src/main.cpp
)

target_link_libraries(hyalo-terminal
    PRIVATE
        hyalo-core
        PkgConfig::VTE4
)

install(TARGETS hyalo-terminal
    RUNTIME DESTINATION bin
)

install(FILES
    data/hyalo-terminal.desktop
    data/org.hyalo.Terminal.desktop
    DESTINATION share/applications
)