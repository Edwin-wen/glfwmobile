Pod::Spec.new do |s|
  s.name             = 'canvas_core'
  s.version          = '1.0.0'
  s.summary          = 'Cross-platform Skia canvas core for Kuikly (C++ / C ABI).'
  s.description      = <<-DESC
                         canvas_core 提供唯一的 C ABI（canvas_core.h），
                         Android / iOS / 鸿蒙共用同一份 Skia 绘制实现。
                         iOS 侧通过本 pod 编译 C++ 源码并链接仓库内 vcpkg 的 Skia 静态库。
                       DESC
  s.homepage         = 'https://github.com/edwin/glfwmobile'
  s.license          = { :type => 'MIT' }
  s.author           = { 'edwin' => 'edwin@example.com' }
  s.source           = { :git => 'https://github.com/edwin/glfwmobile.git', :tag => s.version.to_s }

  s.platform         = :ios, '14.1'
  s.static_framework = true
  s.requires_arc     = true

  s.source_files         = 'include/**/*.h', 'src/**/*.{h,cpp}', 'third_party/**/*.h'
  s.public_header_files  = 'include/**/*.h'
  s.preserve_paths       = 'third_party/*.h'

  # Skia 产物由仓库根 vcpkg/installed/ios/arm64-ios 提供（不入库）
  repo_root = File.expand_path('../..', __dir__)
  skia_root = File.join(repo_root, 'vcpkg/installed/ios/arm64-ios')

  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY'           => 'libc++',
    'HEADER_SEARCH_PATHS'         => [
      '"$(PODS_TARGET_SRCROOT)/include"',
      '"$(PODS_TARGET_SRCROOT)/src"',
      '"$(PODS_TARGET_SRCROOT)/third_party"',
      "\"#{skia_root}/include/skia\"",
      "\"#{skia_root}/include\""
    ].join(' ')
  }

  # Skia 静态库只在最终 App 的 Link 阶段需要，因此放在 user_target_xcconfig：
  # pod 自身是静态 framework，pod_target_xcconfig 的链接参数不会传给宿主。
  # `$(SRCROOT)` 即 iosApp 工程目录，指向仓库内的 vcpkg 产物（不入库）。
  skia_lib_flags = "-L\"$(SRCROOT)/../vcpkg/installed/ios/arm64-ios/lib\" " \
                   "-Wl,-force_load,\"$(SRCROOT)/../vcpkg/installed/ios/arm64-ios/lib/libskia.a\" " \
                   '-lfreetype -lharfbuzz -lharfbuzz-subset -licuuc -licui18n -licudata ' \
                   '-lpng -lz -ljpeg -lwebp -lwebpdemux -lwebpmux -lsharpyuv -lexpat ' \
                   '-lbrotlidec -lbrotlicommon -lbz2 -lc++'

  s.user_target_xcconfig = {
    'OTHER_LDFLAGS' => skia_lib_flags
  }
  s.libraries = 'c++'
end