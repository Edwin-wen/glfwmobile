# canvas_core：跨端唯一的 Skia 绘制实现。
#
# SKIA_DIR 由调用方传入（指向含 include/ 与 lib/libskia.a 的 Skia 安装目录），
# 平台工程只需要 add_subdirectory 本文件，不需要关心 Skia 的头文件布局。
cmake_minimum_required(VERSION 3.18)
project(canvas_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT SKIA_DIR)
    message(FATAL_ERROR "canvas_core: SKIA_DIR is required (e.g. -DSKIA_DIR=/path/to/skia)")
endif()

if(NOT SKIA_LIBS)
    # vcpkg/自建安装目录下是「Skia + 其静态依赖（freetype/harfbuzz/icu/png/z/...）」，
    # 统一收集；未被引用的归档不会被打进产物。
    file(GLOB SKIA_LIBS "${SKIA_DIR}/lib/*.a")
endif()

add_library(canvas_core STATIC
        src/scene.cpp
        src/painter.cpp
        src/canvas_core.cpp)

target_include_directories(canvas_core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        # Skia 头文件互相引用 "include/core/xxx.h"，因此需要这个根
        ${SKIA_DIR}/include/skia
        ${SKIA_DIR}/include)

# Skia 以静态库形式链接，符号只在本模块内部使用（对外只暴露 canvas_core.h 的 C ABI）。
# 静态库之间存在互相引用（skia <-> freetype/harfbuzz），用 start-group 消除顺序依赖。
if(ANDROID OR OHOS OR CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    target_link_libraries(canvas_core PUBLIC -Wl,--start-group ${SKIA_LIBS} -Wl,--end-group)
else()
    target_link_libraries(canvas_core PUBLIC ${SKIA_LIBS})
endif()