cmake_minimum_required(VERSION 3.25)
project(search VERSION 0.1.0 LANGUAGES CXX)
include(cmake/MsvcDependencies.cmake)

if(NOT WIN32)
    message(FATAL_ERROR "search currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Concurrent Network Sql)
include(CTest)

add_library(search_core STATIC
    src/core/types.h
    src/core/search.h src/core/search.cpp
    src/core/pinyin.h src/core/pinyin.cpp
    src/storage/settings.h src/storage/settings.cpp
    src/storage/catalog_store.h src/storage/catalog_store.cpp
)
target_include_directories(search_core PUBLIC src)
target_link_libraries(search_core PUBLIC Qt6::Core Qt6::Sql)

add_library(search_desktop STATIC
    src/platform/windows_integration.h src/platform/windows_integration.cpp
    src/platform/directory_monitor.h src/platform/directory_monitor.cpp
    src/app/catalog_service.h src/app/catalog_service.cpp
    src/ui/theme.h src/ui/theme.cpp
    src/ui/application_icons.h src/ui/application_icons.cpp
    src/ui/result_model.h src/ui/result_model.cpp
    src/ui/result_delegate.h src/ui/result_delegate.cpp
    src/ui/search_window.h src/ui/search_window.cpp
    src/ui/settings_window.h src/ui/settings_window.cpp
    src/app/application.h src/app/application.cpp
)
target_link_libraries(search_desktop PUBLIC search_core Qt6::Widgets Qt6::Concurrent Qt6::Network
    shell32 ole32 propsys user32 advapi32 dwmapi gdi32)
target_compile_definitions(search_desktop PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

qt_add_executable(search WIN32 src/main.cpp)
enable_language(RC)
configure_file(cmake/search.rc.in "${CMAKE_BINARY_DIR}/search.rc" @ONLY)
target_sources(search PRIVATE "${CMAKE_BINARY_DIR}/search.rc")
target_link_libraries(search PRIVATE search_desktop)
set_target_properties(search PROPERTIES OUTPUT_NAME search)

foreach(target search_core search_desktop search)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus
            $<$<CONFIG:Release>:/Zi>)
    endif()
endforeach()
if(MSVC)
    # Keep optimized release builds diagnosable; symbols stay in the build tree.
    target_link_options(search PRIVATE $<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>)
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(search_tests tests/core_tests.cpp)
    target_link_libraries(search_tests PRIVATE search_core Qt6::Test)
    if(MSVC)
        target_compile_options(search_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME search.core COMMAND search_tests)
    qt_add_executable(search_ui_tests tests/ui_tests.cpp)
    target_link_libraries(search_ui_tests PRIVATE search_desktop Qt6::Test)
    if(MSVC)
        target_compile_options(search_ui_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME search.ui COMMAND search_ui_tests)
    set_tests_properties(search.core search.ui PROPERTIES TIMEOUT 30)
endif()

set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Executable install directory" FORCE)
include(GNUInstallDirs)
install(TARGETS search RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Application)
# Generate the call directly: Qt 6.8's app-script helper serializes forwarded
# tool arguments as a semicolon-prefixed token, so exclusions can be ignored.
qt_generate_deploy_script(TARGET search OUTPUT_SCRIPT deploy_script CONTENT [[
qt_deploy_runtime_dependencies(
    EXECUTABLE "$<TARGET_FILE:search>"
    GENERATE_QT_CONF
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS
        --skip-plugin-types generic,networkinformation,tls,styles
        --exclude-plugins qgif,qjpeg,qsqlmimer,qsqlodbc,qsqlpsql
        --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw
)
]])
install(SCRIPT ${deploy_script} COMPONENT Application)
# App-local CRT DLLs keep the portable edition usable on Windows 10/11 without
# bundling the much larger VC++ installer. The OS already supplies the UCRT.
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
    endif()
    include(InstallRequiredSystemLibraries)
endif()
install(FILES docs/third-party.md DESTINATION licenses COMPONENT Application)
install(DIRECTORY docs/licenses/ DESTINATION licenses COMPONENT Application)
install(FILES docs/portable.md DESTINATION "." RENAME README.md COMPONENT Application)

configure_file(cmake/package.json.in "${CMAKE_BINARY_DIR}/package.json" @ONLY)
