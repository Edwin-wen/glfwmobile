package com.edwin.glfwmobile.expandview

/**
 * Java 侧对 `canvas_core` 的唯一入口。
 *
 * 命名与 `androidApp/src/main/cpp/skia_canvas_jni.cpp` 严格对应（JNI 静态注册），
 * 改类名/包名必须同步改 C++ 侧符号。
 */
internal object CanvasNative {

    external fun nativeCreate(): Long

    external fun nativeDestroy(handle: Long)

    external fun nativeSetSize(handle: Long, width: Int, height: Int, dpr: Float)

    external fun nativeSetClearColor(handle: Long, argb: Int)

    external fun nativeSetScene(handle: Long, sceneJson: String)

    external fun nativeRenderBitmap(handle: Long, bitmap: android.graphics.Bitmap): Boolean

    /** 0=raster 1=gpu 2=auto，见 canvas_core.h 的 skc_backend */
    external fun nativeSetBackend(handle: Long, backend: Int)

    external fun nativeBackendInUse(handle: Long): Int

    external fun nativeAttachGl(handle: Long): Boolean

    external fun nativeRenderGl(handle: Long, fboId: Int, width: Int, height: Int): Boolean

    external fun nativeDetachGpu(handle: Long)

    external fun nativeLastError(handle: Long): String

    init {
        System.loadLibrary("skia_canvas")
    }

    const val BACKEND_RASTER = 0
    const val BACKEND_GPU = 1
    const val BACKEND_AUTO = 2
}