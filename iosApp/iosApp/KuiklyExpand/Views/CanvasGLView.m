#import "CanvasGLView.h"
#import <OpenKuiklyIOSRender/KRComponentDefine.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/QuartzCore.h>

/* 默认清屏颜色：不透明红色 */
#define KR_CANVAS_DEFAULT_R 1.0f
#define KR_CANVAS_DEFAULT_G 0.0f
#define KR_CANVAS_DEFAULT_B 0.0f
#define KR_CANVAS_DEFAULT_A 1.0f

@interface CanvasGLView () {
    id<MTLDevice> _device;          // Metal 设备（懒创建，保证在主线程初始化）
    id<MTLCommandQueue> _commandQueue;
    CAMetalLayer *_metalLayer;      // backing layer
    BOOL _metalReady;               // Metal 是否可用
    BOOL _readyEventFired;          // onReady 是否已派发
    CGSize _lastDrawableSize;       // 上次 drawable 尺寸，用于尺寸变化时更新
    double _clearColor[4];          // RGBA 0~1（MTLClearColor 使用 double 分量）
}

/* 对应 Kuikly 侧 attr { clearColor("...") } */
@property (nonatomic, copy) NSString *css_clearColor;

/* 对应 Kuikly 侧 event { onReady { } } */
@property (nonatomic, strong) KuiklyRenderCallback css_onReady;

@end

@implementation CanvasGLView

@synthesize hr_rootView;

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _clearColor[0] = KR_CANVAS_DEFAULT_R;
        _clearColor[1] = KR_CANVAS_DEFAULT_G;
        _clearColor[2] = KR_CANVAS_DEFAULT_B;
        _clearColor[3] = KR_CANVAS_DEFAULT_A;
        // Metal 未就绪时用纯色兜底，避免白屏
        self.backgroundColor = [self p_uiColorWithCurrentClearColor];
        _metalLayer = (CAMetalLayer *)self.layer;
        _metalLayer.opaque = YES;
        _metalLayer.framebufferOnly = YES;
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        // 先给一个初始 scale，layoutSubviews 里会再按屏幕真实 scale 校准
        _metalLayer.contentsScale = [UIScreen mainScreen].scale;
    }
    return self;
}

- (void)dealloc {
    _device = nil;
    _commandQueue = nil;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    // window 就绪后 contentsScale 才可靠，这里按屏幕 scale 校准图层
    _metalLayer.contentsScale = [UIScreen mainScreen].scale;
    if (!_device) {
        [self p_setupMetal];
    }
    [self p_render];
}

#pragma mark - KuiklyRenderViewExportProtocol

- (void)hrv_setPropWithKey:(NSString *)propKey propValue:(id)propValue {
    // clearColor / onReady 等属性由 css_setPropWithKey 统一分发到 setCss_xxx:
    KUIKLY_SET_CSS_COMMON_PROP;
}

- (void)hrv_callWithMethod:(NSString *)method params:(NSString *)params callback:(KuiklyRenderCallback)callback {
    KUIKLY_CALL_CSS_METHOD;
}

#pragma mark - css prop

- (void)setCss_clearColor:(NSString *)css_clearColor {
    _css_clearColor = [css_clearColor copy];
    if (![self p_parseColor:css_clearColor into:_clearColor]) {
        _clearColor[0] = KR_CANVAS_DEFAULT_R;
        _clearColor[1] = KR_CANVAS_DEFAULT_G;
        _clearColor[2] = KR_CANVAS_DEFAULT_B;
        _clearColor[3] = KR_CANVAS_DEFAULT_A;
    }
    self.backgroundColor = [self p_uiColorWithCurrentClearColor];
    [self setNeedsLayout];
}

- (void)setCss_onReady:(KuiklyRenderCallback)css_onReady {
    _css_onReady = css_onReady;
    [self p_fireReadyIfNeeded];
}

#pragma mark - Metal

/** 创建 Metal 设备与命令队列（仅在主线程、首次布局时调用） */
- (void)p_setupMetal {
    if (CGSizeEqualToSize(self.bounds.size, CGSizeZero)) {
        return;
    }
    _device = MTLCreateSystemDefaultDevice();
    if (!_device) {
        NSLog(@"[CanvasGLView] create MTLDevice failed, fallback to solid background color");
        return;
    }
    _metalLayer.device = _device;
    _commandQueue = [_device newCommandQueue];
    _metalReady = YES;
    _lastDrawableSize = CGSizeZero;
}

/** 清屏并呈现一帧（静态画面：只在颜色/尺寸变化时触发） */
- (void)p_render {
    if (!_metalReady || !_device || !_metalLayer) {
        return;
    }
    CGSize size = self.bounds.size;
    if (CGSizeEqualToSize(size, CGSizeZero)) {
        return;
    }
    // drawable 尺寸必须与物理像素一致，尺寸变化时需要更新，否则画面会被拉伸
    CGFloat scale = MAX(self.contentScaleFactor, _metalLayer.contentsScale);
    CGSize drawableSize = CGSizeMake(size.width * scale, size.height * scale);
    if (!CGSizeEqualToSize(drawableSize, _lastDrawableSize)) {
        _metalLayer.drawableSize = drawableSize;
        _lastDrawableSize = drawableSize;
    }
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        if (!drawable) {
            // 拿不到 drawable（窗口不可见等）时保持纯色兜底，不白屏
            return;
        }
        MTLRenderPassDescriptor *descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        MTLRenderPassColorAttachmentDescriptor *colorAttachment = descriptor.colorAttachments[0];
        colorAttachment.texture = drawable.texture;
        colorAttachment.loadAction = MTLLoadActionClear;
        colorAttachment.storeAction = MTLStoreActionStore;
        colorAttachment.clearColor = MTLClearColorMake(_clearColor[0], _clearColor[1], _clearColor[2], _clearColor[3]);

        id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
        [encoder endEncoding];
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
    }
    [self p_fireReadyIfNeeded];
}

- (void)p_fireReadyIfNeeded {
    if (_readyEventFired || !_metalReady || !_css_onReady) {
        return;
    }
    _readyEventFired = YES;
    _css_onReady(@{@"viewName": @"CanvasGLView"});
}

#pragma mark - 颜色处理

/** 解析 #AARRGGBB / #RRGGBB，解析失败返回 NO 且不改写 color */
- (BOOL)p_parseColor:(NSString *)colorString into:(double *)color {
    if (![colorString isKindOfClass:[NSString class]] || colorString.length == 0) {
        return NO;
    }
    NSString *hex = [colorString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    hex = [hex hasPrefix:@"#"] ? [hex substringFromIndex:1] : hex;
    unsigned long long value = 0;
    NSScanner *scanner = [NSScanner scannerWithString:hex];
    if (![scanner scanHexLongLong:&value] || !scanner.isAtEnd) {
        return NO;
    }
    CGFloat r = 0, g = 0, b = 0, a = 1;
    if (hex.length == 8) {
        a = ((value >> 24) & 0xFF) / 255.0;
        r = ((value >> 16) & 0xFF) / 255.0;
        g = ((value >> 8) & 0xFF) / 255.0;
        b = (value & 0xFF) / 255.0;
    } else if (hex.length == 6) {
        r = ((value >> 16) & 0xFF) / 255.0;
        g = ((value >> 8) & 0xFF) / 255.0;
        b = (value & 0xFF) / 255.0;
    } else {
        return NO;
    }
    color[0] = (double)r;
    color[1] = (double)g;
    color[2] = (double)b;
    color[3] = (double)a;
    return YES;
}

- (UIColor *)p_uiColorWithCurrentClearColor {
    return [UIColor colorWithRed:(CGFloat)_clearColor[0]
                           green:(CGFloat)_clearColor[1]
                            blue:(CGFloat)_clearColor[2]
                           alpha:(CGFloat)_clearColor[3]];
}

@end
