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
option(IIFILEPROVIDER_BUILD_QT_API "Build the existing Qt value/file adapters" ON)
function(_iifileprovider_find_sqlite)
    set(CMAKE_FIND_FRAMEWORK LAST)
    find_package(SQLite3 3.26 REQUIRED)
endfunction()
_iifileprovider_find_sqlite()
find_package(Threads REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

add_library(iiFileProviderObjects STATIC src/ObjectStore.cpp src/ObjectHash.cpp src/ObjectSource.cpp src/ObjectBatchRead.cpp src/ObjectSnapshotBatch.cpp src/ObjectPackager.cpp src/ObjectAudit.cpp)
add_library(iiFileProvider::Objects ALIAS iiFileProviderObjects)
set_target_properties(iiFileProviderObjects PROPERTIES EXPORT_NAME Objects POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)
target_compile_features(iiFileProviderObjects PUBLIC cxx_std_23)
target_include_directories(iiFileProviderObjects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(iiFileProviderObjects PRIVATE SQLite3::SQLite3 Threads::Threads)
add_executable(iiFileProviderObjectPackage tools/ObjectPackage.cpp tools/ObjectPackageOptions.cpp)
target_link_libraries(iiFileProviderObjectPackage PRIVATE iiFileProvider::Objects)
install(TARGETS iiFileProviderObjectPackage RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(BUILD_TESTING)
    add_executable(iiFileProvider_object_read_only tests/object_read_only.cpp)
    target_compile_definitions(iiFileProvider_object_read_only PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(iiFileProvider_object_read_only PRIVATE iiFileProvider::Objects SQLite3::SQLite3)
    add_test(NAME iiFileProvider.object_read_only COMMAND iiFileProvider_object_read_only)
    set_tests_properties(iiFileProvider.object_read_only PROPERTIES TIMEOUT 180)
    add_executable(iiFileProvider_object_package_options tests/object_package_options.cpp tools/ObjectPackageOptions.cpp)
    target_include_directories(iiFileProvider_object_package_options PRIVATE tools)
    target_link_libraries(iiFileProvider_object_package_options PRIVATE iiFileProvider::Objects)
    add_test(NAME iiFileProvider.object_package_options COMMAND iiFileProvider_object_package_options)
    set_tests_properties(iiFileProvider.object_package_options PROPERTIES TIMEOUT 30)
    add_executable(iiFileProvider_object_audit tests/object_audit.cpp)
    target_link_libraries(iiFileProvider_object_audit PRIVATE iiFileProvider::Objects SQLite3::SQLite3)
    target_compile_definitions(iiFileProvider_object_audit PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiFileProvider.object_audit COMMAND iiFileProvider_object_audit)
    set_tests_properties(iiFileProvider.object_audit PROPERTIES TIMEOUT 360)
    add_executable(iiFileProvider_object_snapshot_batch tests/object_snapshot_batch.cpp)
    target_link_libraries(iiFileProvider_object_snapshot_batch PRIVATE iiFileProvider::Objects)
    target_compile_definitions(iiFileProvider_object_snapshot_batch PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiFileProvider.object_snapshot_batch COMMAND iiFileProvider_object_snapshot_batch)
    set_tests_properties(iiFileProvider.object_snapshot_batch PROPERTIES TIMEOUT 180)
    add_executable(iiFileProvider_object_index tests/object_index.cpp)
    target_link_libraries(iiFileProvider_object_index PRIVATE iiFileProvider::Objects SQLite3::SQLite3)
    target_compile_definitions(iiFileProvider_object_index PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME iiFileProvider.object_index COMMAND iiFileProvider_object_index)
    set_tests_properties(iiFileProvider.object_index PROPERTIES TIMEOUT 300)
    add_executable(iiFileProvider_object_batch_read tests/object_batch_read.cpp)
    target_link_libraries(iiFileProvider_object_batch_read PRIVATE iiFileProvider::Objects)
    add_test(NAME iiFileProvider.object_batch_read COMMAND iiFileProvider_object_batch_read)
    set_tests_properties(iiFileProvider.object_batch_read PROPERTIES TIMEOUT 120)
    add_executable(iiFileProvider_object_batch tests/object_batch.cpp)
    target_link_libraries(iiFileProvider_object_batch PRIVATE iiFileProvider::Objects)
    add_test(NAME iiFileProvider.object_batch COMMAND iiFileProvider_object_batch)
    set_tests_properties(iiFileProvider.object_batch PROPERTIES TIMEOUT 240)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME iiFileProvider.object_package_cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/object_package_cli.py $<TARGET_FILE:iiFileProviderObjectPackage>)
    set_tests_properties(iiFileProvider.object_package_cli PROPERTIES TIMEOUT 300)
    add_executable(iiFileProvider_object_packager tests/object_packager.cpp)
    target_link_libraries(iiFileProvider_object_packager PRIVATE iiFileProvider::Objects)
    add_test(NAME iiFileProvider.object_packager COMMAND iiFileProvider_object_packager)
    set_tests_properties(iiFileProvider.object_packager PROPERTIES TIMEOUT 240)
    add_executable(iiFileProvider_object_packager_metadata tests/object_packager_metadata.cpp)
    target_link_libraries(iiFileProvider_object_packager_metadata PRIVATE iiFileProvider::Objects)
    add_test(NAME iiFileProvider.object_packager_metadata COMMAND iiFileProvider_object_packager_metadata)
    set_tests_properties(iiFileProvider.object_packager_metadata PROPERTIES TIMEOUT 240)
    add_executable(iiFileProvider_object_store tests/object_store.cpp)
    target_link_libraries(iiFileProvider_object_store PRIVATE iiFileProvider::Objects SQLite3::SQLite3)
    target_compile_definitions(iiFileProvider_object_store PRIVATE OBJECT_FIXTURES_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME iiFileProvider.object_store COMMAND iiFileProvider_object_store)
    set_tests_properties(iiFileProvider.object_store PROPERTIES TIMEOUT 60)
endif()
install(TARGETS iiFileProviderObjects EXPORT iiFileProviderObjectsTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/ObjectStore.h src/ObjectSource.h src/ObjectPackager.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT iiFileProviderObjectsTargets NAMESPACE iiFileProvider:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderObjects)
configure_package_config_file(cmake/iiFileProviderObjectsConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderObjectsConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderObjects)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderObjectsConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderObjects)
install(FILES docs/OBJECT_STORE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs)

if(IIFILEPROVIDER_BUILD_QT_API)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core)

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
    add_executable(iiFileProvider_object_authorship tests/object_authorship.cpp)
    target_compile_definitions(iiFileProvider_object_authorship PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(iiFileProvider_object_authorship PRIVATE iiFileProvider::Objects iiFileProvider::iiFileProvider)
    add_test(NAME iiFileProvider.object_authorship COMMAND iiFileProvider_object_authorship)
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
endif()
