set(stage "${BUILD_ROOT}/windows-native-runtime")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_ROOT}"
 --config "${CONFIG}" --prefix "${stage}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
 message(FATAL_ERROR "Native runtime staging failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${PYTHON}" -c "import ctypes; ctypes.CDLL(r'${stage}/bin/${LIBRARY_NAME}')"
 RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
 message(FATAL_ERROR "Installed native runtime did not load: ${output}\n${error}")
endif()
