cmake_minimum_required(VERSION 3.21)
project(Ember VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Multimedia Widgets Sql Svg)
qt_standard_project_setup(REQUIRES 6.5)
add_library(EmberCore STATIC src/Core.cpp src/Core.h src/SessionStore.cpp src/SessionStore.h src/ImageLibrary.cpp src/ImageLibrary.h src/EmberBundle.cpp src/EmberBundle.h src/LoopbackTrust.cpp)
target_link_libraries(EmberCore PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Sql Qt6::Network)
if(WIN32)
    target_link_libraries(EmberCore PUBLIC iphlpapi)
endif()
target_include_directories(EmberCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
qt_add_executable(Ember WIN32 src/main.cpp src/EmberBackend.h src/EmberBackend.cpp)
target_link_libraries(Ember PRIVATE EmberCore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Multimedia Qt6::Widgets Qt6::Svg)

if(WIN32)
    # Build a real Explorer/taskbar .ico without shipping a preview raster or
    # an external icon compiler. Qt SVG is already used by the runtime.
    qt_add_executable(ember-icon-builder tools/IconBuilder.cpp)
    target_link_libraries(ember-icon-builder PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)
    set(EMBER_ICON_FILE "${CMAKE_CURRENT_BINARY_DIR}/ember.ico")
    add_custom_command(
        OUTPUT "${EMBER_ICON_FILE}"
        COMMAND $<TARGET_FILE:ember-icon-builder> "${CMAKE_CURRENT_SOURCE_DIR}/assets/ember-emblem.svg" "${EMBER_ICON_FILE}"
        DEPENDS ember-icon-builder "${CMAKE_CURRENT_SOURCE_DIR}/assets/ember-emblem.svg"
        VERBATIM)
    add_custom_target(ember-icon-resource DEPENDS "${EMBER_ICON_FILE}")
    add_dependencies(Ember ember-icon-resource)
    string(ASCII 34 EMBER_RC_QUOTE)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ember-icon.rc" "IDI_ICON1 ICON ${EMBER_RC_QUOTE}${EMBER_ICON_FILE}${EMBER_RC_QUOTE}\n")
    target_sources(Ember PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ember-icon.rc")
endif()

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_qml_module(Ember URI Ember VERSION 1.0 QML_FILES qml/Main.qml RESOURCES assets/ember-emblem.svg)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(ember-core-tests tests/CoreTests.cpp)
    target_link_libraries(ember-core-tests PRIVATE EmberCore Qt6::Test)
    set_target_properties(ember-core-tests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME ember-core COMMAND ember-core-tests
             -o "${CMAKE_BINARY_DIR}/ember-test-report.txt,txt")
endif()
