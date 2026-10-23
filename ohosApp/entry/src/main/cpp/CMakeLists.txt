# the minimum version of CMake.
cmake_minimum_required(VERSION 3.4.1)
project(HarmonyDemo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if (DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif ()

include_directories(${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/include)

add_library(kuikly_render SHARED IMPORTED)
set_target_properties(kuikly_render
        PROPERTIES
        IMPORTED_LOCATION ${NATIVERENDER_ROOT_PATH}/../../../oh_modules/@kuikly-open/render/libs/${OHOS_ARCH}/libkuikly.so)

add_library(kuikly_shared SHARED IMPORTED)
set_target_properties(kuikly_shared
        PROPERTIES
        IMPORTED_LOCATION ${NATIVERENDER_ROOT_PATH}/../../../libs/${OHOS_ARCH}/libshared.so)

# 跨端 Skia 绘制核心：与 Android / iOS 共用同一份 C++ 实现。
# Skia 产物由仓库根 vcpkg/installed/ohos/arm64-ohos 提供（不入库）。
set(SKC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../cppCore/canvasCore)
set(SKIA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../vcpkg/installed/ohos/arm64-ohos)
add_subdirectory(${SKC_ROOT} canvasCore)

add_library(entry SHARED napi_init.cpp canvas_gl_render.cpp)
target_link_libraries(entry PUBLIC libace_napi.z.so libace_ndk.z.so libhilog_ndk.z.so libEGL.so libGLESv3.so libnative_window.so kuikly_shared kuikly_render canvas_core)