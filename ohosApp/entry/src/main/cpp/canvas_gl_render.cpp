#include "canvas_gl_render.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <hilog/log.h>
#include <native_window/external_window.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "canvas_core.h"

#undef LOG_TAG
#define LOG_TAG "CanvasGLView"
#define LOG_INFO(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0xD001100, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0xD001100, LOG_TAG, __VA_ARGS__)

namespace {

/** 默认清屏颜色：不透明红色 */
constexpr float kDefaultClearColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};

constexpr const char *kVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char *kFragmentShader = R"(#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTexture;
out vec4 fragColor;
void main() {
    fragColor = texture(uTexture, vUv);
}
)";

uint32_t ArgbFromRgba(const float *rgba) {
    const auto channel = [](float value) -> uint32_t {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
    };
    return (channel(rgba[3]) << 24) | (channel(rgba[0]) << 16) | (channel(rgba[1]) << 8) |
           channel(rgba[2]);
}

/**
 * 单个 XComponent 的渲染器。
 *
 * 绘制统一由 canvas_core（C++ / Skia）光栅化到 RGBA 缓冲，再由 GL 上传纹理并铺满
 * Surface。与 Android 的 Bitmap 路径、iOS 的 CGImage 路径是同一份 Skia 实现。
 * 画面为静态内容，仅在 Surface 创建/尺寸变化/场景变化时重绘。
 */
class CanvasGLRenderer {
public:
    CanvasGLRenderer() {
        std::memcpy(clearColor_, kDefaultClearColor, sizeof(clearColor_));
        core_ = skc_create();
    }

    ~CanvasGLRenderer() {
        DestroyGLResources();
        DestroySurface();
        Terminate();
        if (core_ != nullptr) {
            skc_destroy(core_);
            core_ = nullptr;
        }
    }

    void SetClearColor(const float *color) {
        std::memcpy(clearColor_, color, sizeof(clearColor_));
        if (core_ != nullptr) {
            skc_set_clear_color(core_, ArgbFromRgba(color));
        }
        DrawFrame();
    }

    void SetScene(const std::string &sceneJson) {
        if (core_ != nullptr) {
            skc_set_scene(core_, sceneJson.c_str());
            const char *error = skc_last_error(core_);
            if (error != nullptr && error[0] != '\0') {
                LOG_ERROR("setScene failed: %{public}s", error);
            }
        }
        DrawFrame();
    }

    void SetPixelRatio(float ratio) {
        pixelRatio_ = ratio > 0.0f ? ratio : 1.0f;
        if (core_ != nullptr && width_ > 0 && height_ > 0) {
            skc_set_size(core_, width_, height_, pixelRatio_);
        }
        DrawFrame();
    }

    /** mode: 0=raster 1=gpu 2=auto */
    void SetBackend(int32_t mode) {
        backend_ = mode;
        if (mode == 0) {
            gpuAttached_ = false;
            if (core_ != nullptr) {
                skc_set_backend(core_, SKC_BACKEND_RASTER);
            }
        } else if (core_ != nullptr) {
            skc_set_backend(core_, static_cast<skc_backend>(mode));
        }
        DrawFrame();
    }

    void OnSurfaceCreated(OH_NativeXComponent *component, void *window) {
        if (window == nullptr) {
            LOG_ERROR("%{public}s", "OnSurfaceCreated: window is null");
            return;
        }
        if (!EnsureEGL()) {
            return;
        }
        DestroySurface();
        eglWindow_ = eglCreateWindowSurface(display_, config_,
                                            reinterpret_cast<EGLNativeWindowType>(window), nullptr);
        if (eglWindow_ == EGL_NO_SURFACE) {
            LOG_ERROR("eglCreateWindowSurface failed, error: %{public}d", eglGetError());
            return;
        }
        if (eglMakeCurrent(display_, eglWindow_, eglWindow_, context_) != EGL_TRUE) {
            LOG_ERROR("eglMakeCurrent failed, error: %{public}d", eglGetError());
            DestroySurface();
            return;
        }
        EnsureGLResources();
        // Surface 刚创建时可能还未完成布局，先保证至少呈现一帧
        glViewport(0, 0, 1, 1);
        DrawFrame();
        OnSurfaceChanged(component, window);
    }

    void OnSurfaceChanged(OH_NativeXComponent *component, void *window) {
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
        const int32_t newWidth = static_cast<int32_t>(width);
        const int32_t newHeight = static_cast<int32_t>(height);
        if (newWidth != width_ || newHeight != height_) {
            width_ = newWidth;
            height_ = newHeight;
            pixels_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
            if (core_ != nullptr) {
                skc_set_size(core_, width_, height_, pixelRatio_);
            }
        }
        glViewport(0, 0, width_, height_);
        DrawFrame();
    }

    void OnSurfaceDestroyed() {
        DestroySurface();
    }

private:
    bool EnsureEGL() {
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
        if (eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs) != EGL_TRUE ||
            numConfigs < 1) {
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

    bool EnsureGLResources() {
        if (program_ != 0) {
            return true;
        }
        const GLuint vertex = CompileShader(GL_VERTEX_SHADER, kVertexShader);
        const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
        if (vertex == 0 || fragment == 0) {
            return false;
        }
        program_ = glCreateProgram();
        glAttachShader(program_, vertex);
        glAttachShader(program_, fragment);
        glLinkProgram(program_);
        glDeleteShader(vertex);
        glDeleteShader(fragment);

        GLint linked = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            LOG_ERROR("%{public}s", "link program failed");
            glDeleteProgram(program_);
            program_ = 0;
            return false;
        }

        // 全屏四边形（triangle strip），纹理坐标按像素行翻转（Skia 第 0 行在顶部）
        const GLfloat vertices[] = {
            -1.0f, -1.0f, 0.0f, 1.0f,
            1.0f, -1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, 0.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 0.0f
        };
        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                              reinterpret_cast<const void *>(2 * sizeof(GLfloat)));

        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindVertexArray(0);
        return true;
    }

    GLuint CompileShader(GLenum type, const char *source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_TRUE) {
            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
            glGetShaderInfoLog(shader, length, nullptr, &log[0]);
            LOG_ERROR("compile shader failed: %{public}s", log.c_str());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    void DrawFrame() {
        if (eglWindow_ == EGL_NO_SURFACE || width_ <= 0 || height_ <= 0) {
            return;
        }

        // 优先 GPU：Skia Ganesh 直接画到 EGLSurface 的默认帧缓冲（FBO 0）
        if (backend_ != 0 && core_ != nullptr) {
            if (!gpuAttached_) {
                skc_set_backend(core_, static_cast<skc_backend>(backend_));
                gpuAttached_ = skc_attach_gl(core_);
                if (!gpuAttached_) {
                    LOG_ERROR("gpu attach failed: %{public}s", skc_last_error(core_));
                }
            }
            if (gpuAttached_ && skc_render_gl(core_, 0, width_, height_)) {
                if (eglSwapBuffers(display_, eglWindow_) != EGL_TRUE) {
                    LOG_ERROR("eglSwapBuffers failed, error: %{public}d", eglGetError());
                }
                return;
            }
        }

        const bool skiaReady =
            core_ != nullptr && !pixels_.empty() &&
            skc_render_raster(core_, pixels_.data(), width_ * 4, width_, height_);

        glViewport(0, 0, width_, height_);
        glClearColor(clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]);
        glClear(GL_COLOR_BUFFER_BIT);

        if (skiaReady && program_ != 0 && texture_ != 0) {
            glUseProgram(program_);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, pixels_.data());
            glUniform1i(glGetUniformLocation(program_, "uTexture"), 0);
            glBindVertexArray(vao_);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
            glUseProgram(0);
        }

        if (eglSwapBuffers(display_, eglWindow_) != EGL_TRUE) {
            LOG_ERROR("eglSwapBuffers failed, error: %{public}d", eglGetError());
        }
    }

    void DestroyGLResources() {
        if (texture_ != 0) {
            glDeleteTextures(1, &texture_);
            texture_ = 0;
        }
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
            vbo_ = 0;
        }
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
            vao_ = 0;
        }
        if (program_ != 0) {
            glDeleteProgram(program_);
            program_ = 0;
        }
    }

    void DestroySurface() {
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

    void Terminate() {
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

    skc_canvas *core_ = nullptr;
    std::vector<uint8_t> pixels_;
    int32_t width_ = 0;
    int32_t height_ = 0;
    float pixelRatio_ = 1.0f;
    int32_t backend_ = 2; // 0=raster 1=gpu 2=auto
    bool gpuAttached_ = false;

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface eglWindow_ = EGL_NO_SURFACE;

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint texture_ = 0;

    float clearColor_[4] = {1.0f, 0.0f, 0.0f, 1.0f};
};

std::mutex g_rendererMutex;
std::unordered_map<std::string, std::unique_ptr<CanvasGLRenderer>> g_renderers;

std::string GetXComponentId(OH_NativeXComponent *component) {
    if (component == nullptr) {
        return "";
    }
    char idBuffer[OH_XCOMPONENT_ID_LEN_MAX + 1] = {'\0'};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    if (OH_NativeXComponent_GetXComponentId(component, idBuffer, &idSize) !=
        OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return "";
    }
    return std::string(idBuffer, static_cast<size_t>(idSize));
}

CanvasGLRenderer *GetOrCreateRenderer(const std::string &id) {
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

void RemoveRenderer(const std::string &id) {
    std::lock_guard<std::mutex> lock(g_rendererMutex);
    g_renderers.erase(id);
}

bool ParseHexColor(const std::string &color, float *out) {
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

void OnSurfaceCreatedCB(OH_NativeXComponent *component, void *window) {
    CanvasGLRenderer *renderer = GetOrCreateRenderer(GetXComponentId(component));
    if (renderer != nullptr) {
        renderer->OnSurfaceCreated(component, window);
    }
}

void OnSurfaceChangedCB(OH_NativeXComponent *component, void *window) {
    CanvasGLRenderer *renderer = GetOrCreateRenderer(GetXComponentId(component));
    if (renderer != nullptr) {
        renderer->OnSurfaceChanged(component, window);
    }
}

void OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window) {
    (void)window;
    const std::string id = GetXComponentId(component);
    CanvasGLRenderer *renderer = GetOrCreateRenderer(id);
    if (renderer != nullptr) {
        renderer->OnSurfaceDestroyed();
    }
    RemoveRenderer(id);
}

void DispatchTouchEventCB(OH_NativeXComponent *component, void *window) {
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

int32_t RegisterCanvasGLXComponent(OH_NativeXComponent *component) {
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
    if (OH_NativeXComponent_RegisterCallback(component, &g_canvasCallback) !=
        OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        LOG_ERROR("%{public}s", "OH_NativeXComponent_RegisterCallback failed");
        return -1;
    }
    LOG_INFO("CanvasGLView registered, xcomponent id: %{public}s", id.c_str());
    return 0;
}

void SetCanvasGLClearColor(const std::string &xComponentId, const std::string &color) {
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

void SetCanvasGLScene(const std::string &xComponentId, const std::string &sceneJson) {
    CanvasGLRenderer *renderer = GetOrCreateRenderer(xComponentId);
    if (renderer != nullptr) {
        renderer->SetScene(sceneJson);
    }
}

void SetCanvasGLBackend(const std::string &xComponentId, int32_t mode) {
    CanvasGLRenderer *renderer = GetOrCreateRenderer(xComponentId);
    if (renderer != nullptr) {
        renderer->SetBackend(mode);
    }
}

void SetCanvasGLPixelRatio(const std::string &xComponentId, float ratio) {
    CanvasGLRenderer *renderer = GetOrCreateRenderer(xComponentId);
    if (renderer != nullptr) {
        renderer->SetPixelRatio(ratio);
    }
}

napi_value NapiSetCanvasClearColor(napi_env env, napi_callback_info info) {
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
    if (napi_get_value_string_utf8(env, args[1], colorBuffer, sizeof(colorBuffer), &colorLength) !=
        napi_ok) {
        return nullptr;
    }
    SetCanvasGLClearColor(std::string(idBuffer, idLength), std::string(colorBuffer, colorLength));
    return nullptr;
}

napi_value NapiSetCanvasScene(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
        LOG_ERROR("%{public}s", "setCanvasScene: invalid arguments");
        return nullptr;
    }
    char idBuffer[OH_XCOMPONENT_ID_LEN_MAX + 1] = {'\0'};
    size_t idLength = 0;
    if (napi_get_value_string_utf8(env, args[0], idBuffer, sizeof(idBuffer), &idLength) != napi_ok) {
        return nullptr;
    }
    size_t jsonLength = 0;
    if (napi_get_value_string_utf8(env, args[1], nullptr, 0, &jsonLength) != napi_ok) {
        return nullptr;
    }
    std::string sceneJson(jsonLength + 1, '\0');
    if (napi_get_value_string_utf8(env, args[1], &sceneJson[0], jsonLength + 1, &jsonLength) !=
        napi_ok) {
        return nullptr;
    }
    sceneJson.resize(jsonLength);
    SetCanvasGLScene(std::string(idBuffer, idLength), sceneJson);
    return nullptr;
}

napi_value NapiSetCanvasPixelRatio(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
        LOG_ERROR("%{public}s", "setCanvasPixelRatio: invalid arguments");
        return nullptr;
    }
    char idBuffer[OH_XCOMPONENT_ID_LEN_MAX + 1] = {'\0'};
    size_t idLength = 0;
    if (napi_get_value_string_utf8(env, args[0], idBuffer, sizeof(idBuffer), &idLength) != napi_ok) {
        return nullptr;
    }
    double ratio = 1.0;
    if (napi_get_value_double(env, args[1], &ratio) != napi_ok) {
        return nullptr;
    }
    SetCanvasGLPixelRatio(std::string(idBuffer, idLength), static_cast<float>(ratio));
    return nullptr;
}

napi_value NapiSetCanvasBackend(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
        LOG_ERROR("%{public}s", "setCanvasBackend: invalid arguments");
        return nullptr;
    }
    char idBuffer[OH_XCOMPONENT_ID_LEN_MAX + 1] = {'\0'};
    size_t idLength = 0;
    if (napi_get_value_string_utf8(env, args[0], idBuffer, sizeof(idBuffer), &idLength) != napi_ok) {
        return nullptr;
    }
    int32_t mode = 2;
    if (napi_get_value_int32(env, args[1], &mode) != napi_ok) {
        return nullptr;
    }
    SetCanvasGLBackend(std::string(idBuffer, idLength), mode);
    return nullptr;
}

void ReleaseAllCanvasGLRenderers() {
    std::unordered_map<std::string, std::unique_ptr<CanvasGLRenderer>> renderers;
    {
        std::lock_guard<std::mutex> lock(g_rendererMutex);
        renderers.swap(g_renderers);
    }
    renderers.clear();
}