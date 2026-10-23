# 把 OpenHarmony SDK 自带的官方 CMake toolchain 接到 vcpkg 的链式 toolchain。
#
# 必须使用 SDK 自带的 CMake（native/build-tools/cmake，3.28+），
# 因为系统 CMake 没有 Platform/OHOS.cmake。
if(NOT DEFINED ENV{OHOS_SDK_NATIVE})
    message(FATAL_ERROR
        "OHOS_SDK_NATIVE is required, e.g. "
        "/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native")
endif()

set(OHOS_SDK_NATIVE "$ENV{OHOS_SDK_NATIVE}" CACHE PATH "")
set(OHOS_ARCH "arm64-v8a" CACHE STRING "")
set(OHOS_STL "c++_shared" CACHE STRING "")

include("${OHOS_SDK_NATIVE}/build/cmake/ohos.toolchain.cmake")

# 官方 toolchain 只设置 CMAKE_C_COMPILER_TARGET / CMAKE_SYSROOT，
# CMake 正常编译会带上，但端口里直接 execute_process(${CMAKE_C_COMPILER})
# 的代码生成步骤（libpng 等）不会。把 target/sysroot 显式并入通用 flags。
set(_ohos_target_flags "--target=aarch64-linux-ohos --sysroot=${OHOS_SDK_NATIVE}/sysroot")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_ohos_target_flags}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_ohos_target_flags}")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${_ohos_target_flags}")