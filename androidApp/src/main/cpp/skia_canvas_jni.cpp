#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>

#include "canvas_core.h"

/*
 * Android 侧桥接：只做 JNI 参数转换与 Bitmap 内存交接。
 * 所有绘制语义都在 canvasCore（与 iOS / 鸿蒙共用同一份实现）。
 */
namespace {

constexpr const char *kTag = "CanvasGLView";

skc_canvas *FromHandle(jlong handle) {
    return reinterpret_cast<skc_canvas *>(handle);
}

void LogError(const char *message, const char *detail) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s: %s", message, detail == nullptr ? "" : detail);
}

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_edwin_glfwmobile_expandview_CanvasGLView_nativeCreate(JNIEnv *env, jobject thiz) {
    (void)env;
    (void)thiz;
    return reinterpret_cast<jlong>(skc_create());
}

JNIEXPORT void JNICALL
Java_com_edwin_glfwmobile_expandview_CanvasGLView_nativeDestroy(JNIEnv *env, jobject thiz,
                                                                jlong handle) {
    (void)env;
    (void)thiz;
    skc_destroy(FromHandle(handle));
}

JNIEXPORT void JNICALL
Java_com_edwin_glfwmobile_expandview_CanvasGLView_nativeSetSize(JNIEnv *env, jobject thiz,
                                                                jlong handle, jint width,
                                                                jint height, jfloat dpr) {
    (void)env;
    (void)thiz;
    skc_set_size(FromHandle(handle), width, height, dpr);
}

JNIEXPORT void JNICALL
Java_com_edwin_glfwmobile_expandview_CanvasGLView_nativeSetClearColor(JNIEnv *env, jobject thiz,
                                                                      jlong handle, jint argb) {
    (void)env;
    (void)thiz;
    skc_set_clear_color(FromHandle(handle), static_cast<uint32_t>(argb));
}

JNIEXPORT void JNICALL
Java_com_edwin_glfwmobile_expandview_CanvasGLView_nativeSetScene(JNIEnv *env, jobject thiz,
                                                                 jlong handle, jstring scene) {
    (void)thiz;
    skc_canvas *canvas = FromHandle(handle);
    if (canvas == nullptr || scene == nullptr) {
        return;
    }
    const char *json = env->GetStringUTFChars(scene, nullptr);
    if (json == nullptr) {
        return;
    }
    skc_set_scene(canvas, json);
    env->ReleaseStringUTFChars(scene, json);
    const char *error = skc_last_error(canvas);
    if (error != nullptr && error[0] != '\0') {
        LogError("setScene failed", error);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_edwin_glfwmobile_expandview_CanvasGLView_nativeRenderBitmap(JNIEnv *env, jobject thiz,
                                                                     jlong handle, jobject bitmap) {
    (void)env;
    (void)thiz;
    skc_canvas *canvas = FromHandle(handle);
    if (canvas == nullptr || bitmap == nullptr) {
        return JNI_FALSE;
    }

    AndroidBitmapInfo info{};
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        LogError("AndroidBitmap_getInfo failed", "");
        return JNI_FALSE;
    }
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LogError("bitmap must be ARGB_8888", "");
        return JNI_FALSE;
    }

    void *pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS ||
        pixels == nullptr) {
        LogError("AndroidBitmap_lockPixels failed", "");
        return JNI_FALSE;
    }

    const bool ok = skc_render_raster(canvas, pixels, static_cast<int32_t>(info.stride),
                                      static_cast<int32_t>(info.width),
                                      static_cast<int32_t>(info.height));
    AndroidBitmap_unlockPixels(env, bitmap);

    if (!ok) {
        LogError("render failed", skc_last_error(canvas));
    }
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_edwin_glfwmobile_expandview_CanvasGLView_nativeLastError(JNIEnv *env, jobject thiz,
                                                                  jlong handle) {
    (void)thiz;
    return env->NewStringUTF(skc_last_error(FromHandle(handle)));
}

} // extern "C"