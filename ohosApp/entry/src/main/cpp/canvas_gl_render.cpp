#include "canvas_gl_render.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <hilog/log.h>
#include <native_window/external_window.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#undef LOG_TAG
#define LOG_TAG "CanvasGLView"
#define LOG_INFO(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0xD001100, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD001100, LOG_TAG, __VA_ARGS__)

namespace {

/** 默认清屏颜色：不透明红色 */
constexpr float kDefaultClearColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};

/**
 * 单个 XComponent 的 GL 渲染器。
 * 注意：XComponent 回调与 napi 调用都发生在 ArkUI UI 线程，无需额外跨线程保护。
 */
class CanvasGLRenderer {
public:
    CanvasGLRenderer()
    {
        std::memcpy(clearColor_, kDefaultClearColor, sizeof(clearColor_));
    }

    ~CanvasGLRenderer()
    {
        DestroySurface();
        Terminate();
    }

    void SetClearColor(const float *color)
    {
        std::memcpy(clearColor_, color, sizeof(clearColor_));
        DrawFrame();
    }

    void OnSurfaceCreated(OH_NativeXComponent *component, void *window)
    {
        if (window == nullptr) {
            LOG_ERROR("%{public}s", "OnSurfaceCreated: window is null");
            return;
        }
        if (!EnsureEGL()) {
            return;
        }
        DestroySurface();
        eglWindow_ = eglCreateWindowSurface(display_, config_, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
        if (eglWindow_ == EGL_NO_SURFACE) {
            LOG_ERROR("eglCreateWindowSurface failed, error: %{public}d", eglGetError());
            return;
        }
        if (eglMakeCurrent(display_, eglWindow_, eglWindow_, context_) != EGL_TRUE) {
            LOG_ERROR("eglMakeCurrent failed, error: %{public}d", eglGetError());
            DestroySurface();
            return;
        }
        // Surface 刚创建时可能还未完成布局，先保证至少呈现一帧红色
        glViewport(0, 0, 1, 1);
        DrawFrame();
        OnSurfaceChanged(component, window);
    }

    void OnSurfaceChanged(OH_NativeXComponent *component, void *window)
    {
        if (eglWindow_ == EGL_NO_SURFACE || window == nullptr) {
            return;
        }
        uint64_t width = 0;
        uint64_t height = 0;
        if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) !=
            OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
            return;
        }
        if (width == 0 || height == 0) {
            return;
        }
        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        DrawFrame();
    }

    void OnSurfaceDestroyed()
    {
        DestroySurface();
    }

private:
    bool EnsureEGL()
    {
        if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
            return true;
        }
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY) {
            LOG_ERROR("%{public}s", "eglGetDisplay failed");
            return false;
        }
        EGLint major = 0;
        EGLint minor = 0;
        if (eglInitialize(display_, &major, &minor) != EGL_TRUE) {
            LOG_ERROR("eglInitialize failed, error: %{public}d", eglGetError());
            display_ = EGL_NO_DISPLAY;
            return false;
        }
        eglBindAPI(EGL_OPENGL_ES_API);

        const EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        EGLint numConfigs = 0;
        if (eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs) != EGL_TRUE || numConfigs < 1) {
            LOG_ERROR("eglChooseConfig failed, error: %{public}d", eglGetError());
            Terminate();
            return false;
        }
        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
        if (context_ == EGL_NO_CONTEXT) {
            LOG_ERROR("eglCreateContext failed, error: %{public}d", eglGetError());
            Terminate();
            return false;
        }
        LOG_INFO("EGL initialized, version %{public}d.%{public}d", major, minor);
        return true;
    }

    void DrawFrame()
    {
        if (eglWindow_ == EGL_NO_SURFACE) {
            return;
        }
        glClearColor(clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        if (eglSwapBuffers(display_, eglWindow_) != EGL_TRUE) {
            LOG_ERROR("eglSwapBuffers failed, error: %{public}d", eglGetError());
        }
    }

    void DestroySurface()
    {
        if (display_ == EGL_NO_DISPLAY) {
            eglWindow_ = EGL_NO_SURFACE;
            return;
        }
        if (display_ == eglGetCurrentDisplay() && eglWindow_ != EGL_NO_SURFACE &&
            eglWindow_ == eglGetCurrentSurface(EGL_DRAW)) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (eglWindow_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, eglWindow_);
            eglWindow_ = EGL_NO_SURFACE;
        }
    }

    void Terminate()
    {
        if (display_ == EGL_NO_DISPLAY) {
            return;
        }
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        config_ = nullptr;
    }

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface eglWindow_ = EGL_NO_SURFACE;
    float clearColor_[4] = {1.0f, 0.0f, 0.0f, 1.0f};
};

std::mutex g_rendererMutex;
std::unordered_map<std::string, std::unique_ptr<CanvasGLRenderer>> g_renderers;

std::string GetXComponentId(OH_NativeXComponent *component)
{
    if (component == nullptr) {
        return "";
    }
    char idBuffer[OH_XCOMPONENT_ID_LEN_MAX + 1] = {'\0'};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    if (OH_NativeXComponent_GetXComponentId(component, idBuffer, &idSize) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return "";
    }
    return std::string(idBuffer, static_cast<size_t>(idSize));
}

CanvasGLRenderer *GetOrCreateRenderer(const std::string &id)
{
    if (id.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_rendererMutex);
    auto &renderer = g_renderers[id];
    if (renderer == nullptr) {
        renderer = std::make_unique<CanvasGLRenderer>();
    }
    return renderer.get();
}

void RemoveRenderer(const std::string &id)
{
    std::lock_guard<std::mutex> lock(g_rendererMutex);
    g_renderers.erase(id);
}

bool ParseHexColor(const std::string &color, float *out)
{
    std::string hex = color;
    if (!hex.empty() && hex[0] == '#') {
        hex = hex.substr(1);
    }
    if (hex.size() != 6 && hex.size() != 8) {
        return false;
    }
    uint64_t value = 0;
    for (char c : hex) {
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<uint64_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= static_cast<uint64_t>(c - 'A' + 10);
        } else {
            return false;
        }
    }
    if (hex.size() == 8) {
        out[0] = static_cast<float>((value >> 16) & 0xFF) / 255.0f;
        out[1] = static_cast<float>((value >> 8) & 0xFF) / 255.0f;
        out[2] = static_cast<float>(value & 0xFF) / 255.0f;
        out[3] = static_cast<float>((value >> 24) & 0xFF) / 255.0f;
    } else {
        out[0] = static_cast<float>((value >> 16) & 0xFF) / 255.0f;
        out[1] = static_cast<float>((value >> 8) & 0xFF) / 255.0f;
        out[2] = static_cast<float>(value & 0xFF) / 255.0f;
        out[3] = 1.0f;
    }
    return true;
}

// ---------------- XComponent 生命周期回调 ----------------

void OnSurfaceCreatedCB(OH_NativeXComponent *component, void *window)
{
    CanvasGLRenderer *renderer = GetOrCreateRenderer(GetXComponentId(component));
    if (renderer != nullptr) {
        renderer->OnSurfaceCreated(component, window);
    }
}

void OnSurfaceChangedCB(OH_NativeXComponent *component, void *window)
{
    CanvasGLRenderer *renderer = GetOrCreateRenderer(GetXComponentId(component));
    if (renderer != nullptr) {
        renderer->OnSurfaceChanged(component, window);
    }
}

void OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window)
{
    (void)window;
    const std::string id = GetXComponentId(component);
    CanvasGLRenderer *renderer = GetOrCreateRenderer(id);
    if (renderer != nullptr) {
        renderer->OnSurfaceDestroyed();
    }
    RemoveRenderer(id);
}

void DispatchTouchEventCB(OH_NativeXComponent *component, void *window)
{
    (void)component;
    (void)window;
}

OH_NativeXComponent_Callback g_canvasCallback = {
    .OnSurfaceCreated = OnSurfaceCreatedCB,
    .OnSurfaceChanged = OnSurfaceChangedCB,
    .OnSurfaceDestroyed = OnSurfaceDestroyedCB,
    .DispatchTouchEvent = DispatchTouchEventCB,
};

} // namespace

int32_t RegisterCanvasGLXComponent(OH_NativeXComponent *component)
{
    if (component == nullptr) {
        LOG_ERROR("%{public}s", "RegisterCanvasGLXComponent: component is null");
        return -1;
    }
    const std::string id = GetXComponentId(component);
    if (id.empty()) {
        LOG_ERROR("%{public}s", "RegisterCanvasGLXComponent: get xcomponent id failed");
        return -1;
    }
    // 提前创建渲染器，避免首帧丢失
    GetOrCreateRenderer(id);
    if (OH_NativeXComponent_RegisterCallback(component, &g_canvasCallback) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        LOG_ERROR("%{public}s", "OH_NativeXComponent_RegisterCallback failed");
        return -1;
    }
    LOG_INFO("CanvasGLView registered, xcomponent id: %{public}s", id.c_str());
    return 0;
}

void SetCanvasGLClearColor(const std::string &xComponentId, const std::string &color)
{
    float rgba[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    if (!ParseHexColor(color, rgba)) {
        LOG_ERROR("invalid clearColor: %{public}s", color.c_str());
        return;
    }
    // 颜色可能在 Surface 创建前设置，此时先创建渲染器缓存颜色
    CanvasGLRenderer *renderer = GetOrCreateRenderer(xComponentId);
    if (renderer != nullptr) {
        renderer->SetClearColor(rgba);
    }
}

napi_value NapiSetCanvasClearColor(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
        LOG_ERROR("%{public}s", "setCanvasClearColor: invalid arguments");
        return nullptr;
    }
    char idBuffer[OH_XCOMPONENT_ID_LEN_MAX + 1] = {'\0'};
    size_t idLength = 0;
    if (napi_get_value_string_utf8(env, args[0], idBuffer, sizeof(idBuffer), &idLength) != napi_ok) {
        return nullptr;
    }
    char colorBuffer[32] = {'\0'};
    size_t colorLength = 0;
    if (napi_get_value_string_utf8(env, args[1], colorBuffer, sizeof(colorBuffer), &colorLength) != napi_ok) {
        return nullptr;
    }
    SetCanvasGLClearColor(std::string(idBuffer, idLength), std::string(colorBuffer, colorLength));
    return nullptr;
}

void ReleaseAllCanvasGLRenderers()
{
    std::unordered_map<std::string, std::unique_ptr<CanvasGLRenderer>> renderers;
    {
        std::lock_guard<std::mutex> lock(g_rendererMutex);
        renderers.swap(g_renderers);
    }
    renderers.clear();
}
