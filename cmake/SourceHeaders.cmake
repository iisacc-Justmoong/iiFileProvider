# Preserve flat public include names for responsibility-grouped authored headers.
foreach(_area IN ITEMS API Files Identity Catalog Objects Temporary)
    file(GLOB _headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/${_area}/*.h")
    foreach(_header IN LISTS _headers)
        get_filename_component(_name "${_header}" NAME)
        if(DEFINED _iifp_header_${_name})
            message(FATAL_ERROR "Duplicate iiFileProvider header basename: ${_name}")
        endif()
        set("_iifp_header_${_name}" "${_header}")
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/${_name}"
            CONTENT "#pragma once\n#include \"${_header}\"\n")
    endforeach()
endforeach()
