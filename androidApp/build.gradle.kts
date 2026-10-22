plugins {
    id("com.android.application")
    kotlin("android")
}

/**
 * Skia 静态库所在目录（含 include/ 与 lib/libskia.a）。
 * 优先取 gradle 属性 `skia.android.dir` / 环境变量 `SKIA_ANDROID_DIR`，
 * 缺省回落到本机 vcpkg 的 arm64-android 安装目录。
 */
val skiaAndroidDir: String = providers.gradleProperty("skia.android.dir").orNull
    ?: System.getenv("SKIA_ANDROID_DIR")
    ?: "${System.getProperty("user.home")}/ENV/vcpkg/installed/arm64-android"

android {
    namespace = "com.edwin.glfwmobile"
    compileSdk = 34
    ndkVersion = "27.1.12297006"
    defaultConfig {
        applicationId = "com.edwin.glfwmobile"
        minSdk = 23
        targetSdk = 30
        versionCode = 1
        versionName = "1.0"
        // 当前只提供 arm64-v8a 的 Skia 产物；其余 ABI 需补构建产物后再放开
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                // Skia 由 vcpkg 以 c++_shared 构建，这里必须保持一致
                arguments += listOf("-DSKIA_DIR=$skiaAndroidDir", "-DANDROID_STL=c++_shared")
                cppFlags += "-std=c++17"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    kotlinOptions {
        jvmTarget = "1.8"
    }
    //AGP 8 起 buildConfig 默认关闭，而代码里用到了 BuildConfig.DEBUG，这里显式打开
    buildFeatures {
        buildConfig = true
    }
}

dependencies {
    implementation(project(":shared"))

    implementation(libs.androidx.recyclerview)
    implementation(libs.androidx.appcompat)

    implementation(libs.picasso)

    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.dynamicanimation)
    implementation(libs.glide)
    annotationProcessor(libs.glide.compiler)
}