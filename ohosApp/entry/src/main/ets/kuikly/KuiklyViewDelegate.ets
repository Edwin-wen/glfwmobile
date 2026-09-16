import {
  IKuiklyViewDelegate,
  KRRenderModuleExportCreator,
  KRRenderViewExportCreator
} from '@kuikly-open/render';
import { KRCanvasGLView } from './components/KRCanvasGLView';
import { KRMyView } from './components/KRMyView';
import { KRBridgeModule } from './modules/KRBridgeModule';
import { KRMyModule } from './modules/KRMyModule';

export class KuiklyViewDelegate extends IKuiklyViewDelegate {
  getCustomRenderViewCreatorRegisterMap(): Map<string, KRRenderViewExportCreator> {
    const map: Map<string, KRRenderViewExportCreator> = new Map();
    map.set(KRMyView.VIEW_NAME, () => new KRMyView());
    map.set(KRCanvasGLView.VIEW_NAME, () => new KRCanvasGLView());
    return map;
  }

  getCustomRenderModuleCreatorRegisterMap(): Map<string, KRRenderModuleExportCreator> {
    const map: Map<string, KRRenderModuleExportCreator> = new Map();
    map.set(KRBridgeModule.MODULE_NAME, () => new KRBridgeModule());
    map.set(KRMyModule.MODULE_NAME, () => new KRMyModule())
    return map;
  }
}