#import "CanvasGLView.h"
#import <OpenKuiklyIOSRender/KRComponentDefine.h>

#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#import <QuartzCore/QuartzCore.h>

/* 默认清屏颜色：不透明红色 */
#define KR_CANVAS_DEFAULT_R 1.0f
#define KR_CANVAS_DEFAULT_G 0.0f
#define KR_CANVAS_DEFAULT_B 0.0f
#define KR_CANVAS_DEFAULT_A 1.0f

@interface CanvasGLView () {
    EAGLContext *_eaglContext;      // GL 上下文（懒创建，保证在主线程初始化）
    GLuint _framebuffer;
    GLuint _colorRenderbuffer;
    BOOL _glReady;                  // framebuffer 是否可用
    BOOL _readyEventFired;          // onReady 是否已派发
    CGSize _lastRenderbufferSize;   // 上次 renderbuffer 的尺寸，用于尺寸变化时重建
    GLfloat _clearColor[4];         // RGBA
}

/* 对应 Kuikly 侧 attr { clearColor("...") } */
@property (nonatomic, copy) NSString *css_clearColor;

/* 对应 Kuikly 侧 event { onReady { } } */
@property (nonatomic, strong) KuiklyRenderCallback css_onReady;

@end

@implementation CanvasGLView

@synthesize hr_rootView;

+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _clearColor[0] = KR_CANVAS_DEFAULT_R;
        _clearColor[1] = KR_CANVAS_DEFAULT_G;
        _clearColor[2] = KR_CANVAS_DEFAULT_B;
        _clearColor[3] = KR_CANVAS_DEFAULT_A;
        // GL 未就绪时用纯色兜底，避免白屏
        self.backgroundColor = [self p_uiColorWithCurrentClearColor];
        CAEAGLLayer *eaglLayer = (CAEAGLLayer *)self.layer;
        eaglLayer.opaque = YES;
        eaglLayer.drawableProperties = @{
            kEAGLDrawablePropertyRetainedBacking: @(NO),
            kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8,
        };
    }
    return self;
}

- (void)dealloc {
    [self p_releaseGL];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    if (!_eaglContext) {
        [self p_setupGL];
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

#pragma mark - GL

/** 创建 GL 上下文与 framebuffer（仅在主线程、首次布局时调用） */
- (void)p_setupGL {
    if (CGSizeEqualToSize(self.bounds.size, CGSizeZero)) {
        return;
    }
    _eaglContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    if (!_eaglContext || ![EAGLContext setCurrentContext:_eaglContext]) {
        _eaglContext = nil;
        NSLog(@"[CanvasGLView] create EAGLContext failed, fallback to solid background color");
        return;
    }
    glGenFramebuffers(1, &_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glGenRenderbuffers(1, &_colorRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderbuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorRenderbuffer);
    _glReady = YES;
    _lastRenderbufferSize = CGSizeZero;
}

/** 清屏并呈现一帧 */
- (void)p_render {
    if (!_glReady) {
        return;
    }
    if (![_eaglContext isEqual:[EAGLContext currentContext]]) {
        if (![EAGLContext setCurrentContext:_eaglContext]) {
            return;
        }
    }
    CGSize size = self.bounds.size;
    if (CGSizeEqualToSize(size, CGSizeZero)) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderbuffer);
    // 尺寸变化时需要重新申请 drawable 存储，否则画面会被拉伸
    if (!CGSizeEqualToSize(size, _lastRenderbufferSize)) {
        [_eaglContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer *)self.layer];
        _lastRenderbufferSize = size;
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            NSLog(@"[CanvasGLView] framebuffer incomplete, fallback to solid background color");
            _glReady = NO;
            self.backgroundColor = [self p_uiColorWithCurrentClearColor];
            return;
        }
    }
    CGFloat scale = self.contentScaleFactor;
    glViewport(0, 0, (GLsizei)(size.width * scale), (GLsizei)(size.height * scale));
    glClearColor(_clearColor[0], _clearColor[1], _clearColor[2], _clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    [_eaglContext presentRenderbuffer:GL_RENDERBUFFER];
    [self p_fireReadyIfNeeded];
}

- (void)p_releaseGL {
    if (!_eaglContext) {
        return;
    }
    if ([EAGLContext setCurrentContext:_eaglContext]) {
        if (_framebuffer) {
            glDeleteFramebuffers(1, &_framebuffer);
            _framebuffer = 0;
        }
        if (_colorRenderbuffer) {
            glDeleteRenderbuffers(1, &_colorRenderbuffer);
            _colorRenderbuffer = 0;
        }
        [EAGLContext setCurrentContext:nil];
    }
    _eaglContext = nil;
    _glReady = NO;
}

- (void)p_fireReadyIfNeeded {
    if (_readyEventFired || !_glReady || !_css_onReady) {
        return;
    }
    _readyEventFired = YES;
    _css_onReady(@{@"viewName": @"CanvasGLView"});
}

#pragma mark - 颜色处理

/** 解析 #AARRGGBB / #RRGGBB，解析失败返回 NO 且不改写 color */
- (BOOL)p_parseColor:(NSString *)colorString into:(GLfloat *)color {
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
    color[0] = (GLfloat)r;
    color[1] = (GLfloat)g;
    color[2] = (GLfloat)b;
    color[3] = (GLfloat)a;
    return YES;
}

- (UIColor *)p_uiColorWithCurrentClearColor {
    return [UIColor colorWithRed:_clearColor[0] green:_clearColor[1] blue:_clearColor[2] alpha:_clearColor[3]];
}

@end
