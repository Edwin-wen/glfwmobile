#import "CanvasGLView.h"
#import <OpenKuiklyIOSRender/KRComponentDefine.h>

#import <QuartzCore/QuartzCore.h>
#import <canvas_core/canvas_core.h>

/* 默认清屏颜色：不透明红色（与 Android / Web 保持一致） */
static const uint32_t kKRCanvasDefaultArgb = 0xFFFF0000u;

@interface CanvasGLView () {
    skc_canvas *_core;      // C++ 绘制核心（与 Android / 鸿蒙同一份实现）
    uint8_t *_pixels;       // RGBA8888 光栅缓冲，由 CGImage 直接引用
    size_t _pixelWidth;
    size_t _pixelHeight;
    BOOL _readyEventFired;
}

/* 对应 Kuikly 侧 attr { clearColor("...") }，兼容旧用法 */
@property (nonatomic, copy) NSString *css_clearColor;

/* 对应 Kuikly 侧 attr { scene(...) }，统一绘制指令（JSON） */
@property (nonatomic, copy) NSString *css_scene;

/* 对应 Kuikly 侧 event { onReady { } } */
@property (nonatomic, strong) KuiklyRenderCallback css_onReady;

@end

@implementation CanvasGLView

@synthesize hr_rootView;

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _core = skc_create();
        self.backgroundColor = [self p_uiColorFromArgb:kKRCanvasDefaultArgb];
        // 光栅结果通过 layer.contents 呈现，尺寸变化时拉伸到目标大小
        self.layer.contentsGravity = kCAGravityResize;
        self.layer.magnificationFilter = kCAFilterNearest;
    }
    return self;
}

- (void)dealloc {
    if (_core != NULL) {
        skc_destroy(_core);
        _core = NULL;
    }
    free(_pixels);
    _pixels = NULL;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self p_render];
}

#pragma mark - KuiklyRenderViewExportProtocol

- (void)hrv_setPropWithKey:(NSString *)propKey propValue:(id)propValue {
    // clearColor / scene / onReady 统一分发到 setCss_xxx:
    KUIKLY_SET_CSS_COMMON_PROP;
}

- (void)hrv_callWithMethod:(NSString *)method params:(NSString *)params callback:(KuiklyRenderCallback)callback {
    KUIKLY_CALL_CSS_METHOD;
}

#pragma mark - css prop

- (void)setCss_clearColor:(NSString *)css_clearColor {
    _css_clearColor = [css_clearColor copy];
    uint32_t argb = kKRCanvasDefaultArgb;
    if (![self p_parseArgb:css_clearColor into:&argb]) {
        argb = kKRCanvasDefaultArgb;
    }
    [self p_ensureCore];
    if (_core != NULL) {
        skc_set_clear_color(_core, argb);
    }
    self.backgroundColor = [self p_uiColorFromArgb:argb];
    [self setNeedsLayout];
}

- (void)setCss_scene:(NSString *)css_scene {
    _css_scene = [css_scene copy];
    [self p_ensureCore];
    if (_core != NULL && css_scene.length > 0) {
        skc_set_scene(_core, css_scene.UTF8String);
    }
    [self setNeedsLayout];
}

- (void)setCss_onReady:(KuiklyRenderCallback)css_onReady {
    _css_onReady = css_onReady;
    [self p_fireReadyIfNeeded];
}

#pragma mark - Skia 渲染

- (void)p_ensureCore {
    if (_core == NULL) {
        _core = skc_create();
    }
}

- (void)p_render {
    CGSize size = self.bounds.size;
    if (size.width < 1.0 || size.height < 1.0) {
        return;
    }
    [self p_ensureCore];
    if (_core == NULL) {
        return;
    }

    // 物理像素对齐：scene 坐标是逻辑像素（pt），由 dpr 换算
    CGFloat scale = self.contentScaleFactor > 0 ? self.contentScaleFactor : 1.0;
    size_t width = (size_t)lround(size.width * scale);
    size_t height = (size_t)lround(size.height * scale);
    if (width == 0 || height == 0) {
        return;
    }

    uint8_t *previousPixels = NULL;
    if (width != _pixelWidth || height != _pixelHeight) {
        uint8_t *newPixels = (uint8_t *)calloc(width * height * 4, 1);
        if (newPixels == NULL) {
            return;
        }
        previousPixels = _pixels;
        _pixels = newPixels;
        _pixelWidth = width;
        _pixelHeight = height;
        skc_set_size(_core, (int32_t)width, (int32_t)height, (float)scale);
    }

    if (!skc_render_raster(_core, _pixels, (int32_t)(width * 4), (int32_t)width, (int32_t)height)) {
        if (previousPixels != NULL) {
            free(previousPixels);
        }
        return;
    }

    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, _pixels, width * height * 4, NULL);
    if (provider == NULL) {
        if (previousPixels != NULL) {
            free(previousPixels);
        }
        return;
    }
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = CGImageCreate(width, height, 8, 32, width * 4, colorSpace,
                                     kCGImageAlphaPremultipliedLast | kCGBitmapByteOrderDefault,
                                     provider, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(provider);

    if (image != NULL) {
        self.layer.contents = (__bridge id)image;
        CGImageRelease(image);
    }
    // 旧内容已被替换，此时释放上一块缓冲
    if (previousPixels != NULL) {
        free(previousPixels);
    }

    [self p_fireReadyIfNeeded];
}

- (void)p_fireReadyIfNeeded {
    if (_readyEventFired || _css_onReady == nil) {
        return;
    }
    _readyEventFired = YES;
    _css_onReady(@{@"viewName": @"CanvasGLView"});
}

#pragma mark - 颜色处理

/** 解析 #AARRGGBB / #RRGGBB / #RGB，失败返回 NO 且不改写 argb */
- (BOOL)p_parseArgb:(NSString *)colorString into:(uint32_t *)argb {
    if (![colorString isKindOfClass:[NSString class]] || colorString.length == 0) {
        return NO;
    }
    NSString *hex = [colorString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    hex = [hex hasPrefix:@"#"] ? [hex substringFromIndex:1] : hex;
    if (hex.length == 3) {
        unichar r = [hex characterAtIndex:0];
        unichar g = [hex characterAtIndex:1];
        unichar b = [hex characterAtIndex:2];
        hex = [NSString stringWithFormat:@"%C%C%C%C%C%C", r, r, g, g, b, b];
    }
    if (hex.length != 6 && hex.length != 8) {
        return NO;
    }
    unsigned long long value = 0;
    NSScanner *scanner = [NSScanner scannerWithString:hex];
    if (![scanner scanHexLongLong:&value] || !scanner.isAtEnd) {
        return NO;
    }
    *argb = (hex.length == 6) ? (uint32_t)(0xFF000000u | value) : (uint32_t)value;
    return YES;
}

- (UIColor *)p_uiColorFromArgb:(uint32_t)argb {
    return [UIColor colorWithRed:((argb >> 16) & 0xFF) / 255.0
                           green:((argb >> 8) & 0xFF) / 255.0
                            blue:(argb & 0xFF) / 255.0
                           alpha:((argb >> 24) & 0xFF) / 255.0];
}

@end