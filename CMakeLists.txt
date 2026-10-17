cmake_minimum_required(VERSION 3.21)
project(plasma-opencode-status VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Qml)
qt_standard_project_setup(REQUIRES 6.5)

set(module_dir "${CMAKE_CURRENT_BINARY_DIR}/qml/org/touchfoundry/opencodestatus")
qt_add_qml_module(opencodestatusplugin
    URI org.touchfoundry.opencodestatus
    VERSION 1.0
    PLUGIN_TARGET opencodestatusplugin
    OUTPUT_DIRECTORY "${module_dir}"
    SOURCES src/monitor.h src/monitor.cpp src/sessionmodel.h src/sessionmodel.cpp
)
target_link_libraries(opencodestatusplugin PRIVATE Qt6::Core Qt6::Qml)
target_include_directories(opencodestatusplugin PRIVATE src)

# A prefix-relative path also supports staging and user-local installations.
set(QML_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/qt6/qml" CACHE PATH "Qt QML import directory")
install(TARGETS opencodestatusplugin LIBRARY DESTINATION "${QML_INSTALL_DIR}/org/touchfoundry/opencodestatus")
install(FILES "${module_dir}/qmldir" "${module_dir}/opencodestatusplugin.qmltypes"
    DESTINATION "${QML_INSTALL_DIR}/org/touchfoundry/opencodestatus")
install(DIRECTORY package/ DESTINATION "${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/org.touchfoundry.opencodestatus")

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_executable(monitor-test tests/monitor-test.cpp src/monitor.cpp src/monitor.h)
    target_include_directories(monitor-test PRIVATE src)
    target_link_libraries(monitor-test PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME monitor COMMAND monitor-test)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)
    add_executable(ui-test tests/ui-test.cpp)
    target_link_libraries(ui-test PRIVATE Qt6::Quick Qt6::Test)
    target_compile_definitions(ui-test PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QML_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}/qml")
    add_dependencies(ui-test opencodestatusplugin)
    add_test(NAME ui COMMAND ui-test)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(model-test tests/model-test.cpp src/sessionmodel.cpp src/sessionmodel.h)
    target_include_directories(model-test PRIVATE src)
    target_link_libraries(model-test PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME model COMMAND model-test)
endif()
