if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(NOT DEFINED BINARY_ROOT)
    set(BINARY_ROOT "")
endif()

function(iifp_read path out)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing license input: ${path}")
    endif()
    file(READ "${path}" text)
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(iifp_require text needle label)
    string(FIND "${text}" "${needle}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${label} does not contain: ${needle}")
    endif()
endfunction()

function(iifp_reject text needle label)
    string(FIND "${text}" "${needle}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "${label} still contains: ${needle}")
    endif()
endfunction()

iifp_read("${SOURCE_ROOT}/LICENSE" license)
iifp_require("${license}" "Apache License" "LICENSE")
iifp_require("${license}" "Version 2.0, January 2004" "LICENSE")
iifp_require("${license}" "TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION" "LICENSE")
iifp_require("${license}" "END OF TERMS AND CONDITIONS" "LICENSE")
iifp_require("${license}" "APPENDIX: How to apply the Apache License to your work." "LICENSE")
iifp_reject("${license}" "Permission is hereby granted" "LICENSE")
iifp_reject("${license}" "MIT License" "LICENSE")

iifp_read("${SOURCE_ROOT}/NOTICE" notice)
iifp_require("${notice}" "Copyright (c) 2026 iisacc" "NOTICE")
iifp_require("${notice}" "Apache License" "NOTICE")
iifp_require("${notice}" "AGPL-3.0-or-later" "NOTICE")

iifp_read("${SOURCE_ROOT}/README.md" readme)
iifp_require("${readme}" "SPDX-License-Identifier: Apache-2.0" "README.md")
iifp_reject("${readme}" "SPDX-License-Identifier: MIT" "README.md")
iifp_reject("${readme}" "MIT License" "README.md")

iifp_read("${SOURCE_ROOT}/docs/License.md" korean)
iifp_require("${korean}" "SPDX-License-Identifier: Apache-2.0" "docs/License.md")
iifp_require("${korean}" "Copyright (c) 2026 iisacc" "docs/License.md")
iifp_require("${korean}" "AGPL-3.0-or-later" "docs/License.md")

iifp_read("${SOURCE_ROOT}/AGENT.md" agent)
iifp_require("${agent}" "Apache License 2.0" "AGENT.md")
iifp_require("${agent}" "Copyright (c) 2026 iisacc" "AGENT.md")
iifp_require("${agent}" "AGPL-3.0-or-later" "AGENT.md")

iifp_read("${SOURCE_ROOT}/Native/README.md" native_readme)
iifp_require("${native_readme}" "Apache-2.0" "Native/README.md")
iifp_reject("${native_readme}" "MIT license" "Native/README.md")

iifp_read("${SOURCE_ROOT}/Native/src/Crypto/Sha256.cpp" sha)
iifp_require("${sha}" "SPDX-License-Identifier: Apache-2.0" "Sha256.cpp")
iifp_reject("${sha}" "MIT" "Sha256.cpp")

iifp_read("${SOURCE_ROOT}/Native/third_party/licenses/iisacc-counterpart/LICENSE" counterpart)
if(NOT counterpart STREQUAL license)
    message(FATAL_ERROR "First-party counterpart LICENSE is not the Apache-2.0 text")
endif()
iifp_read("${SOURCE_ROOT}/Native/third_party/licenses/iisacc-counterpart/NOTICE" counterpart_notice)
iifp_require("${counterpart_notice}" "Copyright (c) 2026 iisacc" "counterpart NOTICE")
iifp_require("${counterpart_notice}" "Apache License, Version 2.0" "counterpart NOTICE")

foreach(config IN ITEMS
        cmake/iiFileProviderConfig.cmake.in
        cmake/iiFileProviderCatalogConfig.cmake.in
        cmake/iiFileProviderObjectsConfig.cmake.in
        cmake/iiFileProviderTemporaryConfig.cmake.in
        cmake/iiFileProviderSwiftConfig.cmake.in
        cmake/iiFileProviderSQLite3Config.cmake.in
        Native/iiFileProviderNativeConfig.cmake.in
        Compatibility/iiFileProviderCompatibilityConfig.cmake.in
        example/CoreIO/iiFileProviderCoreIOConfig.cmake.in)
    iifp_read("${SOURCE_ROOT}/${config}" body)
    if(config STREQUAL "cmake/iiFileProviderSQLite3Config.cmake.in")
        iifp_reject("${body}" "Apache-2.0" "${config}")
    else()
        iifp_require("${body}" "_SPDX_LICENSE \"Apache-2.0\"" "${config}")
    endif()
endforeach()

foreach(list IN ITEMS CMakeLists.txt Native/CMakeLists.txt Compatibility/CMakeLists.txt example/CoreIO/CMakeLists.txt)
    iifp_read("${SOURCE_ROOT}/${list}" body)
    iifp_require("${body}" "_SPDX_LICENSE \"Apache-2.0\"" "${list}")
    iifp_require("${body}" "LICENSE" "${list}")
endforeach()

# Vendored notices stay with their upstream terms.
iifp_read("${SOURCE_ROOT}/Native/third_party/utf8proc/LICENSE.md" utf8)
iifp_require("${utf8}" "MIT" "utf8proc")
iifp_read("${SOURCE_ROOT}/Native/third_party/licenses/nlohmann-json/LICENSE.MIT" json_license)
iifp_require("${json_license}" "MIT License" "nlohmann-json")
iifp_require("${json_license}" "Niels Lohmann" "nlohmann-json")
iifp_read("${SOURCE_ROOT}/Native/third_party/stb/LICENSE" stb)
iifp_require("${stb}" "MIT License" "stb")
iifp_require("${stb}" "Sean Barrett" "stb")
iifp_read("${SOURCE_ROOT}/Native/third_party/sqlite/LICENSE" sqlite)
iifp_require("${sqlite}" "Public domain" "sqlite")
iifp_reject("${sqlite}" "Apache License" "sqlite")

if(NOT BINARY_ROOT STREQUAL "")
    foreach(generated IN ITEMS
            iiFileProviderCatalogConfig.cmake
            iiFileProviderObjectsConfig.cmake
            iiFileProviderTemporaryConfig.cmake
            iiFileProviderSwiftConfig.cmake
            Native/iiFileProviderNativeConfig.cmake)
        set(path "${BINARY_ROOT}/${generated}")
        if(EXISTS "${path}")
            iifp_read("${path}" body)
            iifp_require("${body}" "_SPDX_LICENSE \"Apache-2.0\"" "${generated}")
        endif()
    endforeach()
    if(EXISTS "${BINARY_ROOT}/iiFileProviderConfig.cmake")
        iifp_read("${BINARY_ROOT}/iiFileProviderConfig.cmake" body)
        iifp_require("${body}" "iiFileProvider_SPDX_LICENSE \"Apache-2.0\"" "iiFileProviderConfig.cmake")
    endif()
endif()
