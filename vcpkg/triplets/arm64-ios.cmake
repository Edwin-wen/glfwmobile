# iOS 真机 arm64。vcpkg 自带 scripts/toolchains/ios.cmake 会据此设置
# CMAKE_SYSTEM_NAME=iOS 与 CMAKE_OSX_ARCHITECTURES=arm64。
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
# autoconf 端口（icu）依赖这个 triplet 判断是否交叉编译。
# ICU 不认识 apple-ios 平台，所以 --host 用 darwin（走 mh-darwin），
# 再用不同的 --build 触发 cross 模式；实际编译器仍是 iOS clang。
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-apple-darwin;--build=x86_64-apple-darwin")