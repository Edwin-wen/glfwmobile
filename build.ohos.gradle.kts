plugins {
    //trick: for the same plugin versions in all sub-modules
    //注意：这里是鸿蒙(ohos)链的独立入口，配合 settings.ohos.gradle.kts 使用。
    //Kotlin 必须用 KBA/ohos 专用版本，AGP 也固定在 7.4.2，与主链(gradle/libs.versions.toml)
    //故意不一致，不要在这里"统一版本"，否则鸿蒙产物会构建失败。
    id("com.android.application").version("7.4.2").apply(false)
    id("com.android.library").version("7.4.2").apply(false)
    kotlin("android").version("2.0.21-KBA-010").apply(false)
    kotlin("multiplatform").version("2.0.21-KBA-010").apply(false)
    id("com.google.devtools.ksp").version("2.0.21-1.0.27").apply(false)

}