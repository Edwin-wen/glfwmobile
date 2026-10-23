# Android 宿主：只负责 JNI 与把内存交给 canvas_core，绘制逻辑全在 cppCore/canvasCore/。
cmake_minimum_required(VERSION 3.18)
project(skia_canvas CXX)

# androidApp/src/main/cpp -> 仓库根目录
set(SKC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
add_subdirectory(${SKC_DIR}/cppCore/canvasCore canvasCore)

add_library(skia_canvas SHARED skia_canvas_jni.cpp)
target_include_directories(skia_canvas PRIVATE ${SKC_DIR}/cppCore/canvasCore/include)
target_link_libraries(skia_canvas canvas_core jnigraphics android log)