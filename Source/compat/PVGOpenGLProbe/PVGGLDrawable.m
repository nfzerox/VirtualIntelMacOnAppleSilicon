#import <Foundation/Foundation.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <stdio.h>

// Big Sur MTLIOAccelGLDrawable is a thin wrapper over IOAccelerator functions
// also exported by Catalina's loose IOAccelerator (installed on Mojave).
// SetDrawable validates these exact 48-byte input / 40-byte output sizes.
typedef struct {
    uint64_t mode;
    uint32_t surface,depth,face,level,volatileState,fixedSource,scale,width,height,padding;
} PVGGLDrawableInput;
typedef struct {
    uint64_t mode;
    uint32_t width,height,surfaceWidth,surfaceHeight;
    uint8_t privateResult[16];
} PVGGLDrawableOutput;
_Static_assert(sizeof(PVGGLDrawableInput)==48,"drawable request ABI");
_Static_assert(sizeof(PVGGLDrawableOutput)==40,"drawable reply ABI");
static CFTypeRef (*createDrawable)(void *);
static int (*clearDrawable)(CFTypeRef);
static int (*setDrawable)(CFTypeRef,const void *,size_t,void *,size_t);
static int (*setRect)(CFTypeRef,int,int,int,int);
static int (*setInterval)(CFTypeRef,int,int);
static int (*lookupSurface)(CFTypeRef,unsigned,CFTypeRef *);
static BOOL loadAPI(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        void *image=dlopen("/System/Library/PrivateFrameworks/IOAccelerator.framework/Versions/A/IOAccelerator",RTLD_NOW|RTLD_LOCAL);
        if(!image)return;
        createDrawable=dlsym(image,"IOAccelGLDrawableCreate");
        clearDrawable=dlsym(image,"IOAccelGLDrawableClearDrawable");
        setDrawable=dlsym(image,"IOAccelGLDrawableSetDrawable");
        setRect=dlsym(image,"IOAccelGLDrawableSetSwapRect");
        setInterval=dlsym(image,"IOAccelGLDrawableSetSwapInterval");
        lookupSurface=dlsym(image,"IOAccelGLDrawableLookupIOSurfaceFromIndex");
    });
    return createDrawable&&clearDrawable&&setDrawable&&setRect&&setInterval&&lookupSurface;
}
@interface PVGGLDrawable : NSObject {
    CFTypeRef _drawable;
    PVGGLDrawableOutput _result;
}
- (id)initWithDevice:(id)device;
@end
@implementation PVGGLDrawable
- (id)initWithDevice:(id)device {
    if(!(self=[super init]))return nil;
    if(!loadAPI()){fprintf(stderr,"PVG GL drawable API unavailable\n");[self release];return nil;}
    void *ref=((void *(*)(id,SEL))objc_msgSend)(device,sel_registerName("deviceRef"));
    _drawable=createDrawable(ref);
    if(!_drawable){fprintf(stderr,"PVG GL drawable creation failed\n");[self release];return nil;}
    return self;
}
- (void)dealloc {if(_drawable)CFRelease(_drawable);[super dealloc];}
- (NSUInteger)windowMode {return _result.mode;}
- (unsigned)width {return _result.width;}
- (unsigned)height {return _result.height;}
- (unsigned)surfaceWidth {return _result.surfaceWidth;}
- (unsigned)surfaceHeight {return _result.surfaceHeight;}
- (int)clearDrawable {return clearDrawable(_drawable);}
- (int)setDrawableSurface:(unsigned)surface mode:(NSUInteger)mode colorDepthMode:(unsigned)depth
                     face:(unsigned)face level:(unsigned)level volatile:(unsigned)volatileState
              fixedSource:(unsigned)fixedSource scaleOptions:(unsigned)scale
              scaledWidth:(unsigned)width scaledHeight:(unsigned)height {
    PVGGLDrawableInput input={mode,surface,depth,face,level,volatileState,fixedSource,scale,width,height,0};
    memset(&_result,0,sizeof(_result));
    int status=setDrawable(_drawable,&input,sizeof(input),&_result,sizeof(_result));
    if(getenv("PVG_GL_TRACE_DRAWABLE"))fprintf(stderr,"PVG drawable surface=%u mode=%llu depth=%u size=%ux%u status=0x%x reply=%llu,%ux%u surface=%ux%u\n",surface,(unsigned long long)mode,depth,width,height,status,(unsigned long long)_result.mode,_result.width,_result.height,_result.surfaceWidth,_result.surfaceHeight);
    return status;
}
- (int)setSwapRectX:(int)x y:(int)y w:(int)width h:(int)height {return setRect(_drawable,x,y,width,height);}
- (int)setSwapInterval:(int)interval limit:(int)limit {return setInterval(_drawable,interval,limit);}
- (CFTypeRef)lookupIOSurfaceAtIndex:(unsigned)index {
    CFTypeRef surface=NULL;int status=lookupSurface(_drawable,index,&surface);
    if(getenv("PVG_GL_TRACE_DRAWABLE"))fprintf(stderr,"PVG drawable lookup=%u status=0x%x surface=%p\n",index,status,surface);
    return status==0?surface:NULL;
}
@end
id PVGNewGLDrawable(id device,SEL selector) {(void)selector;return [[PVGGLDrawable alloc] initWithDevice:device];}
