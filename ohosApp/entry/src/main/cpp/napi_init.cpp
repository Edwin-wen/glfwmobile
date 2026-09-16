#include "napi/native_api.h"
#include "libshared_api.h"
#include "canvas_gl_render.h"
#include <hilog/log.h>

static napi_value InitKuikly(napi_env env, napi_callback_info info) {
    auto api = libshared_symbols();
    int handler = api->kotlin.root.initKuikly();
    napi_value result;
    napi_create_int32(env, handler, &result);
    return result;
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"initKuikly", nullptr, InitKuikly, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setCanvasClearColor", nullptr, NapiSetCanvasClearColor, nullptr, nullptr, nullptr, napi_default, nullptr}
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    // 当本 so 被 XComponent({ libraryname: 'entry' }) 加载时，exports 会挂载 XComponent 对象，
    // 需要在此注册 Surface 生命周期回调，用于创建 EGL + OpenGL ES 渲染环境。
    napi_value exportInstance = nullptr;
    if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) == napi_ok) {
        OH_NativeXComponent *nativeXComponent = nullptr;
        if (napi_unwrap(env, exportInstance, reinterpret_cast<void **>(&nativeXComponent)) == napi_ok) {
            RegisterCanvasGLXComponent(nativeXComponent);
        }
    }
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void*)0),
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void)
{
    napi_module_register(&demoModule);
}