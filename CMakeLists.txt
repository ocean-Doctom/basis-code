cmake_minimum_required(VERSION 3.16)

project(QtApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Network)

qt_wrap_cpp(DEVTOOLS_MOC devtools.h)

add_executable(QtApp WIN32
    ${DEVTOOLS_MOC}
    main.cpp
    notepad.cpp
    notepad.h
    terminalpane.cpp terminalpane.h
    devtools.cpp devtools.h
    chatgptpage.cpp chatgptpage.h
    agenttools.cpp agenttools.h
    builtinextensions.cpp builtinextensions.h
    syntaxhighlighter.cpp syntaxhighlighter.h
    codeeditor.cpp codeeditor.h
    windowtitlebar.cpp windowtitlebar.h
)

target_link_libraries(QtApp PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Network)
set(WORKSPACE_ASSETS icons/deepseek-original.ico icons/language-chatgpt.svg icons/language-cpp.svg icons/language-java.svg icons/language-python.svg icons/dropdown.svg icons/dropdown-light.svg icons/run.svg theme.qss theme-dark.qss theme-github-dark.qss close.svg icons/files.svg icons/extensions.svg
    icons/minimize.svg icons/maximize.svg icons/restore.svg icons/settings.svg icons/basis-code.ico icons/terminal.svg)
qt_add_resources(QtApp workspace_assets PREFIX "/" FILES ${WORKSPACE_ASSETS})

if(MINGW)
    target_compile_options(QtApp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_sources(QtApp PRIVATE basis-code.rc)
    target_link_libraries(QtApp PRIVATE dwmapi crypt32 bcrypt)
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
    add_executable(ExtensionTests tests/extensions_tests.cpp builtinextensions.cpp agenttools.cpp)
    target_include_directories(ExtensionTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ExtensionTests PRIVATE Qt6::Widgets Qt6::Network)
    add_dependencies(ExtensionTests QtApp)
    add_test(NAME extensions_and_agent_boundaries COMMAND ExtensionTests)
    set_tests_properties(extensions_and_agent_boundaries PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    add_executable(ChatGptTests ${DEVTOOLS_MOC} tests/chatgpt_tests.cpp devtools.cpp devtools.h terminalpane.cpp terminalpane.h agenttools.cpp agenttools.h)
    target_include_directories(ChatGptTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ChatGptTests PRIVATE Qt6::Widgets Qt6::Network crypt32 bcrypt)
    add_dependencies(ChatGptTests QtApp)
    add_test(NAME chatgpt_native_and_credentials COMMAND ChatGptTests)
    set_tests_properties(chatgpt_native_and_credentials PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    add_executable(LanguageTests ${DEVTOOLS_MOC} tests/language_tests.cpp devtools.cpp devtools.h terminalpane.cpp terminalpane.h)
    target_include_directories(LanguageTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(LanguageTests PRIVATE Qt6::Widgets Qt6::Network)
    add_dependencies(LanguageTests QtApp)
    add_executable(DevToolsTests ${DEVTOOLS_MOC} tests/devtools_tests.cpp devtools.cpp devtools.h terminalpane.cpp terminalpane.h)
    target_include_directories(DevToolsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(DevToolsTests PRIVATE Qt6::Widgets Qt6::Network)
    add_dependencies(DevToolsTests QtApp)
    add_test(NAME devtools_integration COMMAND DevToolsTests)
    set_tests_properties(devtools_integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;BASIS_TEST_COMPILER=${CMAKE_CXX_COMPILER}"
        TIMEOUT 120)
    add_executable(CodeEditorTests tests/codeeditor_tests.cpp codeeditor.cpp codeeditor.h syntaxhighlighter.cpp syntaxhighlighter.h)
    target_include_directories(CodeEditorTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(CodeEditorTests PRIVATE Qt6::Widgets)
    add_dependencies(CodeEditorTests QtApp)
    add_test(NAME codeeditor_line_numbers COMMAND CodeEditorTests)
    set_tests_properties(codeeditor_line_numbers PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    add_executable(NotepadTests ${DEVTOOLS_MOC} tests/notepad_tests.cpp builtinextensions.cpp terminalpane.cpp chatgptpage.cpp agenttools.cpp agenttools.h syntaxhighlighter.cpp devtools.cpp devtools.h notepad.cpp notepad.h codeeditor.cpp codeeditor.h windowtitlebar.cpp windowtitlebar.h)
    target_include_directories(NotepadTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(NotepadTests PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Network)
    qt_add_resources(NotepadTests test_workspace_assets PREFIX "/" FILES ${WORKSPACE_ASSETS})
    add_dependencies(NotepadTests QtApp)
    add_test(NAME notepad_integration COMMAND NotepadTests)
    set_tests_properties(notepad_integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30
    )
    if(WIN32)
        target_link_libraries(NotepadTests PRIVATE dwmapi crypt32 bcrypt)
        set_property(TEST extensions_and_agent_boundaries chatgpt_native_and_credentials devtools_integration notepad_integration codeeditor_line_numbers APPEND PROPERTY ENVIRONMENT
            "QT_PLUGIN_PATH=${QT_BIN_DIR}/../plugins")
    endif()
endif()
