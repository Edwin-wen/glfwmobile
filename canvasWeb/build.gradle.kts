plugins {
    kotlin("multiplatform")
}

/**
 * Web 端绘制层（H5 / 小程序共用）：
 * - `WebScene`：scene ops 解析，对齐原生端 `cppCore/canvasCore/src/scene.cpp`
 * - `CanvasKitRenderer`：H5 的 CanvasKit 播放器
 *
 * 不含任何 Kuikly / 业务依赖，宿主模块按需引用。
 */
kotlin {
    js(IR) {
        browser()
    }
    sourceSets {
        val jsMain by getting {
            dependencies {
                implementation(kotlin("stdlib-js"))
            }
        }
    }
}