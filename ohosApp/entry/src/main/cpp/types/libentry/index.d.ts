export const initKuikly: () => number;

/**
 * 设置画布清屏颜色（原生 OpenGL 渲染）
 * @param xComponentId XComponent 组件的 id
 * @param color 颜色值，支持 #AARRGGBB / #RRGGBB
 */
export const setCanvasClearColor: (xComponentId: string, color: string) => void;