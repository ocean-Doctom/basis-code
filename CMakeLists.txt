cmake_minimum_required(VERSION 3.16)

project(QtApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

add_executable(QtApp WIN32
    main.cpp
    notepad.cpp
    notepad.h
    windowtitlebar.cpp windowtitlebar.h
)

target_link_libraries(QtApp PRIVATE Qt6::Widgets Qt6::PrintSupport)
set(WORKSPACE_ASSETS theme.qss close.svg icons/files.svg icons/folder.svg icons/new-file.svg icons/save.svg
    icons/minimize.svg icons/maximize.svg icons/restore.svg icons/settings.svg icons/basis-code.ico)
qt_add_resources(QtApp workspace_assets PREFIX "/" FILES ${WORKSPACE_ASSETS})

if(MINGW)
    target_compile_options(QtApp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_sources(QtApp PRIVATE basis-code.rc)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    get_filename_component(QT_BIN_DIR "${WINDEPLOYQT_EXECUTABLE}" DIRECTORY)
    get_filename_component(COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET QtApp POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=${QT_BIN_DIR}\;${COMPILER_BIN_DIR}\;$ENV{PATH}"
            "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler
            "$<TARGET_FILE:QtApp>"
        VERBATIM
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(NotepadTests tests/notepad_tests.cpp notepad.cpp notepad.h windowtitlebar.cpp windowtitlebar.h)
    target_include_directories(NotepadTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(NotepadTests PRIVATE Qt6::Widgets Qt6::PrintSupport)
    qt_add_resources(NotepadTests test_workspace_assets PREFIX "/" FILES ${WORKSPACE_ASSETS})
    add_dependencies(NotepadTests QtApp)
    add_test(NAME notepad_integration COMMAND NotepadTests)
    set_tests_properties(notepad_integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30
    )
    if(WIN32)
        set_property(TEST notepad_integration APPEND PROPERTY ENVIRONMENT
            "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins")
    endif()
endif()
