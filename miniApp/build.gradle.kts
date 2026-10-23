import java.nio.file.Paths

plugins {
    // Import KMM plugin
    kotlin("multiplatform")
}

kotlin {
    // Build JS output for miniApp
    js(IR) {
        // Build output supports browser
        browser {
            webpackTask {
                // Final output executable JS filename
                outputFileName = "miniApp.js"
                webpackConfigApplier {
                    val tempConfigFile = File(project.buildDir, "../webpack.config.d/config.js")
                    tempConfigFile.parentFile.mkdirs()
                    // use node target
                    tempConfigFile.writeText(
                        """
                        config.target = 'node';
                    """.trimIndent()
                    )
                    file(tempConfigFile.absolutePath)
                }
            }

            commonWebpackConfig {
                // Do not export global objects, only export necessary entry methods
                output?.library = null
                // use next line to enable source map
                // devtool = org.jetbrains.kotlin.gradle.targets.js.webpack.WebpackDevtool.INLINE_CHEAP_SOURCE_MAP
                devtool = null
            }
        }
        // Package render code and miniApp code together and execute directly
        binaries.executable()
    }
    sourceSets {
        val jsMain by getting {
            dependencies {
                // Import web render
                implementation("com.tencent.kuikly-open.core-render-web:base:${Version.getKuiklyVersion()}")
                implementation("com.tencent.kuikly-open.core-render-web:miniapp:${Version.getKuiklyVersion()}")
                // 跨端 scene 解析（与原生 canvasCore / H5 CanvasKit 共用同一份指令语义）
                implementation(project(":canvasWeb"))
            }
        }
    }
}

// Business project path name
val businessPathName = "shared"

/**
 * 压缩 dist 内的 JS 产物。
 * webpack 产物默认不压缩（lib + business 约 3.9M），超过微信主包 2M 上限，
 * 压缩后约 1.4M，低于 1.5M 建议值，无需拆分分包。
 */
val minifyMiniAppDist = tasks.register<Exec>("minifyMiniAppDist") {
    group = "kuikly"
    description = "Minify miniApp/dist JS bundles to keep WeChat main package under 1.5M"
    workingDir = projectDir
    commandLine("bash", file("tools/minify-dist.sh").absolutePath)
    // 压缩必须发生在产物拷贝之后，否则会被未压缩的产物覆盖
    mustRunAfter("syncRenderProductionToDist", "syncRenderDevelopmentToDist")
}

/**
 * Copy locally built unified JS result to miniApp's dist/business directory
 */
fun copyLocalJSBundle(buildSubPath: String) {
    // Output target path
    val destDir = Paths.get(
        project.buildDir.absolutePath, "../",
        "dist", "business"
    ).toFile()
    if (!destDir.exists()) {
        // Create directory if it doesn't exist
        destDir.mkdirs()
    } else {
        // Remove original files if directory exists
        destDir.deleteRecursively()
    }

    val sourceDir = Paths.get(
        project.rootDir.absolutePath,
        businessPathName,
        "build/dist/js", buildSubPath
    ).toFile()

    // Copy files
    project.copy {
        // Copy js files from business build result
        from(sourceDir) {
            include("nativevue2.js")
        }
        into(destDir)
    }
}

project.afterEvaluate {
    // kotlin 1.9 from 改为 $buildDir/dist/js/distributions
    tasks.register<Copy>("syncRenderProductionToDist") {
        from("$buildDir/kotlin-webpack/js/distributions")
        into("$projectDir/dist/lib")
        include("**/*.js", "**/*.d.ts")
    }

    // kotlin 1.9 from 改为 $buildDir/dist/js/developmentExecutable
    tasks.register<Copy>("syncRenderDevelopmentToDist") {
        from("$buildDir/kotlin-webpack/js/developmentExecutable")
        into("$projectDir/dist/lib")
        include("**/*.js", "**/*.d.ts")
    }

    tasks.register<Copy>("copyAssets") {
        val assetsDir = Paths.get(
            project.rootDir.absolutePath,
            businessPathName,
            "src/commonMain/assets"
        ).toFile()
        from(assetsDir)
        into("$projectDir/dist/assets")
        include("**/**")
    }

    tasks.named("jsBrowserProductionWebpack") {
        finalizedBy("syncRenderProductionToDist", "minifyMiniAppDist")
    }

    tasks.named("jsBrowserDevelopmentWebpack") {
        finalizedBy("syncRenderDevelopmentToDist", "minifyMiniAppDist")
    }

    tasks.register("jsMiniAppProductionWebpack") {
        group = "kuikly"
        // First execute jsBrowserProductionWebpack build task
        dependsOn("jsBrowserProductionWebpack")
        // Then copy corresponding nativevue2.zip from business build result and copy nativevue2.js
        // to miniApp's release directory
        copyLocalJSBundle("distributions")
        finalizedBy("minifyMiniAppDist")
    }

    tasks.register("jsMiniAppDevelopmentWebpack") {
        group = "kuikly"
        // First execute jsBrowserDevelopmentWebpack build task
        dependsOn("jsBrowserDevelopmentWebpack")
        // Then copy corresponding nativevue2.zip from business build result and copy nativevue2.js
        // to miniApp's release directory
        copyLocalJSBundle("developmentExecutable")
        finalizedBy("minifyMiniAppDist")
    }
}