find_package(iiFileProviderNative 0.8 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiFileProvider")
add_library(iiFileProviderCompatibility STATIC src/Compatibility.cpp)
add_library(iiFileProvider::Compatibility ALIAS iiFileProviderCompatibility)
target_compile_features(iiFileProviderCompatibility PUBLIC cxx_std_23)
target_include_directories(iiFileProviderCompatibility PUBLIC src)
target_link_libraries(iiFileProviderCompatibility PUBLIC iiFileProvider::Native)
set_target_properties(iiFileProviderCompatibility PROPERTIES
        AUTOMOC OFF AUTORCC OFF AUTOUIC OFF POSITION_INDEPENDENT_CODE ON)
