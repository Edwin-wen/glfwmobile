#import <UIKit/UIKit.h>
#import <OpenKuiklyIOSRender/KuiklyRenderViewExportProtocol.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * Kuikly 自定义 View：原生 Metal 画布（iOS 实现）
 *
 * 对应 Kuikly 侧 `com.edwin.glfwmobile.canvas.CanvasGLView`（viewName = CanvasGLView），
 * 由于框架通过 `NSClassFromString(viewName)` 创建实例，类名必须与 viewName 完全一致。
 *
 * 说明：基于 CAMetalLayer + MTLDevice 使用独立 Metal 渲染层，
 * 不适合做圆角/半透明叠加，适用于整页铺满的静态画面。
 * （OpenGL ES 在 iOS 已废弃，本实现用 Metal 完成等价的清屏 + present 逻辑）
 */
@interface CanvasGLView : UIView<KuiklyRenderViewExportProtocol>

@end

NS_ASSUME_NONNULL_END
