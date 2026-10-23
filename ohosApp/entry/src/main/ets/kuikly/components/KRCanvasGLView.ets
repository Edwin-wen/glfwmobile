import { KRAny, KuiklyRenderBaseView, KuiklyRenderCallback } from '@kuikly-open/render';
import { ComponentContent, display } from '@kit.ArkUI';
import nativeEntry from 'libentry.so';

const kClearColor = 'clearColor';
const kScene = 'scene';
const kBackend = 'backend';
const kOnReady = 'onReady';

/**
 * Kuikly 自定义 View：Skia 画布（鸿蒙实现）
 *
 * 对应 Kuikly 侧 `com.edwin.glfwmobile.canvas.CanvasGLView`（viewName = CanvasGLView）。
 *
 * 实现说明：XComponent(SURFACE) 提供渲染表面；native 侧
 * （`entry/src/main/cpp/canvas_gl_render.cpp`）用 canvas_core（C++ / Skia）光栅化到
 * RGBA 缓冲，再通过 EGL + GLES3 上传纹理铺满 Surface。
 * 绘制指令五端共用同一份 scene JSON。
 */
@Observed
export class KRCanvasGLView extends KuiklyRenderBaseView {
  static readonly VIEW_NAME = 'CanvasGLView';

  /** XComponent 的 id，保证同一页面多个画布实例互不干扰 */
  private static idSeed: number = 0;
  readonly xComponentId: string = `krcanvasgl_${KRCanvasGLView.idSeed++}`;

  cssClearColor: string | null = null;
  cssScene: string | null = null;
  cssBackend: string | null = null;
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
      case kScene:
        this.cssScene = propValue as string;
        this.applySceneToNative();
        return true;
      case kBackend:
        this.cssBackend = propValue as string;
        this.applyBackendToNative();
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
   * native 侧在 Surface 尚未就绪时会先缓存，待首帧绘制时生效。
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

  /** 把 scene（统一绘制指令）透传给 canvas_core */
  applySceneToNative(): void {
    const scene = this.cssScene;
    if (scene == null || scene.length == 0) {
      return;
    }
    try {
      nativeEntry.setCanvasScene(this.xComponentId, scene);
    } catch (e) {
      console.error(`KuiklyRender KRCanvasGLView setCanvasScene failed: ${JSON.stringify(e)}`);
    }
  }

  /** scene 坐标是 vp，XComponent 尺寸是物理像素，这里同步像素密度 */
  applyPixelRatioToNative(): void {
    try {
      const ratio = display.getDefaultDisplaySync().densityPixels;
      nativeEntry.setCanvasPixelRatio(this.xComponentId, ratio);
    } catch (e) {
      console.error(`KuiklyRender KRCanvasGLView setCanvasPixelRatio failed: ${JSON.stringify(e)}`);
    }
  }

  /** 绘制后端：0=raster 1=gpu 2=auto，与 canvas_core.h 的 skc_backend 对齐 */
  applyBackendToNative(): void {
    const backend = this.cssBackend;
    let mode = 2;
    if (backend == 'raster') {
      mode = 0;
    } else if (backend == 'gpu') {
      mode = 1;
    }
    try {
      nativeEntry.setCanvasBackend(this.xComponentId, mode);
    } catch (e) {
      console.error(`KuiklyRender KRCanvasGLView setCanvasBackend failed: ${JSON.stringify(e)}`);
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
        // Surface 已创建：同步一次像素密度 / 后端 / 颜色 / 场景，并通知 Kuikly 画布就绪
        this.renderView.applyPixelRatioToNative();
        this.renderView.applyBackendToNative();
        this.renderView.applyClearColorToNative();
        this.renderView.applySceneToNative();
        this.renderView.fireReady();
      })
  }
}

@Builder
function createCanvasGLView(view: KuiklyRenderBaseView) {
  KRCanvasGLViewContent({ renderView: view as KRCanvasGLView })
}
