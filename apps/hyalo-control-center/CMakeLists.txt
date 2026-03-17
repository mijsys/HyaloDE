add_executable(hyalo-control-center
    src/main.cpp
)

target_compile_definitions(hyalo-control-center
    PRIVATE
        HYALO_DE_VERSION="${PROJECT_VERSION}"
    HYALO_HAS_TERMINAL=$<BOOL:${HYALO_BUILD_TERMINAL}>
)

target_include_directories(hyalo-control-center
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hyalo-control-center
    PRIVATE
    hyalo-core
)

install(TARGETS hyalo-control-center
    RUNTIME DESTINATION bin
)

install(FILES data/org.hyalo.ControlCenter.desktop
    DESTINATION share/applications
)