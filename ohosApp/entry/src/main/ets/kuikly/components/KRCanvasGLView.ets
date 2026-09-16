import { KRAny, KuiklyRenderBaseView, KuiklyRenderCallback } from '@kuikly-open/render';
import { ComponentContent } from '@kit.ArkUI';
import nativeEntry from 'libentry.so';

const kClearColor = 'clearColor';
const kOnReady = 'onReady';

/**
 * Kuikly 自定义 View：原生 OpenGL 画布（鸿蒙实现）
 *
 * 对应 Kuikly 侧 `com.edwin.glfwmobile.canvas.CanvasGLView`（viewName = CanvasGLView）。
 *
 * 实现说明：使用 XComponent(SURFACE) 承载渲染，真正的绘制在 native 侧
 * （`entry/src/main/cpp/canvas_gl_render.cpp`）通过 EGL + OpenGL ES 3.0 完成，
 * 静态画面仅在 Surface 创建/尺寸变化时清屏一次，不做常驻刷新。
 */
@Observed
export class KRCanvasGLView extends KuiklyRenderBaseView {
  static readonly VIEW_NAME = 'CanvasGLView';

  /** XComponent 的 id，保证同一页面多个画布实例互不干扰 */
  private static idSeed: number = 0;
  readonly xComponentId: string = `krcanvasgl_${KRCanvasGLView.idSeed++}`;

  cssClearColor: string | null = null;
  private cssOnReady: KuiklyRenderCallback | null = null;

  createArkUIView(): ComponentContent<KuiklyRenderBaseView> {
    const uiContext = this.getUIContext() as UIContext;
    return new ComponentContent<KuiklyRenderBaseView>(
      uiContext,
      wrapBuilder<[KuiklyRenderBaseView]>(createCanvasGLView),
      this
    );
  }

  setProp(propKey: string, propValue: KRAny | KuiklyRenderCallback): boolean {
    switch (propKey) {
      case kClearColor:
        this.cssClearColor = propValue as string;
        this.applyClearColorToNative();
        return true;
      case kOnReady:
        this.cssOnReady = propValue as KuiklyRenderCallback;
        return true;
      default:
        return super.setProp(propKey, propValue);
    }
  }

  call(method: string, params: KRAny, callback: KuiklyRenderCallback | null): KRAny {
    return null;
  }

  /**
   * 把清屏颜色透传给 native 渲染器。
   * native 侧在 Surface 尚未就绪时会先缓存颜色，待首帧绘制时生效。
   */
  applyClearColorToNative(): void {
    const color = this.cssClearColor;
    if (color == null || color.length == 0) {
      return;
    }
    try {
      nativeEntry.setCanvasClearColor(this.xComponentId, color);
    } catch (e) {
      console.error(`KuiklyRender KRCanvasGLView setCanvasClearColor failed: ${JSON.stringify(e)}`);
    }
  }

  /** XComponent 创建成功（Surface 已就绪）后回调 Kuikly */
  fireReady(): void {
    const callback = this.cssOnReady;
    if (callback != null) {
      callback({ 'viewName': KRCanvasGLView.VIEW_NAME });
    }
  }
}

@Component
export struct KRCanvasGLViewContent {
  @ObjectLink renderView: KRCanvasGLView;

  build() {
    XComponent({
      id: this.renderView.xComponentId,
      type: XComponentType.SURFACE,
      libraryname: 'entry'
    })
      .width('100%')
      .height('100%')
      .onLoad(() => {
        // Surface 已创建：同步一次颜色并通知 Kuikly 画布就绪
        this.renderView.applyClearColorToNative();
        this.renderView.fireReady();
      })
  }
}

@Builder
function createCanvasGLView(view: KuiklyRenderBaseView) {
  KRCanvasGLViewContent({ renderView: view as KRCanvasGLView })
}
