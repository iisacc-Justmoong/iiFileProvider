if(NOT TARGET_TYPE STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "File compatibility shared build requested but target type is ${TARGET_TYPE}")
endif()
