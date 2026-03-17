add_executable(hyalo-software-store
    src/main.cpp
)

target_include_directories(hyalo-software-store
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hyalo-software-store
    PRIVATE
        hyalo-core
)

install(TARGETS hyalo-software-store
    RUNTIME DESTINATION bin
)

install(FILES data/org.hyalo.SoftwareStore.desktop
    DESTINATION share/applications
)
