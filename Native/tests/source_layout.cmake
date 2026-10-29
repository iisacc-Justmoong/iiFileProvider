foreach(_file Core/iiFileProviderNative.h Core/File.cpp Filesystem/iiFileProviderNativeDevice.h
        Filesystem/Device.cpp Filesystem/AtomicPublication.h Filesystem/AtomicPublication.cpp
        Storage/iiFileProviderStorage.h Storage/Storage.cpp Database/iiFileProviderDatabase.h
        Database/Database.cpp Presentation/iiFileProviderPresentation.h Presentation/Presentation.cpp)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing responsibility-grouped native source: ${_file}")
    endif()
endforeach()
