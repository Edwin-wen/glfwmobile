export const initKuikly: () => number;

/**
 * 设置画布清屏颜色（Skia 渲染）
 * @param xComponentId XComponent 组件的 id
 * @param color 颜色值，支持 #AARRGGBB / #RRGGBB
 */
export const setCanvasClearColor: (xComponentId: string, color: string) => void;

/**
 * 设置画布绘制指令（统一 scene JSON，与 Android / iOS / Web 共用同一份语义）
 * @param xComponentId XComponent 组件的 id
 * @param sceneJson 场景 JSON
 */
export const setCanvasScene: (xComponentId: string, sceneJson: string) => void;

/**
 * 设置像素密度：XComponent 尺寸是物理像素，scene 坐标是 vp
 * @param xComponentId XComponent 组件的 id
 * @param ratio 像素密度（display.densityPixels）
 */
export const setCanvasPixelRatio: (xComponentId: string, ratio: number) => void;