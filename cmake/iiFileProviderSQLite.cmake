include_guard(GLOBAL)

set(_iifp_bundle_sqlite_default OFF)
if(ANDROID OR EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_iifp_bundle_sqlite_default ON)
endif()
option(IIFILEPROVIDER_USE_BUNDLED_SQLITE
    "Build the pinned SQLite source for targets without a system SQLite SDK"
    ${_iifp_bundle_sqlite_default})
find_package(Threads REQUIRED)
if(NOT IIFILEPROVIDER_USE_BUNDLED_SQLITE)
    block(SCOPE_FOR VARIABLES)
        set(CMAKE_FIND_FRAMEWORK LAST)
        find_package(SQLite3 3.27 REQUIRED)
    endblock()
    if(NOT TARGET SQLite3::SQLite3)
        add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
    endif()
    return()
endif()

set(_iifp_sqlite_source "${CMAKE_CURRENT_LIST_DIR}/../Native/third_party/sqlite")
add_library(iiFileProviderSQLite3 STATIC "${_iifp_sqlite_source}/sqlite3.c")
add_library(SQLite3::SQLite3 ALIAS iiFileProviderSQLite3)
add_library(SQLite::SQLite3 ALIAS iiFileProviderSQLite3)
set_target_properties(iiFileProviderSQLite3 PROPERTIES EXPORT_NAME SQLite3
    POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_compile_definitions(iiFileProviderSQLite3 PRIVATE
    SQLITE_ENABLE_FTS5 SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
target_include_directories(iiFileProviderSQLite3 PUBLIC
    $<BUILD_INTERFACE:${_iifp_sqlite_source}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/iiFileProvider/sqlite>)
target_link_libraries(iiFileProviderSQLite3 PRIVATE Threads::Threads)
if(EMSCRIPTEN)
    target_compile_options(iiFileProviderSQLite3 PUBLIC -pthread)
    target_compile_options(iiFileProviderSQLite3 PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>")
    target_link_options(iiFileProviderSQLite3 PUBLIC -pthread)
endif()
if(UNIX)
    target_link_libraries(iiFileProviderSQLite3 PRIVATE m)
endif()
install(TARGETS iiFileProviderSQLite3 EXPORT iiFileProviderSQLite3Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "${_iifp_sqlite_source}/sqlite3.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiFileProvider/sqlite)
install(EXPORT iiFileProviderSQLite3Targets NAMESPACE SQLite3::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderSQLite3)
configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/iiFileProviderSQLite3Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderSQLite3Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderSQLite3)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiFileProviderSQLite3Config.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iiFileProviderSQLite3)
install(FILES "${_iifp_sqlite_source}/LICENSE" "${_iifp_sqlite_source}/provenance.json"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/iiFileProviderNative/sqlite)
