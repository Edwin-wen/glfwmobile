plugins {
    //trick: for the same plugin versions in all sub-modules
    //版本统一维护在 gradle/libs.versions.toml，改版本只改那一个文件
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.multiplatform) apply false
    alias(libs.plugins.ksp) apply false
}

buildscript {
    dependencies {
        classpath(BuildPlugin.kuikly)
    }
}