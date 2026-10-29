foreach(_file API/iiFileProvider.h API/iiFileProvider.cpp Files/File.h Files/File.cpp
        Catalog/FileCatalog.h Catalog/FileCatalog.cpp Catalog/FileSearchIndex.h Catalog/FileSearchIndex.cpp
        Identity/Authorship.h Identity/Authorship.cpp Objects/ObjectStore.h Objects/ObjectStore.cpp
        Temporary/TemporaryLease.h Temporary/TemporaryLease.cpp)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing responsibility-grouped source: ${_file}")
    endif()
endforeach()
