cmake_minimum_required(VERSION 3.24)
project(iiFileProvider VERSION 0.5.0 LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(_default_home "$ENV{HOME}")
    elseif(DEFINED ENV{USERPROFILE} AND NOT "$ENV{USERPROFILE}" STREQUAL "")
        set(_default_home "$ENV{USERPROFILE}")
    else()
        message(FATAL_ERROR "Set CMAKE_INSTALL_PREFIX when HOME and USERPROFILE are unavailable")
    endif()
    set(CMAKE_INSTALL_PREFIX "${_default_home}/.local/SDK/iiFileProvider" CACHE PATH "Install prefix" FORCE)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core)
function(_iifileprovider_find_sqlite)
    set(CMAKE_FIND_FRAMEWORK LAST)
    find_package(SQLite3 3.26 REQUIRED)
endfunction()
_iifileprovider_find_sqlite()
if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

add_library(iiFileProvider SHARED src/iiFileProvider.h src/iiFileProvider.cpp
    src/Export.h src/AuthenticationToken.h src/AuthenticationToken.cpp src/FileAuthor.h src/FileAuthor.cpp src/JsonContract.h
    src/Authorship.h src/Authorship.cpp src/FileLink.h src/FileLink.cpp src/File.h src/File.cpp src/AtomicFileCommit.cpp
    src/Database.h src/Database.cpp src/StagedFile.h src/StagedFile.cpp)
add_library(iiFileProvider::iiFileProvider ALIAS iiFileProvider)
target_compile_features(iiFileProvider PUBLIC cxx_std_20)
target_compile_definitions(iiFileProvider PRIVATE IIFILEPROVIDER_BUILDING_LIBRARY)
target_link_libraries(iiFileProvider PUBLIC Qt6::Core)
target_link_libraries(iiFileProvider PRIVATE SQLite3::SQLite3)
target_include_directories(iiFileProvider PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(iiFileProvider PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    # Storage APIs are additive; preserve the existing 0.4 value-type ABI.
    SOVERSION 0.4
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME iiFileProvider.dependency_direction
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dependency_direction.py)
    add_executable(iiFileProvider_database tests/database.cpp)
    target_link_libraries(iiFileProvider_database PRIVATE iiFileProvider::iiFileProvider)
    add_test(NAME iiFileProvider.database COMMAND iiFileProvider_database)
    add_executable(iiFileProvider_file_crud tests/file_crud.cpp)
    target_link_libraries(iiFileProvider_file_crud PRIVATE iiFileProvider::iiFileProvider)
    add_test(NAME iiFileProvider.file_crud COMMAND iiFileProvider_file_crud)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    add_executable(iiFileProvider_authorship tests/authorship.cpp)
    target_link_libraries(iiFileProvider_authorship PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
    set_target_properties(iiFileProvider_authorship PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
    add_test(NAME iiFileProvider.authorship COMMAND iiFileProvider_authorship)
    add_executable(iiFileProvider_file_links tests/file_links.cpp)
    target_link_libraries(iiFileProvider_file_links PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
    set_target_properties(iiFileProvider_file_links PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
    add_test(NAME iiFileProvider.file_links COMMAND iiFileProvider_file_links)
    set_tests_properties(iiFileProvider.file_links PROPERTIES TIMEOUT 30)
    add_executable(iiFileProvider_author_contract tests/author_contract.cpp)
    target_link_libraries(iiFileProvider_author_contract PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
    set_target_properties(iiFileProvider_author_contract PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
    add_test(NAME iiFileProvider.author_contract COMMAND iiFileProvider_author_contract)
    set_tests_properties(iiFileProvider.author_contract PROPERTIES TIMEOUT 30)
    add_executable(iiFileProvider_hello_world tests/hello_world.cpp)
    target_link_libraries(iiFileProvider_hello_world PRIVATE iiFileProvider::iiFileProvider)
    set_target_properties(iiFileProvider_hello_world PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME iiFileProvider.hello_world COMMAND iiFileProvider_hello_world)
endif()

set(_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProvider")
configure_package_config_file(
    cmake/iiFileProviderConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderConfig.cmake"
    INSTALL_DESTINATION "${_package_dir}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(TARGETS iiFileProvider EXPORT iiFileProviderTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES src/iiFileProvider.h src/Export.h src/AuthenticationToken.h src/FileAuthor.h src/Authorship.h src/FileLink.h src/File.h src/Database.h src/StagedFile.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT iiFileProviderTargets NAMESPACE iiFileProvider:: DESTINATION "${_package_dir}")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderConfigVersion.cmake"
    DESTINATION "${_package_dir}"
)
install(FILES README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/iiFileProvider")
install(FILES docs/FILE_AUTHOR_CONTRACT.md DESTINATION "${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs")
install(FILES docs/FILE_LINKS.md DESTINATION "${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs")

install(FILES docs/FILE_CRUD.md DESTINATION "${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs")
