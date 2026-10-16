#!/usr/bin/env bash
set -euo pipefail

source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${source_dir}/build"
install_prefix="${INSTALL_PREFIX:-${HOME}/.local/SDK/iiFileProvider}"
qt_api="${IIFILEPROVIDER_BUILD_QT_API:-ON}"
case "$qt_api" in ON|OFF) ;; *) echo "IIFILEPROVIDER_BUILD_QT_API must be ON or OFF." >&2; exit 2 ;; esac
qt_prefix="${QT_PREFIX_PATH:-}"
if [[ -z "$qt_prefix" && -d "/Volumes/Storage/Qt/6.8.3/macos" ]]; then
    qt_prefix="/Volumes/Storage/Qt/6.8.3/macos"
fi
cmake_prefix_path="${CMAKE_PREFIX_PATH:-}"
cmake_prefix_path="${cmake_prefix_path//:/;}"
if [[ -n "$qt_prefix" ]]; then
    cmake_prefix_path="${qt_prefix}${cmake_prefix_path:+;${cmake_prefix_path}}"
fi
if [[ $# -ne 0 ]]; then
    echo "Use INSTALL_PREFIX, QT_PREFIX_PATH, and CMAKE_PREFIX_PATH environment variables." >&2
    exit 2
fi
if [[ "$install_prefix" != /* ]]; then
    echo "INSTALL_PREFIX must be an absolute path." >&2
    exit 2
fi

cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DIIFILEPROVIDER_BUILD_QT_API="$qt_api" \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" \
    -DCMAKE_PREFIX_PATH="$cmake_prefix_path"
cmake --build "$build_dir" --config Release --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "$build_dir" -C Release --output-on-failure
cmake --install "$build_dir" --config Release

if [[ "$qt_api" == "ON" ]]; then
    consumer_build_dir="${build_dir}/consumer/build"
    cmake -S "${source_dir}/tests/consumer" -B "$consumer_build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DiiFileProvider_DIR="${install_prefix}/lib/cmake/iiFileProvider" \
        -DCMAKE_PREFIX_PATH="${install_prefix}${cmake_prefix_path:+;${cmake_prefix_path}}"
    cmake --build "$consumer_build_dir" --config Release --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
    ctest --test-dir "$consumer_build_dir" -C Release --output-on-failure
fi

for contract in objects catalog; do
    case "$contract" in objects) package=iiFileProviderObjects ;; catalog) package=iiFileProviderCatalog ;; esac
    consumer_build_dir="${build_dir}/${contract}-consumer/build"
    cmake -S "${source_dir}/tests/${contract}_consumer" -B "$consumer_build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$install_prefix" \
        "-D${package}_DIR=${install_prefix}/lib/cmake/${package}"
    cmake --build "$consumer_build_dir" --config Release --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
    ctest --test-dir "$consumer_build_dir" -C Release --output-on-failure
done
