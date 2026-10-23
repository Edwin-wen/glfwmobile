# 关键取舍：vcpkg 没有 OHOS 平台分支，这里让 vcpkg 以 Android 语义处理端口，
# 只是为了触发 meson 的 cross 模式（build/host 架构相同，vcpkg 会误判为原生）。
# 真正的系统名、编译器、sysroot 由链式 toolchain 覆盖为 OHOS。
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_CMAKE_SYSTEM_VERSION 1)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/ohos.cmake")

# OHOS libc 是 musl，Skia 与部分端口需要这个宏来选择正确分支。
# --target/--sysroot 同时塞进 CFLAGS：meson/autotools 端口不像 CMake 端口
# 会自动读取 CMAKE_C_COMPILER_TARGET / CMAKE_SYSROOT。
set(_ohos_sdk "$ENV{OHOS_SDK_NATIVE}")
set(VCPKG_C_FLAGS "-D__MUSL__ --target=aarch64-linux-ohos --sysroot=${_ohos_sdk}/sysroot")
set(VCPKG_CXX_FLAGS "-D__MUSL__ --target=aarch64-linux-ohos --sysroot=${_ohos_sdk}/sysroot")

# 静态库为主，且跨端产物不需要在 macOS 上改 ELF rpath；
# 关闭后也避免依赖 Homebrew 的 patchelf。
set(VCPKG_FIXUP_ELF_RPATH OFF)

# autotools 端口（icu）用 Linux host triplet 走 mh-linux；
# 用不同的 --build 触发 cross 模式，避免 configure 运行 OHOS 可执行文件。
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-gnu;--build=aarch64-apple-darwin")