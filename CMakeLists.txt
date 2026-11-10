cmake_minimum_required(VERSION 3.24)
project(iiFileProvider VERSION 0.8.0 LANGUAGES C CXX)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF) # NDK kits omit clang-scan-deps; no named modules are used.
if(MSVC)
    add_compile_options(/utf-8)
endif()

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
install(DIRECTORY platform/android/src/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/android/src)
install(FILES cmake/iiFileProviderAndroid.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProvider)
install(FILES swift/IisaccFileProvider.swift DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/swift)
configure_package_config_file(cmake/iiFileProviderSwiftConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderSwiftConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderSwift)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderSwiftConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderSwiftConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderSwiftConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderSwift)
include(CTest)
include(cmake/SourceHeaders.cmake)
include(cmake/iiFileProviderSQLite.cmake)
include(cmake/TestWorkspace.cmake)
option(BUILD_SHARED_LIBS "Build iiFileProvider components as shared libraries" ON)
if(EMSCRIPTEN AND BUILD_SHARED_LIBS)
    set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
endif()
function(iifp_configure_shared target)
    if(NOT BUILD_SHARED_LIBS)
        return()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set_target_properties(${target} PROPERTIES FRAMEWORK TRUE FRAMEWORK_VERSION A
            MACOSX_FRAMEWORK_IDENTIFIER "com.iisacc.${target}")
    elseif(APPLE)
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@loader_path")
    elseif(UNIX AND NOT EMSCRIPTEN)
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE -fPIC -sSIDE_MODULE=1)
        target_link_options(${target} PRIVATE -sSIDE_MODULE=1)
    endif()
endfunction()
option(IIFILEPROVIDER_BUILD_NATIVE_API "Build the Qt-free streaming and C adapters" ON)
option(IIFILEPROVIDER_NATIVE_STATIC "Build a static native provider for embedded runtimes" OFF)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(IIFILEPROVIDER_BUILD_NATIVE_API AND NOT IIFILEPROVIDER_NATIVE_STATIC AND NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(_iifp_test_runner "${CMAKE_COMMAND}" -E env "IIFILEPROVIDER_NATIVE_LIBRARY=$<TARGET_FILE:iiFileProviderNative>"
            "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/resource_runner.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/python" "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    else()
        set(_iifp_test_runner "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/resource_runner.py"
            auto "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    endif()
    add_test(NAME iiFileProvider.ResourceWorkspace COMMAND ${_iifp_test_runner}
        "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resource_runner.py")
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME iiFileProvider.portable_sqlite
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DBINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/portable_sqlite.cmake")
    set_tests_properties(iiFileProvider.portable_sqlite PROPERTIES TIMEOUT 360)
endif()
if(IIFILEPROVIDER_BUILD_NATIVE_API)
    add_subdirectory(Native)
    install(DIRECTORY python/ii_file_provider DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/python PATTERN __pycache__ EXCLUDE)
endif()
option(IIFILEPROVIDER_BUILD_CORE_IO "Deprecated: build the Core I/O consumer separately" OFF)
if(IIFILEPROVIDER_BUILD_CORE_IO)
    message(FATAL_ERROR "Core I/O is a separate consumer project: configure example/CoreIO independently")
endif()
option(IIFILEPROVIDER_BUILD_QT_API "Build the existing Qt value/file adapters" ON)

add_library(iiFileProviderTemporary src/Temporary/TemporaryLease.cpp)
iifp_configure_shared(iiFileProviderTemporary)
add_library(iiFileProvider::Temporary ALIAS iiFileProviderTemporary)
if(EMSCRIPTEN)
    target_compile_options(iiFileProviderTemporary PUBLIC -pthread)
    target_compile_options(iiFileProviderTemporary PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>")
    target_link_options(iiFileProviderTemporary PUBLIC -pthread)
endif()
set_target_properties(iiFileProviderTemporary PROPERTIES EXPORT_NAME Temporary POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_features(iiFileProviderTemporary PUBLIC cxx_std_23)
target_include_directories(iiFileProviderTemporary PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS iiFileProviderTemporary EXPORT iiFileProviderTemporaryTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/Temporary/TemporaryLease.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT iiFileProviderTemporaryTargets NAMESPACE iiFileProvider:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderTemporary)
configure_package_config_file(cmake/iiFileProviderTemporaryConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderTemporaryConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderTemporary)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderTemporaryConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderTemporaryConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderTemporaryConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderTemporary)

# Native classification and provisioning reuse this SDK's already-vendored
# JSON and Unicode implementations; neither target imports a higher SDK or Qt.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Native/third_party/dependencies.json" _catalog_dependencies)
string(JSON _catalog_dependency_count LENGTH "${_catalog_dependencies}" files)
math(EXPR _catalog_dependency_last "${_catalog_dependency_count}-1")
foreach(_index RANGE 0 ${_catalog_dependency_last})
    string(JSON _file GET "${_catalog_dependencies}" files ${_index} file)
    string(JSON _expected GET "${_catalog_dependencies}" files ${_index} sha256)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/Native/third_party/${_file}" _actual)
    if(NOT _actual STREQUAL _expected)
        message(FATAL_ERROR "Pinned Catalog dependency mismatch: ${_file}")
    endif()
endforeach()
add_library(iiFileProviderCatalog src/Catalog/FileCatalog.cpp src/Catalog/FileSearchIndex.cpp Native/third_party/utf8proc/utf8proc.c)
iifp_configure_shared(iiFileProviderCatalog)
add_library(iiFileProvider::Catalog ALIAS iiFileProviderCatalog)
set_target_properties(iiFileProviderCatalog PROPERTIES EXPORT_NAME Catalog POSITION_INDEPENDENT_CODE ON
    CXX_EXTENSIONS OFF C_VISIBILITY_PRESET hidden WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_features(iiFileProviderCatalog PUBLIC cxx_std_23)
target_compile_definitions(iiFileProviderCatalog PRIVATE UTF8PROC_STATIC)
target_include_directories(iiFileProviderCatalog PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE Native/third_party Native/third_party/utf8proc)
target_link_libraries(iiFileProviderCatalog PRIVATE SQLite3::SQLite3 Threads::Threads iiFileProvider::Temporary)
if(TARGET iiFileProviderNative)
    target_link_libraries(iiFileProviderCatalog PRIVATE iiFileProvider::Native)
    target_compile_definitions(iiFileProviderCatalog PRIVATE IIFILEPROVIDER_HAS_PRESENTATION)
endif()
install(TARGETS iiFileProviderCatalog EXPORT iiFileProviderCatalogTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/Catalog/FileCatalog.h src/Catalog/FileSearchIndex.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT iiFileProviderCatalogTargets NAMESPACE iiFileProvider:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderCatalog)
configure_package_config_file(cmake/iiFileProviderCatalogConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderCatalogConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderCatalog)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderCatalogConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderCatalogConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderCatalogConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderCatalog)
install(FILES Native/third_party/licenses/nlohmann-json/LICENSE.MIT
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiFileProviderCatalog/nlohmann-json)
install(FILES Native/third_party/utf8proc/LICENSE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiFileProviderCatalog/utf8proc)
install(FILES docs/FILE_CATALOG.md docs/FILE_OWNERSHIP.md docs/FILE_CRUD.md docs/FILE_PRESENTATION.md docs/FILE_PRESENTATION_VALIDATION.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs)
install(FILES docs/FILE_SEARCH_INDEX.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs)
install(FILES docs/PROJECT_PACKAGES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs)
install(FILES docs/PROJECT_PACKAGES.agent.json DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs OPTIONAL)
install(FILES docs/FILE_SEARCH_INDEX.agent.json DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs OPTIONAL)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider)
install(FILES docs/OWNED_FILE_IO.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs)
set(_iifp_tools_default ON)
if(ANDROID OR EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_iifp_tools_default OFF)
endif()
option(IIFILEPROVIDER_BUILD_TOOLS "Build host command-line catalog/package tools" ${_iifp_tools_default})
if(IIFILEPROVIDER_BUILD_TOOLS)
add_executable(iiFileProviderCli tools/FileProvider.cpp)
set_target_properties(iiFileProviderCli PROPERTIES OUTPUT_NAME ii-file-provider CXX_EXTENSIONS OFF)
target_include_directories(iiFileProviderCli PRIVATE Native/third_party)
target_link_libraries(iiFileProviderCli PRIVATE iiFileProvider::Catalog)
if(MINGW)
    target_link_options(iiFileProviderCli PRIVATE -municode)
endif()
install(TARGETS iiFileProviderCli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

add_library(iiFileProviderObjects src/Objects/ObjectStore.cpp src/Objects/OntologyFileSystem.cpp src/Objects/ObjectMetadataCodec.cpp src/Objects/ObjectHash.cpp src/Objects/ObjectSource.cpp src/Objects/ObjectBatchRead.cpp src/Objects/ObjectSnapshotBatch.cpp src/Objects/ObjectPackager.cpp src/Objects/ObjectAudit.cpp)
iifp_configure_shared(iiFileProviderObjects)
add_library(iiFileProvider::Objects ALIAS iiFileProviderObjects)
if(EMSCRIPTEN)
    target_compile_options(iiFileProviderObjects PUBLIC -pthread)
    target_compile_options(iiFileProviderObjects PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>")
    target_link_options(iiFileProviderObjects PUBLIC -pthread)
endif()
set_target_properties(iiFileProviderObjects PROPERTIES EXPORT_NAME Objects POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_features(iiFileProviderObjects PUBLIC cxx_std_23)
target_include_directories(iiFileProviderObjects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE Native/third_party)
target_link_libraries(iiFileProviderObjects PRIVATE SQLite3::SQLite3 Threads::Threads iiFileProvider::Temporary)
if(IIFILEPROVIDER_BUILD_TOOLS)
add_executable(iiFileProviderObjectPackage tools/ObjectPackage.cpp tools/ObjectPackageOptions.cpp)
target_link_libraries(iiFileProviderObjectPackage PRIVATE iiFileProvider::Objects)
if(UNIX AND NOT EMSCRIPTEN)
    target_sources(iiFileProviderObjectPackage PRIVATE tools/PackageSignals.cpp)
    target_compile_definitions(iiFileProviderObjectPackage PRIVATE IIFILEPROVIDER_PACKAGE_POSIX_SIGNALS=1)
    target_link_libraries(iiFileProviderObjectPackage PRIVATE Threads::Threads)
endif()
install(TARGETS iiFileProviderObjectPackage RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(BUILD_TESTING)
    add_test(NAME iiFileProvider.SourceLayout COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/source_layout.cmake)
    add_executable(iiFileProvider_file_search_index tests/file_search_index.cpp)
    target_link_libraries(iiFileProvider_file_search_index PRIVATE iiFileProvider::Catalog Threads::Threads)
    iifp_fixture_test(iiFileProvider.file_search_index iiFileProvider_file_search_index)
    foreach(_test IN ITEMS object_document object_journal object_journal_storage object_metadata_limits ontology_filesystem ontology_storage ontology_boundaries)
        add_executable(iiFileProvider_${_test} tests/${_test}.cpp)
        target_compile_definitions(iiFileProvider_${_test} PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(iiFileProvider_${_test} PRIVATE iiFileProvider::Objects SQLite3::SQLite3 Threads::Threads)
        iifp_fixture_test(iiFileProvider.${_test} iiFileProvider_${_test})
        set_tests_properties(iiFileProvider.${_test} PROPERTIES TIMEOUT 120)
    endforeach()
    if(TARGET iiFileProviderNative)
        add_executable(iiFileProvider_presentation_catalog tests/presentation_catalog.cpp)
        target_include_directories(iiFileProvider_presentation_catalog PRIVATE Native/third_party)
        target_link_libraries(iiFileProvider_presentation_catalog PRIVATE iiFileProvider::Catalog iiFileProvider::Native)
        iifp_fixture_test(iiFileProvider.presentation_catalog iiFileProvider_presentation_catalog @WORK@)
    endif()
    add_executable(iiFileProvider_file_catalog tests/file_catalog.cpp)
    target_compile_definitions(iiFileProvider_file_catalog PRIVATE CATALOG_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(iiFileProvider_file_catalog PRIVATE iiFileProvider::Catalog SQLite3::SQLite3)
    iifp_fixture_test(iiFileProvider.file_catalog iiFileProvider_file_catalog)
    set_tests_properties(iiFileProvider.file_catalog PROPERTIES TIMEOUT 120)
    foreach(_test IN ITEMS catalog_reconciliation file_catalog_index file_catalog_safety file_catalog_packages)
        add_executable(iiFileProvider_${_test} tests/${_test}.cpp)
        target_compile_definitions(iiFileProvider_${_test} PRIVATE CATALOG_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(iiFileProvider_${_test} PRIVATE iiFileProvider::Catalog SQLite3::SQLite3 Threads::Threads)
        iifp_fixture_test(iiFileProvider.${_test} iiFileProvider_${_test})
        set_tests_properties(iiFileProvider.${_test} PROPERTIES TIMEOUT 180)
    endforeach()
    add_executable(iiFileProvider_file_catalog_objects tests/file_catalog_objects.cpp)
    target_compile_definitions(iiFileProvider_file_catalog_objects PRIVATE CATALOG_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(iiFileProvider_file_catalog_objects PRIVATE IIFILEPROVIDER_TEST_PRESENTATION=$<BOOL:${IIFILEPROVIDER_BUILD_NATIVE_API}>)
    target_link_libraries(iiFileProvider_file_catalog_objects PRIVATE iiFileProvider::Catalog iiFileProvider::Objects iiFileProvider::Temporary)
    iifp_fixture_test(iiFileProvider.file_catalog_objects iiFileProvider_file_catalog_objects)
    set_tests_properties(iiFileProvider.file_catalog_objects PROPERTIES TIMEOUT 120)
    add_executable(iiFileProvider_temporary_lease tests/temporary_lease.cpp)
    if(NOT WIN32)
        add_executable(iiFileProvider_temporary_anchor tests/temporary_anchor.cpp)
        target_link_libraries(iiFileProvider_temporary_anchor PRIVATE iiFileProvider::Temporary)
        iifp_fixture_test(iiFileProvider.temporary_anchor iiFileProvider_temporary_anchor)
    endif()
    target_link_libraries(iiFileProvider_temporary_lease PRIVATE iiFileProvider::Temporary Threads::Threads)
    iifp_fixture_test(iiFileProvider.temporary_lease iiFileProvider_temporary_lease)
    set_tests_properties(iiFileProvider.temporary_lease PROPERTIES TIMEOUT 30)
    add_executable(iiFileProvider_object_read_only tests/object_read_only.cpp)
    target_compile_definitions(iiFileProvider_object_read_only PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(iiFileProvider_object_read_only PRIVATE iiFileProvider::Objects SQLite3::SQLite3)
    iifp_fixture_test(iiFileProvider.object_read_only iiFileProvider_object_read_only)
    set_tests_properties(iiFileProvider.object_read_only PROPERTIES TIMEOUT 180)
    add_executable(iiFileProvider_object_package_options tests/object_package_options.cpp tools/ObjectPackageOptions.cpp)
    target_include_directories(iiFileProvider_object_package_options PRIVATE tools)
    target_link_libraries(iiFileProvider_object_package_options PRIVATE iiFileProvider::Objects)
    iifp_fixture_test(iiFileProvider.object_package_options iiFileProvider_object_package_options)
    set_tests_properties(iiFileProvider.object_package_options PROPERTIES TIMEOUT 30)
    if(UNIX AND NOT EMSCRIPTEN)
        add_executable(iiFileProvider_package_signals tests/object_package_signals.cpp tools/PackageSignals.cpp)
        target_compile_features(iiFileProvider_package_signals PRIVATE cxx_std_23)
        target_link_libraries(iiFileProvider_package_signals PRIVATE Threads::Threads)
        add_test(NAME iiFileProvider.package_signals COMMAND iiFileProvider_package_signals)
        set_tests_properties(iiFileProvider.package_signals PROPERTIES TIMEOUT 30)
    endif()
    add_executable(iiFileProvider_object_audit tests/object_audit.cpp)
    target_link_libraries(iiFileProvider_object_audit PRIVATE iiFileProvider::Objects SQLite3::SQLite3)
    target_compile_definitions(iiFileProvider_object_audit PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    iifp_fixture_test(iiFileProvider.object_audit iiFileProvider_object_audit)
    set_tests_properties(iiFileProvider.object_audit PROPERTIES TIMEOUT 360)
    add_executable(iiFileProvider_object_snapshot_identity tests/object_snapshot_identity.cpp)
    target_link_libraries(iiFileProvider_object_snapshot_identity PRIVATE iiFileProvider::Objects)
    target_compile_definitions(iiFileProvider_object_snapshot_identity PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    iifp_fixture_test(iiFileProvider.object_snapshot_identity iiFileProvider_object_snapshot_identity)
    add_executable(iiFileProvider_object_snapshot_batch tests/object_snapshot_batch.cpp)
    target_link_libraries(iiFileProvider_object_snapshot_batch PRIVATE iiFileProvider::Objects)
    target_compile_definitions(iiFileProvider_object_snapshot_batch PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    iifp_fixture_test(iiFileProvider.object_snapshot_batch iiFileProvider_object_snapshot_batch)
    set_tests_properties(iiFileProvider.object_snapshot_batch PROPERTIES TIMEOUT 180)
    add_executable(iiFileProvider_object_index tests/object_index.cpp)
    target_link_libraries(iiFileProvider_object_index PRIVATE iiFileProvider::Objects SQLite3::SQLite3)
    target_compile_definitions(iiFileProvider_object_index PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    iifp_fixture_test(iiFileProvider.object_index iiFileProvider_object_index)
    set_tests_properties(iiFileProvider.object_index PROPERTIES TIMEOUT 300)
    add_executable(iiFileProvider_object_batch_read tests/object_batch_read.cpp)
    target_link_libraries(iiFileProvider_object_batch_read PRIVATE iiFileProvider::Objects)
    iifp_fixture_test(iiFileProvider.object_batch_read iiFileProvider_object_batch_read)
    set_tests_properties(iiFileProvider.object_batch_read PROPERTIES TIMEOUT 120)
    add_executable(iiFileProvider_object_batch tests/object_batch.cpp)
    target_link_libraries(iiFileProvider_object_batch PRIVATE iiFileProvider::Objects)
    iifp_fixture_test(iiFileProvider.object_batch iiFileProvider_object_batch)
    set_tests_properties(iiFileProvider.object_batch PROPERTIES TIMEOUT 240)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME iiFileProvider.coreio_boundary COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/coreio_boundary.py)
    iifp_python_fixture_test(iiFileProvider.creation_audit
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/creation_audit.py @WORK@)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../Product" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../iiSDKCore")
        add_test(NAME iiFileProvider.workspace_creation COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/audit_file_creation.py --check)
        set_tests_properties(iiFileProvider.workspace_creation PROPERTIES TIMEOUT 600 LABELS "workspace;source-boundary")
    endif()
    if(IIFILEPROVIDER_BUILD_TOOLS)
    iifp_python_fixture_test(iiFileProvider.file_catalog_cli
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/file_catalog_cli.py $<TARGET_FILE:iiFileProviderCli> @WORK@)
    set_tests_properties(iiFileProvider.file_catalog_cli PROPERTIES TIMEOUT 180)
    iifp_python_fixture_test(iiFileProvider.object_package_cli
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/object_package_cli.py $<TARGET_FILE:iiFileProviderObjectPackage>)
    set_tests_properties(iiFileProvider.object_package_cli PROPERTIES TIMEOUT 300)
    endif()
    add_executable(iiFileProvider_object_packager tests/object_packager.cpp)
    target_link_libraries(iiFileProvider_object_packager PRIVATE iiFileProvider::Objects iiFileProvider::Temporary)
    iifp_fixture_test(iiFileProvider.object_packager iiFileProvider_object_packager)
    set_tests_properties(iiFileProvider.object_packager PROPERTIES TIMEOUT 240)
    add_executable(iiFileProvider_object_packager_metadata tests/object_packager_metadata.cpp)
    target_link_libraries(iiFileProvider_object_packager_metadata PRIVATE iiFileProvider::Objects)
    iifp_fixture_test(iiFileProvider.object_packager_metadata iiFileProvider_object_packager_metadata)
    set_tests_properties(iiFileProvider.object_packager_metadata PROPERTIES TIMEOUT 240)
    add_executable(iiFileProvider_object_store tests/object_store.cpp)
    target_link_libraries(iiFileProvider_object_store PRIVATE iiFileProvider::Objects iiFileProvider::Temporary SQLite3::SQLite3)
    target_compile_definitions(iiFileProvider_object_store PRIVATE OBJECT_FIXTURES_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    iifp_fixture_test(iiFileProvider.object_store iiFileProvider_object_store)
    set_tests_properties(iiFileProvider.object_store PROPERTIES TIMEOUT 60)
endif()
install(TARGETS iiFileProviderObjects EXPORT iiFileProviderObjectsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES src/Objects/ObjectStore.h src/Objects/ObjectSource.h src/Objects/ObjectPackager.h src/Objects/OntologyFileSystem.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT iiFileProviderObjectsTargets NAMESPACE iiFileProvider:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderObjects)
configure_package_config_file(cmake/iiFileProviderObjectsConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderObjectsConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderObjects)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderObjectsConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderObjects)
install(FILES docs/OBJECT_STORE.md docs/FILE_JOURNAL.md docs/FILE_JOURNAL_VALIDATION.md docs/ONTOLOGY_FILESYSTEM.md docs/ONTOLOGY_VALIDATION.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs)

if(BUILD_TESTING AND BUILD_SHARED_LIBS AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME iiFileProvider.shared_target_types COMMAND "${CMAKE_COMMAND}"
        "-DTEMP_TYPE=$<TARGET_PROPERTY:iiFileProviderTemporary,TYPE>"
        "-DCATALOG_TYPE=$<TARGET_PROPERTY:iiFileProviderCatalog,TYPE>"
        "-DOBJECTS_TYPE=$<TARGET_PROPERTY:iiFileProviderObjects,TYPE>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_target_types.cmake")
    foreach(_pair IN ITEMS "temporary_lease;iiFileProviderTemporary" "file_search_index;iiFileProviderCatalog" "object_store;iiFileProviderObjects")
        list(GET _pair 0 _test_target)
        list(GET _pair 1 _library_target)
        add_test(NAME "iiFileProvider.shared_link.${_library_target}" COMMAND "${CMAKE_COMMAND}"
            "-DIMAGE=$<TARGET_FILE:iiFileProvider_${_test_target}>"
            "-DEXPECTED=$<TARGET_FILE:${_library_target}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_link.cmake")
    endforeach()
endif()

if(BUILD_TESTING AND IIFILEPROVIDER_BUILD_NATIVE_API AND NOT CMAKE_CROSSCOMPILING)
    iifp_python_fixture_test(iiFileProvider.python_adapter
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/python_adapter.py $<TARGET_FILE:iiFileProviderNative> @WORK@)
endif()
if(IIFILEPROVIDER_BUILD_QT_API)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core)
if(NOT TARGET iiFileProvider::Native)
    find_package(iiFileProviderNative 0.10.0 CONFIG REQUIRED)
endif()

set(_iifp_qt_library_kind SHARED)
if(NOT BUILD_SHARED_LIBS)
    set(_iifp_qt_library_kind STATIC)
endif()
add_library(iiFileProvider ${_iifp_qt_library_kind} src/API/iiFileProvider.h src/API/iiFileProvider.cpp
    src/API/Export.h src/Identity/AuthenticationToken.h src/Identity/AuthenticationToken.cpp src/Identity/FileAuthor.h src/Identity/FileAuthor.cpp src/API/JsonContract.h
    src/Identity/Authorship.h src/Identity/Authorship.cpp src/Identity/FileLink.h src/Identity/FileLink.cpp src/Files/File.h src/Files/File.cpp src/Files/AtomicFileCommit.cpp
    src/Catalog/Database.h src/Catalog/Database.cpp src/Files/StagedFile.h src/Files/StagedFile.cpp src/Files/iiFileProviderDevice.h src/Files/FileDevice.cpp)
iifp_configure_shared(iiFileProvider)
add_library(iiFileProvider::iiFileProvider ALIAS iiFileProvider)
if(BUILD_TESTING)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Gui)
    if(WIN32)
        add_library(iiFileProviderWindowsHeaderMacros OBJECT tests/windows_header_macros.cpp)
        target_link_libraries(iiFileProviderWindowsHeaderMacros PRIVATE iiFileProvider::iiFileProvider)
    endif()
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
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
    add_executable(iiFileProvider_file_devices tests/file_devices.cpp tests/device_resource.qrc)
    set_target_properties(iiFileProvider_file_devices PROPERTIES AUTORCC ON)
    target_link_libraries(iiFileProvider_file_devices PRIVATE iiFileProvider Qt6::Gui)
    iifp_fixture_test(iiFileProvider.file_devices iiFileProvider_file_devices @WORK@)
endif()
target_compile_features(iiFileProvider PUBLIC cxx_std_23)
target_compile_definitions(iiFileProvider PRIVATE IIFILEPROVIDER_BUILDING_LIBRARY)
if(_iifp_qt_library_kind STREQUAL "STATIC")
    target_compile_definitions(iiFileProvider PUBLIC IIFILEPROVIDER_STATIC)
endif()
target_link_libraries(iiFileProvider PUBLIC Qt6::Core iiFileProvider::Catalog)
target_link_libraries(iiFileProvider PRIVATE SQLite3::SQLite3 iiFileProvider::Temporary)
target_link_libraries(iiFileProvider PRIVATE iiFileProvider::Native)
target_compile_definitions(iiFileProvider PRIVATE IIFILEPROVIDER_HAS_PRESENTATION)
target_include_directories(iiFileProvider PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(iiFileProvider PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    # File devices now inherit QIODevice directly; never load them as the old QFile ABI.
    SOVERSION 0.8
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(iiFileProvider_source_access tests/source_access.cpp)
    target_link_libraries(iiFileProvider_source_access PRIVATE iiFileProvider::iiFileProvider iiFileProvider::Native Threads::Threads)
    iifp_fixture_test(iiFileProvider.source_access iiFileProvider_source_access @WORK@)
    set_tests_properties(iiFileProvider.source_access PROPERTIES TIMEOUT 20)
    add_executable(iiFileProvider_object_authorship tests/object_authorship.cpp)
    target_compile_definitions(iiFileProvider_object_authorship PRIVATE OBJECT_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(iiFileProvider_object_authorship PRIVATE iiFileProvider::Objects iiFileProvider::iiFileProvider)
    iifp_fixture_test(iiFileProvider.object_authorship iiFileProvider_object_authorship)
    add_test(NAME iiFileProvider.dependency_direction
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dependency_direction.py)
    add_executable(iiFileProvider_database tests/database.cpp)
    target_link_libraries(iiFileProvider_database PRIVATE iiFileProvider::iiFileProvider)
    iifp_fixture_test(iiFileProvider.database iiFileProvider_database)
    add_executable(iiFileProvider_file_crud tests/file_crud.cpp)
    target_link_libraries(iiFileProvider_file_crud PRIVATE iiFileProvider::iiFileProvider iiFileProvider::Temporary)
    iifp_fixture_test(iiFileProvider.file_crud iiFileProvider_file_crud)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    add_executable(iiFileProvider_authorship tests/authorship.cpp)
    target_link_libraries(iiFileProvider_authorship PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
    set_target_properties(iiFileProvider_authorship PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
    iifp_fixture_test(iiFileProvider.authorship iiFileProvider_authorship)
    add_executable(iiFileProvider_file_links tests/file_links.cpp)
    target_link_libraries(iiFileProvider_file_links PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
    set_target_properties(iiFileProvider_file_links PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
    iifp_fixture_test(iiFileProvider.file_links iiFileProvider_file_links)
    set_tests_properties(iiFileProvider.file_links PROPERTIES TIMEOUT 30)
    add_executable(iiFileProvider_author_contract tests/author_contract.cpp)
    target_link_libraries(iiFileProvider_author_contract PRIVATE iiFileProvider::iiFileProvider Qt6::Test)
    set_target_properties(iiFileProvider_author_contract PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)
    iifp_fixture_test(iiFileProvider.author_contract iiFileProvider_author_contract)
    set_tests_properties(iiFileProvider.author_contract PROPERTIES TIMEOUT 30)
    add_executable(iiFileProvider_hello_world tests/hello_world.cpp)
    target_link_libraries(iiFileProvider_hello_world PRIVATE iiFileProvider::iiFileProvider)
    set_target_properties(iiFileProvider_hello_world PROPERTIES CXX_EXTENSIONS OFF)
    iifp_fixture_test(iiFileProvider.hello_world iiFileProvider_hello_world)
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
    FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES src/API/iiFileProvider.h src/API/Export.h src/Identity/AuthenticationToken.h src/Identity/FileAuthor.h src/Identity/Authorship.h src/Identity/FileLink.h src/Files/File.h src/Files/iiFileProviderDevice.h src/Catalog/Database.h src/Files/StagedFile.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT iiFileProviderTargets NAMESPACE iiFileProvider:: DESTINATION "${_package_dir}")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderConfigVersion.cmake"
    DESTINATION "${_package_dir}"
)
install(FILES docs/FILE_AUTHOR_CONTRACT.md DESTINATION "${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs")
install(FILES docs/FILE_LINKS.md DESTINATION "${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs")
install(FILES docs/PERFORMANCE.md DESTINATION "${CMAKE_INSTALL_DATADIR}/iiFileProvider/docs")

endif()
