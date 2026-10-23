#include "painter.h"

#include <skia/core/SkCanvas.h>
#include <skia/core/SkColor.h>
#include <skia/core/SkPaint.h>
#include <skia/core/SkRect.h>

namespace skc {

void Paint(SkCanvas *canvas, const Scene &scene, float dpr) {
    if (canvas == nullptr) {
        return;
    }
    // scene 使用逻辑像素，这里统一缩放到物理像素
    canvas->save();
    canvas->scale(dpr, dpr);

    SkPaint paint;
    paint.setAntiAlias(true);

    for (const Op &op : scene.ops) {
        switch (op.type) {
            case OpType::kClear:
                // clear 不受当前矩阵影响，始终覆盖整块 surface
                canvas->clear(op.color);
                break;
            case OpType::kFillRect:
                paint.setStyle(SkPaint::kFill_Style);
                paint.setColor(op.color);
                canvas->drawRect(SkRect::MakeXYWH(op.rect.x, op.rect.y, op.rect.w, op.rect.h),
                                 paint);
                break;
            case OpType::kStrokeLine:
                paint.setStyle(SkPaint::kStroke_Style);
                paint.setColor(op.color);
                paint.setStrokeWidth(op.line.width);
                paint.setStrokeCap(SkPaint::kRound_Cap);
                canvas->drawLine(op.line.x1, op.line.y1, op.line.x2, op.line.y2, paint);
                break;
        }
    }

    canvas->restore();
}

} // namespace skc