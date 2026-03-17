add_executable(hyalo-files
    src/main.cpp
)

target_link_libraries(hyalo-files
    PRIVATE
        hyalo-core
)

install(TARGETS hyalo-files
    RUNTIME DESTINATION bin
)

install(FILES
    data/hyalo-files.desktop
    data/org.hyalo.Files.desktop
    DESTINATION share/applications
)
