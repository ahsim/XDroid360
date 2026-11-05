# libstfsextract.so: STFS package extraction for the XDroid360 launcher, loaded in-process
# through JNI (stfs_jni.cpp; recipe format v2, "STFS extraction"). On the host, stfs_cli, the
# test tool over the same reader. Self-contained: no runtime, no SDL.
#
#   Android: cmake -G Ninja -S stfsextract -B build/stfsextract \
#              -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#              -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
#   host:    cmake -G Ninja -S stfsextract -B build/stfsextract_host -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.22)
project(StfsExtract CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(stfsreader STATIC stfs_reader.cpp)
target_include_directories(stfsreader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (ANDROID)
    add_library(stfsextract SHARED stfs_jni.cpp)
    target_link_libraries(stfsextract PRIVATE stfsreader log)
    target_compile_options(stfsextract PRIVATE -fvisibility=hidden -Wall -Wextra)
    target_link_options(stfsextract PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
else()
    add_executable(stfs_cli stfs_cli.cpp)
    target_link_libraries(stfs_cli PRIVATE stfsreader)
endif()
