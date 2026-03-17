add_executable(hyalo-update-center
    src/main.cpp
)

target_include_directories(hyalo-update-center
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hyalo-update-center
    PRIVATE
        hyalo-core
)

install(TARGETS hyalo-update-center
    RUNTIME DESTINATION bin
)

install(FILES data/org.hyalo.UpdateCenter.desktop
    DESTINATION share/applications
)
