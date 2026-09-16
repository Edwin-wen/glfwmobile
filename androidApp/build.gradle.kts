plugins {
    id("com.android.application")
    kotlin("android")
}

android {
    namespace = "com.edwin.glfwmobile"
    compileSdk = 34
    defaultConfig {
        applicationId = "com.edwin.glfwmobile"
        minSdk = 23
        targetSdk = 30
        versionCode = 1
        versionName = "1.0"
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