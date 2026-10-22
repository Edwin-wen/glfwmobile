# Android 宿主：只负责 JNI 与把内存交给 canvas_core，绘制逻辑全在 canvasCore/。
cmake_minimum_required(VERSION 3.18)
project(skia_canvas CXX)

set(SKC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
add_subdirectory(${SKC_DIR}/canvasCore canvasCore)

add_library(skia_canvas SHARED skia_canvas_jni.cpp)
target_include_directories(skia_canvas PRIVATE ${SKC_DIR}/canvasCore/include)
target_link_libraries(skia_canvas canvas_core jnigraphics android log)