pkg_check_modules(GDK_PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)

add_executable(hyalo-wallpaper
    src/main.cpp
)

target_link_libraries(hyalo-wallpaper
    PRIVATE
        PkgConfig::GTKMM4
        PkgConfig::GDK_PIXBUF
)

if(GTK4_LAYER_SHELL_FOUND)
    target_link_libraries(hyalo-wallpaper PRIVATE PkgConfig::GTK4_LAYER_SHELL)
    target_compile_definitions(hyalo-wallpaper PRIVATE HYALO_HAS_GTK4_LAYER_SHELL=1)
else()
    target_compile_definitions(hyalo-wallpaper PRIVATE HYALO_HAS_GTK4_LAYER_SHELL=0)
endif()

install(TARGETS hyalo-wallpaper
    RUNTIME DESTINATION bin
)