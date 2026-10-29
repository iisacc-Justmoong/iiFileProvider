cmake_minimum_required(VERSION 3.24)
project(iiFileProviderInstalledConsumer LANGUAGES CXX)

find_package(iiFileProvider 0.8.0 CONFIG REQUIRED)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
add_executable(iiFileProvider_installed_consumer ../hello_world.cpp)
target_link_libraries(iiFileProvider_installed_consumer PRIVATE iiFileProvider::iiFileProvider)
set_target_properties(iiFileProvider_installed_consumer PROPERTIES CXX_EXTENSIONS OFF)

enable_testing()
add_test(NAME iiFileProvider.installed_consumer COMMAND iiFileProvider_installed_consumer)

# Compile the author contract through the installed headers and imported shared target.
add_executable(iiFileProvider_installed_author_contract ../author_contract.cpp)
target_link_libraries(iiFileProvider_installed_author_contract PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
set_target_properties(iiFileProvider_installed_author_contract PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME iiFileProvider.installed_author_contract COMMAND iiFileProvider_installed_author_contract)
set_tests_properties(iiFileProvider.installed_author_contract PROPERTIES TIMEOUT 30)
add_executable(iiFileProvider_installed_authorship ../authorship.cpp)
target_link_libraries(iiFileProvider_installed_authorship PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
set_target_properties(iiFileProvider_installed_authorship PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME iiFileProvider.installed_authorship COMMAND iiFileProvider_installed_authorship)
add_executable(iiFileProvider_installed_file_links ../file_links.cpp)
target_link_libraries(iiFileProvider_installed_file_links PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
set_target_properties(iiFileProvider_installed_file_links PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
add_test(NAME iiFileProvider.installed_file_links COMMAND iiFileProvider_installed_file_links)
set_tests_properties(iiFileProvider.installed_file_links PROPERTIES TIMEOUT 30)

foreach(contract file_crud database)
    add_executable(installed_${contract} ../${contract}.cpp)
    target_link_libraries(installed_${contract} PRIVATE iiFileProvider::iiFileProvider iiFileProvider::Temporary)
    add_test(NAME installed.${contract} COMMAND installed_${contract})
endforeach()

add_executable(installed_catalog ../file_catalog.cpp)
target_compile_definitions(installed_catalog PRIVATE CATALOG_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(installed_catalog PRIVATE iiFileProvider::Catalog)
add_test(NAME installed.catalog COMMAND installed_catalog)

find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Gui)
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Match the source-tree image-codec test: Qt 6.8's imported desktop GUI
    # metadata still lists AGL, which current macOS SDKs no longer ship.
    foreach(dependency WrapOpenGL::WrapOpenGL Qt6::Gui)
        if(TARGET ${dependency})
            foreach(property INTERFACE_LINK_LIBRARIES INTERFACE_LINK_OPTIONS)
                get_target_property(links ${dependency} ${property})
                if(links)
                    list(FILTER links EXCLUDE REGEX "AGL")
                    set_property(TARGET ${dependency} PROPERTY ${property} "${links}")
                endif()
            endforeach()
        endif()
    endforeach()
endif()
add_executable(installed_file_devices ../file_devices.cpp ../device_resource.qrc)
set_target_properties(installed_file_devices PROPERTIES AUTORCC ON)
target_link_libraries(installed_file_devices PRIVATE iiFileProvider::iiFileProvider Qt6::Gui)
add_test(NAME installed.file_devices COMMAND installed_file_devices "${CMAKE_CURRENT_BINARY_DIR}")

if(APPLE)
    # Exercise the selected installed package, even when the shell exports an
    # older SDK library with the same install name.
    set_tests_properties(iiFileProvider.installed_consumer
        iiFileProvider.installed_author_contract iiFileProvider.installed_authorship
        iiFileProvider.installed_file_links installed.file_crud installed.database
        installed.catalog installed.file_devices
        PROPERTIES ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_VERSIONED_LIBRARY_PATH=unset:")
endif()
