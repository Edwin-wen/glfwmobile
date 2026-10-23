#import <UIKit/UIKit.h>
#import <OpenKuiklyIOSRender/KuiklyRenderViewExportProtocol.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * Kuikly 自定义 View：Skia 画布（iOS 实现）
 *
 * 对应 Kuikly 侧 `com.edwin.glfwmobile.canvas.CanvasGLView`（viewName = CanvasGLView），
 * 由于框架通过 `NSClassFromString(viewName)` 创建实例，类名必须与 viewName 完全一致。
 *
 * 绘制由 canvas_core（C++ / Skia）光栅化到 RGBA 缓冲，再包成 CGImage 交给 layer.contents；
 * 与 Android / 鸿蒙共用同一份实现与同一份 scene 指令。
 */
@interface CanvasGLView : UIView<KuiklyRenderViewExportProtocol>

@end

NS_ASSUME_NONNULL_END
