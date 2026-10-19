#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include "PVGGLSwizzle.h"
#include "PVGBigSurTextureUsage.h"
extern id PVGNewGLDrawable(id device,SEL selector);

uint32_t MTLTextureSwizzleViewSwizzle(uint32_t base,uint32_t view) {
    return PVGGLComposeSwizzle(base,view);
}
// The four uint8 channels have the same x86_64 calling convention as a u32;
// both native helpers are a single register move (Catalina 502bb/502c3).
uint32_t MTLTextureSwizzleKeyToChannels(uint32_t key) {return key;}
uint32_t MTLTextureSwizzleChannelsToKey(uint32_t channels) {return channels;}

// Catalina Metal exports this NSString pointer; Mojave does not. Verified from
// Catalina's __data -> __cfstring -> __cstring, not inferred from its name.
NSString * const _MTLGLLabel = @"com.apple.metal.gl";

// Big Sur invalidation unregisters this notifier's context from IOSurface's
// user client. Catalina exports that operation, but not the wrapper. Keep the
// CF object's ownership untouched; its normal finalizer still destroys the
// notification port. A repeated removal has no matching registration to remove.
void IOSurfaceNotifierInvalidate(void *notifier) {
    static void (*nativeInvalidate)(void *);
    static int (*removeNotify)(void *);
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        void *image = dlopen("/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface", RTLD_NOW | RTLD_LOCAL);
        nativeInvalidate = dlsym(image, "IOSurfaceNotifierInvalidate");
        removeNotify = dlsym(image, "IOSurfaceClientRemoveSurfaceNotify");
    });
    if (!notifier) return;
    if (nativeInvalidate) nativeInvalidate(notifier);
    else if (removeNotify) (void)removeNotify(notifier);
    else { fprintf(stderr, "PVG GL: IOSurface notification removal unavailable\n"); abort(); }
}

static NSString *intelProfile(id self, SEL selector) {
    (void)self; (void)selector;
    return @"Intel (Apple PVG test profile)";
}

static IMP originalSampleCountSupport;
static BOOL supportsConservativeSampleCount(id self, SEL selector, NSUInteger count) {
    // Report a real capability limit, never rewrite an 8x allocation to 4x.
    // Research opt-in until GL's advertised pixel formats/limits are verified.
    return count <= 4 && ((BOOL (*)(id,SEL,NSUInteger))originalSampleCountSupport)
        (self,selector,count);
}

static BOOL noUnalignedVertexFetch(id self, SEL selector) {
    (void)self; (void)selector;
    // No such capability is advertised by the donor PVG device. Have GLD
    // choose its aligned path rather than claiming unsupported fetches.
    return NO;
}

static BOOL conservativeDepthStencilAccess(id self, SEL selector) {
    (void)self; (void)selector;
    // Exact Big Sur _MTLDepthStencilState default: all four access queries
    // return YES. PVG inherits these; actual tests/writes stay in GPU state.
    return YES;
}

static BOOL basicPrimitiveType(id self, SEL selector, NSUInteger type) {
    (void)self; (void)selector;
    // The five public Metal primitive types are supported by PVG. Do not
    // claim GLD's private fan/loop/adjacency types; GLD must lower those.
    return type <= MTLPrimitiveTypeTriangleStrip;
}

static void defaultDepthStencilWriteMode(id self, SEL selector, BOOL disabled) {
    (void)self; (void)selector; (void)disabled;
    // Keep Catalina's general pipeline instead of specializing it for no
    // depth/stencil writes. GLD's separate GLRDepthStencilKey::copyDescriptor
    // sets depthWriteEnabled and both stencil write masks independently.
    // Those dynamic states still control writes. Depth/stencil rendering
    // tests are required before this research adapter becomes production.
}

static void researchPointCoordFlip(id self, SEL selector, BOOL enabled) {
    (void)self; (void)selector; (void)enabled;
    // The 11.x serializer does not expose this render-pass hint.
    // GLD also supplies Catalina's existing pointCoordLowerLeft pipeline
    // setting. opengl-point-coordinates verifies both origins and repeated
    // switching on Catalina PVG against native Intel GL. Mojave coverage is
    // still required; this is not a claim about every point-sprite mode.
}

static void defaultSampleCoverageInvert(id self,SEL selector,BOOL invert) {
    (void)self;(void)selector;
    if(invert){fprintf(stderr,"PVG GL: Mojave inverted sample coverage not implemented\n");abort();}
}
static void defaultSampleCoverage(id self,SEL selector,float coverage) {
    (void)self;(void)selector;
    if(coverage!=1.0f){fprintf(stderr,"PVG GL: Mojave partial sample coverage not implemented\n");abort();}
}
static char logicOperationKey;
static void disabledLogicOperation(id self,SEL selector,BOOL enabled) {
    (void)self;(void)selector;
    if(enabled){fprintf(stderr,"PVG GL: Mojave logic operations not implemented\n");abort();}
}
static void requireDisabledGLFeature(id self,SEL selector,BOOL enabled) {
    (void)self;
    if(enabled){fprintf(stderr,"PVG GL: unsupported Mojave pipeline feature %s\n",sel_getName(selector));abort();}
}
static void storeDisabledLogicOperation(id self,SEL selector,NSUInteger value) {
    (void)selector;
    // The absent enable setter rejects YES, so this state cannot affect pixels.
    objc_setAssociatedObject(self,&logicOperationKey,@(value),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static IMP originalPipeline;
static IMP originalSimplePipeline;
static IMP originalQueue;
static IMP originalTextureViewInit;
static IMP originalTextureCreate;
static IMP originalResourceDiagnostic;
static id traceDepthResource(id self,SEL selector,id device,id remote,NSUInteger options,void *args,unsigned size) {
    BOOL trace=NO;
    if(args&&size>=0x9c&&[NSStringFromClass([self class]) isEqualToString:@"AppleParavirtTexture"]) {
        uint16_t format=0;memcpy(&format,(uint8_t *)args+0x7e,2);
        trace=format==260;
    }
    id result=((id(*)(id,SEL,id,id,NSUInteger,void *,unsigned))originalResourceDiagnostic)
        (self,selector,device,remote,options,args,size);
    if(trace) {
        fprintf(stderr,"PVG GL depth resource result=%p options=%lx size=%x bytes=",result,(unsigned long)options,size);
        for(unsigned i=0;i<size;i++)fprintf(stderr,"%02x",((unsigned char *)args)[i]);
        fputc('\n',stderr);
    }
    return result;
}
static id traceTextureCreate(id self,SEL selector,MTLTextureDescriptor *descriptor) {
    fprintf(stderr,"PVG GL texture: format=%lu type=%lu size=%lux%lux%lu samples=%lu storage=%lu usage=0x%lx optimized=%d\n",
        (unsigned long)descriptor.pixelFormat,(unsigned long)descriptor.textureType,
        (unsigned long)descriptor.width,(unsigned long)descriptor.height,(unsigned long)descriptor.depth,
        (unsigned long)descriptor.sampleCount,(unsigned long)descriptor.storageMode,(unsigned long)descriptor.usage,descriptor.allowGPUOptimizedContents);
    return ((id(*)(id,SEL,id))originalTextureCreate)(self,selector,descriptor);
}
static char textureViewSwizzleKey;
static IMP originalPVGView,originalSerializeView;
static _Thread_local id swizzleParent;
static _Thread_local uint32_t pendingSwizzle;
static id pvgSwizzledView(id self,SEL selector,id parent,NSUInteger format,
                         NSUInteger type,NSRange levels,NSRange slices,uint32_t swizzle) {
    id savedParent=swizzleParent;uint32_t saved=pendingSwizzle;
    swizzleParent=parent;pendingSwizzle=swizzle;
    @try {
        return ((id(*)(id,SEL,id,NSUInteger,NSUInteger,NSRange,NSRange,uint32_t))originalPVGView)
            (self,selector,parent,format,type,levels,slices,swizzle);
    } @finally {swizzleParent=savedParent;pendingSwizzle=saved;}
}
static uint32_t serializeSwizzledView(id self,SEL selector,NSUInteger format,NSUInteger type,
                                    NSRange levels,NSRange slices,id parent,id allocator) {
    if(parent==swizzleParent&&pendingSwizzle!=0x05040302) {
        SEL full=sel_registerName("newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:baseTexture:allocator:");
        return ((uint32_t(*)(id,SEL,NSUInteger,NSUInteger,NSRange,NSRange,uint32_t,id,id))objc_msgSend)
            (self,full,format,type,levels,slices,pendingSwizzle,parent,allocator);
    }
    return ((uint32_t(*)(id,SEL,NSUInteger,NSUInteger,NSRange,NSRange,id,id))originalSerializeView)
        (self,selector,format,type,levels,slices,parent,allocator);
}
static void installSwizzleSerialization(id device) {
    if(originalPVGView)return;
    id serializer=((id(*)(id,SEL))objc_msgSend)(device,sel_registerName("serializer"));
    SEL old=sel_registerName("newTextureViewWithPixelFormat:textureType:levels:slices:baseTexture:allocator:");
    SEL full=sel_registerName("newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:baseTexture:allocator:");
    SEL view=sel_registerName("initWithTexture:pixelFormat:textureType:levels:slices:swizzle:");
    Class texture=objc_getClass("AppleParavirtTexture");
    Method sm=class_getInstanceMethod([serializer class],old),vm=class_getInstanceMethod(texture,view);
    if(!sm||!vm||![serializer respondsToSelector:full])abort();
    originalSerializeView=method_getImplementation(sm);originalPVGView=method_getImplementation(vm);
    class_replaceMethod([serializer class],old,(IMP)serializeSwizzledView,method_getTypeEncoding(sm));
    class_replaceMethod(texture,view,(IMP)pvgSwizzledView,method_getTypeEncoding(vm));
}
static unsigned textureViewSwizzle(id self,SEL selector) {
    (void)selector;
    NSNumber *value=objc_getAssociatedObject(self,&textureViewSwizzleKey);
    return value?value.unsignedIntValue:0x05040302;
}
static id initIdentityTextureView(id self,SEL selector,id parent,NSUInteger format,
                                 NSUInteger type,NSRange levels,NSRange slices,uint32_t swizzle) {
    (void)selector;
    for(unsigned i=0;i<4;i++)if(((swizzle>>(8*i))&255)>5) {
        fprintf(stderr,"PVG GL: invalid texture-view swizzle 0x%x\n",swizzle);abort();
    }
    if(swizzle!=0x05040302&&!originalPVGView) {
        fprintf(stderr,"PVG GL: texture-view swizzle transport unavailable 0x%x\n",swizzle);abort();
    }
    // Catalina's superclass stores the supplied key (not a composition with
    // the parent's key). Our scoped serializer adapter carries the key to the
    // host as well; merely retaining this property would produce wrong pixels.
    SEL old=sel_registerName("initWithTexture:pixelFormat:textureType:levels:slices:");
    id result=((id(*)(id,SEL,id,NSUInteger,NSUInteger,NSRange,NSRange))originalTextureViewInit)
        (self,old,parent,format,type,levels,slices);
    if(result)objc_setAssociatedObject(result,&textureViewSwizzleKey,@(swizzle),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return result;
}
static char openGLQueueKey;
static NSUInteger zeroSamplerValue(id self,SEL selector) {(void)self;(void)selector;return 0;}
static void requireInvalidSamplerFormat(id self,SEL selector,NSUInteger value) {
    (void)self;
    if(value!=MTLPixelFormatInvalid){fprintf(stderr,"PVG GL: unsupported %s=%lu\n",sel_getName(selector),(unsigned long)value);abort();}
}
static void requireZeroBorderComponent(id self,SEL selector,uint32_t value) {
    (void)self;
    if(value){fprintf(stderr,"PVG GL: unsupported %s=0x%x\n",sel_getName(selector),value);abort();}
}
static void requireSupportedRasterSamples(MTLRenderPassDescriptor *self,SEL selector,NSUInteger count) {
    // SDK: this default is used only for attachment-less rendering. With
    // attachments, Mojave already derives the sample count from the textures.
    BOOL attached=NO;
    for(unsigned i=0;i<8;i++)if(self.colorAttachments[i].texture)attached=YES;
    attached|=self.depthAttachment.texture!=nil||self.stencilAttachment.texture!=nil;
    if(!attached&&count!=1){fprintf(stderr,"PVG GL: unsupported attachment-less %s=%lu\n",sel_getName(selector),(unsigned long)count);abort();}
}
static char openGLModeKey;
static char ditherKey;
static void setDither(id self,SEL selector,BOOL value) {
    (void)selector;objc_setAssociatedObject(self,&ditherKey,@(value),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
static BOOL isDither(id self,SEL selector) {
    (void)selector;return [objc_getAssociatedObject(self,&ditherKey) boolValue];
}
static void setOpenGLMode(id self,SEL selector,BOOL value) {
    (void)selector;objc_setAssociatedObject(self,&openGLModeKey,@(value),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
static BOOL openGLMode(id self,SEL selector) {
    (void)selector;return [objc_getAssociatedObject(self,&openGLModeKey) boolValue];
}
static char writeSwizzleKey;
typedef struct {Class cls; IMP copy;} DescriptorCopy;
static DescriptorCopy descriptorCopies[3];
static id copyDescriptorState(id self,SEL selector,NSZone *zone) {
    IMP original=NULL;
    for(Class cls=object_getClass(self);cls&&!original;cls=class_getSuperclass(cls))
        for(unsigned i=0;i<3;i++)if(descriptorCopies[i].cls==cls)original=descriptorCopies[i].copy;
    if(!original)abort();
    id result=((id(*)(id,SEL,NSZone *))original)(self,selector,zone);
    if(result&&result!=self) {
        const void *keys[]={&openGLModeKey,&ditherKey,&writeSwizzleKey,&openGLQueueKey,&logicOperationKey};
        for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++) {
            id value=objc_getAssociatedObject(self,keys[i]);
            if(value)objc_setAssociatedObject(result,keys[i],value,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
    }
    return result;
}
static void preserveDescriptorCopies(void) {
    const char *names[]={"MTLRenderPassDescriptorInternal","MTLRenderPipelineDescriptorInternal",
        "MTLTextureDescriptorInternal"};
    SEL selector=@selector(copyWithZone:);
    for(unsigned i=0;i<3;i++) {
        Class cls=objc_getClass(names[i]);Method method=class_getInstanceMethod(cls,selector);
        if(!method)continue;
        descriptorCopies[i]=(DescriptorCopy){cls,method_getImplementation(method)};
        class_replaceMethod(cls,selector,(IMP)copyDescriptorState,method_getTypeEncoding(method));
    }
}
static void setWriteSwizzle(id self,SEL selector,BOOL value) {
    (void)selector;objc_setAssociatedObject(self,&writeSwizzleKey,@(value),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
static BOOL writeSwizzle(id self,SEL selector) {
    (void)selector;return [objc_getAssociatedObject(self,&writeSwizzleKey) boolValue];
}
static unsigned identitySwizzle(id self,SEL selector) {
    (void)self;(void)selector;return 0x05040302; // Catalina descriptor default, RGBA
}
static void requireIdentitySwizzle(id self,SEL selector,unsigned value) {
    (void)self;(void)selector;
    if(value!=0x05040302){fprintf(stderr,"PVG GL: unimplemented Mojave swizzle 0x%x\n",value);abort();}
}
static void setOpenGLQueue(id self,SEL selector,BOOL value) {
    (void)selector;objc_setAssociatedObject(self,&openGLQueueKey,@(value),OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
static BOOL isOpenGLQueue(id self,SEL selector) {
    (void)selector;return [objc_getAssociatedObject(self,&openGLQueueKey) boolValue];
}
static id createGLQueue(id self,SEL selector,id descriptor) {
    BOOL flag=((BOOL(*)(id,SEL))objc_msgSend)(descriptor,sel_registerName("isOpenGLQueue"));
    id queue=((id(*)(id,SEL,id))originalQueue)(self,selector,descriptor);
    if(queue)setOpenGLQueue(queue,NULL,flag);
    return queue;
}
static IMP originalGLIR;
static char vertexMetadataKey, fragmentMetadataKey, functionMetadataKey;

// Mojave has no native GLIR library-data class. Keep unspecialized IR in an
// ordinary object and lower a variant into a real, native Metal library. GLD
// owns the variant cache; this runs at shader creation, never at draw/submit.
@interface PVGGLIRFunction : NSObject {
    NSData *_ir;
    id<MTLDevice> _device;
    NSUInteger _type;
}
- (id)initWithIR:(const void *)ir device:(id)device type:(NSUInteger)type;
- (NSUInteger)functionType;
- (id)newFunctionWithPluginData:(NSData *)inputs bitcodeType:(unsigned char)kind;
@end

@implementation PVGGLIRFunction
- (id)initWithIR:(const void *)ir device:(id)device type:(NSUInteger)type {
    if (!(self=[super init])) return nil;
    uint32_t length=0;
    if(ir) memcpy(&length,ir,sizeof(length));
    if(!ir||!length||length>64*1024*1024){[self release];return nil;}
    // Catalina MTLLibraryDataWithGLIR::parseBitCodeHeader consumes the
    // uint32 length prefix; bitcodeData passed to the compiler excludes it.
    _ir=[[NSData alloc] initWithBytes:(const uint8_t *)ir+4 length:length];
    _device=[device retain];_type=type;return self;
}
- (void)dealloc {[_ir release];[_device release];[super dealloc];}
- (NSUInteger)functionType {return _type;}
- (id)newFunctionWithPluginData:(NSData *)inputs bitcodeType:(unsigned char)kind {
    if(kind!=1||![inputs isKindOfClass:[NSData class]])return nil;
    // Serialize frontend calls just as the metadata-only Catalina bridge does.
    @synchronized([MTLRenderPipelineDescriptor class]) {
        static void *(*contextCreate)(void), *(*compilerCreate)(void);
        static void (*contextDispose)(void *),(*moduleDispose)(void *),(*compilerDelete)(void *);
        static void *(*generate)(void *,const void *,size_t,void *,size_t,void **,size_t *);
        static int (*build)(void *,const void *,size_t,unsigned,void *,const void **,size_t *,const void **,size_t *,const void **,size_t *,const char **);
        static dispatch_once_t once;
        dispatch_once(&once, ^{
            void *llvm=dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib",RTLD_NOW|RTLD_LOCAL);
            void *frontend=llvm?dlopen("/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle/Contents/PlugIns/libGLDPlugin3902.dylib",RTLD_NOW|RTLD_LOCAL):NULL;
            void *compiler=frontend?dlopen("/System/Library/Extensions/AppleParavirtGPUMetal.bundle/Contents/Resources/libAppleParavirtCompilerPlugin.dylib",RTLD_NOW|RTLD_LOCAL):NULL;
            if(!compiler){fprintf(stderr,"PVG GL lowering load: %s\n",dlerror());return;}
            contextCreate=dlsym(llvm,"LLVMContextCreate");contextDispose=dlsym(llvm,"LLVMContextDispose");moduleDispose=dlsym(llvm,"LLVMDisposeModule");
            generate=dlsym(frontend,"oglCodeGenServiceGenerateIRWithPluginDataAndPluginReturnData");
            void (*initialize)(void)=dlsym(frontend,"oglCodeGenServiceInitialize");
            if(initialize)initialize();else generate=NULL;
            compilerCreate=dlsym(compiler,"MTLCompilerCreate");compilerDelete=dlsym(compiler,"MTLCompilerDelete");build=dlsym(compiler,"MTLCompilerBuildRequestWithOptions");
        });
        if(!contextCreate||!contextDispose||!moduleDispose||!generate||!compilerCreate||!compilerDelete||!build)return nil;
        void *context=contextCreate(),*metadata=NULL;size_t metadataLength=0;
        void *module=generate(context,_ir.bytes,_ir.length,(void *)inputs.bytes,inputs.length,&metadata,&metadataLength);
        NSData *returnData=metadata&&metadataLength?[NSData dataWithBytes:metadata length:metadataLength]:nil;
        free(metadata);
        void *compiler=module?compilerCreate():NULL;
        const void *libraryBytes=NULL,*reflection=NULL,*diagnostics=NULL;
        size_t libraryLength=0,reflectionLength=0,diagnosticsLength=0;
        const char *message=NULL;
        int status=compiler?build(compiler,NULL,0,0,module,&libraryBytes,&libraryLength,&reflection,&reflectionLength,&diagnostics,&diagnosticsLength,&message):-1;
        id function=nil;
        if(status==0&&libraryBytes&&libraryLength){
            if(getenv("PVG_GL_DUMP_IR"))
                [[NSData dataWithBytes:libraryBytes length:libraryLength] writeToFile:
                    [NSString stringWithFormat:@"/tmp/pvg-gl-lowered-%lu.metallib",(unsigned long)_type] atomically:YES];
            dispatch_data_t data=dispatch_data_create(libraryBytes,libraryLength,NULL,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            NSError *error=nil;id<MTLLibrary> library=[_device newLibraryWithData:data error:&error];
            dispatch_release(data);
            if(library)function=[library newFunctionWithName:@"__main"];
            if(!function)fprintf(stderr,"PVG GL lowered library: %s names=%s\n",error.description.UTF8String,library.functionNames.description.UTF8String);
            if(function)objc_setAssociatedObject(function,&functionMetadataKey,returnData?:(id)[NSNull null],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            [library release];
        } else fprintf(stderr,"PVG GL lowering failed: %d %s\n",status,message?:"no result");
        if(compiler)compilerDelete(compiler);
        // The bridge consumes its deserialized copy, not this frontend module.
        if(module)moduleDispose(module);contextDispose(context);
        return function;
    }
}
@end

static id mojaveCoreIRFunction(id self,SEL selector,const void *ir,id inputs,NSUInteger type) {
    (void)selector;(void)inputs;
    return [[PVGGLIRFunction alloc] initWithIR:ir device:self type:type];
}

// Research bridge: Catalina's compiler produces the pipeline, but its old XPC
// reply omits GLD's plugin-return data. Ask the matching Apple frontend for that
// data, once per immutable specialized function. This duplicates frontend work
// on a cache miss; it is not the intended final compiler transport.
static NSData *pluginMetadata(id function) {
    if (!function) return nil;
    id lowered=objc_getAssociatedObject(function,&functionMetadataKey);
    if(lowered)return lowered==[NSNull null]?nil:lowered;
    SEL kind = sel_registerName("bitcodeType");
    if (![function respondsToSelector:kind] ||
        ((unsigned char (*)(id, SEL))objc_msgSend)(function, kind) != 1) return nil;
    @synchronized([MTLRenderPipelineDescriptor class]) {
        id cached = objc_getAssociatedObject(function, &functionMetadataKey);
        if (cached) return cached == [NSNull null] ? nil : cached;
        static void *(*contextCreate)(void);
        static void (*contextDispose)(void *), (*moduleDispose)(void *);
        static void *(*generate)(void *, const void *, size_t, void *, size_t, void **, size_t *);
        static char *(*printModule)(void *);
        static void (*disposeMessage)(char *);
        static dispatch_once_t once;
        dispatch_once(&once, ^{
            void *llvm = dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib", RTLD_NOW | RTLD_LOCAL);
            void *plugin = llvm ? dlopen("/System/Library/Extensions/AppleMetalOpenGLRenderer.bundle/Contents/PlugIns/libGLDPlugin3902.dylib", RTLD_NOW | RTLD_LOCAL) : NULL;
            if (!plugin) { fprintf(stderr, "PVG GL metadata load: %s\n", dlerror()); return; }
            contextCreate = dlsym(llvm, "LLVMContextCreate");
            contextDispose = dlsym(llvm, "LLVMContextDispose");
            moduleDispose = dlsym(llvm, "LLVMDisposeModule");
            printModule = dlsym(llvm, "LLVMPrintModuleToString");
            disposeMessage = dlsym(llvm, "LLVMDisposeMessage");
            generate = dlsym(plugin, "oglCodeGenServiceGenerateIRWithPluginDataAndPluginReturnData");
            void (*initialize)(void) = dlsym(plugin, "oglCodeGenServiceInitialize");
            if (initialize) initialize(); else generate = NULL;
        });
        if (!contextCreate || !contextDispose || !moduleDispose || !generate) {
            fprintf(stderr, "PVG GL metadata API unavailable\n"); abort();
        }
        // Catalina exposes bitcodeData (copies the library's byte range),
        // whereas the newer driver reference uses bitcodeDataInternal.
        SEL bitcode = sel_registerName("bitcodeDataInternal");
        if (![function respondsToSelector:bitcode]) bitcode = sel_registerName("bitcodeData");
        NSData *ir = ((id (*)(id, SEL))objc_msgSend)(function, bitcode);
        NSData *inputs = ((id (*)(id, SEL))objc_msgSend)(function, sel_registerName("pluginData"));
        if (![ir isKindOfClass:[NSData class]] || ![inputs isKindOfClass:[NSData class]]) {
            fprintf(stderr, "PVG GL specialized function missing compiler inputs\n"); abort();
        }
        void *context = contextCreate(), *bytes = NULL;
        size_t length = 0;
        void *module = generate(context, ir.bytes, ir.length, (void *)inputs.bytes, inputs.length, &bytes, &length);
        if (module && getenv("PVG_GL_DUMP_IR") && printModule && disposeMessage) {
            char *text = printModule(module);
            NSString *path = [NSString stringWithFormat:@"/tmp/pvg-gl-%lu.ll", (unsigned long)[function functionType]];
            [[NSString stringWithUTF8String:text] writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL];
            disposeMessage(text);
        }
        NSData *data = bytes && length ? [NSData dataWithBytes:bytes length:length] : nil;
        free(bytes);
        if (module) moduleDispose(module);
        contextDispose(context);
        if (!module) { fprintf(stderr, "PVG GL metadata compilation failed\n"); abort(); }
        objc_setAssociatedObject(function, &functionMetadataKey, data ?: (id)[NSNull null], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if(getenv("PVG_GL_TRACE_COMPILER"))
            fprintf(stderr, "PVG GL metadata: %lu bytes\n", (unsigned long)data.length);
        return data;
    }
}

static id vertexMetadata(id self, SEL selector) {
    return objc_getAssociatedObject(self, &vertexMetadataKey);
}
static id fragmentMetadata(id self, SEL selector) {
    return objc_getAssociatedObject(self, &fragmentMetadataKey);
}
static id coreIRFunction(id self, SEL selector, const void *ir, id inputs, NSUInteger type) {
    id function = ((id (*)(id, SEL, const void *, id, NSUInteger))originalGLIR)
        (self, selector, ir, inputs, type);
    // Big Sur's CoreIR entry names the generated entry __main; Catalina's
    // otherwise equivalent GLIR constructor uses #main. Resolve the named
    // Objective-C ivar, never a build-specific object offset.
    Ivar name = class_getInstanceVariable([function class], "_name");
    if (name && [object_getIvar(function, name) isEqual:@"#main"]) {
        id previous = object_getIvar(function, name);
        object_setIvar(function, name, [@"__main" retain]);
        [previous release];
    }
    return function;
}

static id tracePipeline(id self, SEL selector, id descriptor, NSUInteger options,
                        id *reflection, NSError **error) {
    NSError *local = nil;
    id result = ((id (*)(id, SEL, id, NSUInteger, id *, NSError **))originalPipeline)
        (self, selector, descriptor, options, reflection, error ?: &local);
    if (!result) fprintf(stderr, "PVG GL pipeline error: %s\n",
        [[(error ? *error : local) description] UTF8String]);
    if (result && reflection && *reflection) {
        // A native Big Sur control already returns this metadata. Do not run
        // the legacy bridge (or load a second frontend) in that configuration.
        Method native = class_getInstanceMethod([*reflection class], sel_registerName("fragmentPluginReturnData"));
        if (native && method_getImplementation(native) != (IMP)fragmentMetadata) return result;
        class_addMethod([*reflection class], sel_registerName("vertexPluginReturnData"), (IMP)vertexMetadata, "@@:");
        class_addMethod([*reflection class], sel_registerName("fragmentPluginReturnData"), (IMP)fragmentMetadata, "@@:");
        objc_setAssociatedObject(*reflection, &vertexMetadataKey,
            pluginMetadata([descriptor vertexFunction]), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(*reflection, &fragmentMetadataKey,
            pluginMetadata([descriptor fragmentFunction]), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        // Inspect generated AIR without submitting the known-failing draw.
        if (getenv("PVG_GL_DUMP_IR_ONLY")) _Exit(0);
    }
    return result;
}

static id traceSimplePipeline(id self, SEL selector, id descriptor, NSError **error) {
    NSError *local=nil;
    id result=((id (*)(id,SEL,id,NSError **))originalSimplePipeline)(self,selector,descriptor,error?:&local);
    if(!result) fprintf(stderr,"PVG GL builtin pipeline error: %s\n",[[(error?*error:local) description] UTF8String]);
    return result;
}

__attribute__((constructor)) static void enablePVGProfile(void) {
    // Enable the donor renderer's compatibility profile only for PVG devices.
    // The Big Sur x86 renderer chooses desktop profiles by vendorName, then
    // refines individual capabilities with Metal feature queries. Our current
    // physical host is Intel. Do not advertise Apple-silicon GPU families.
    if (getenv("PVG_GL_DISABLE_COMPAT")) return;
    @autoreleasepool {
        // This adapter is loaded as part of the GL driver, not injected into
        // applications. Normal Finder/LaunchServices launches must not need
        // a shell environment flag. Leave non-PVG devices untouched.
        NSArray *devices=[MTLCopyAllDevices() autorelease];
        BOOL hasPVG=NO;
        for(id device in devices)
            if([NSStringFromClass([device class]) isEqualToString:@"AppleParavirtDevice"])hasPVG=YES;
        if(!hasPVG)return;
        pvgInstallBigSurTextureUsage();
        // Catalina stores this descriptor flag and copies it into the queue;
        // the donor PVG implementation has no uses of the flag. Preserve its
        // observable state on Mojave rather than discarding the setter.
        Class queueDescriptor=objc_getClass("MTLCommandQueueDescriptorInternal");
        if(getenv("PVG_GL_TRACE_RESOURCES")&&!originalResourceDiagnostic) {
            Class resource=objc_getClass("MTLIOAccelResource");
            SEL init=sel_registerName("initWithDevice:remoteStorageResource:options:args:argsSize:");
            Method method=class_getInstanceMethod(resource,init);
            if(method) {
                originalResourceDiagnostic=method_getImplementation(method);
                class_replaceMethod(resource,init,(IMP)traceDepthResource,method_getTypeEncoding(method));
            }
        }
        class_addMethod(objc_getClass("MTLIOAccelDevice"),sel_registerName("newGLDrawable"),(IMP)PVGNewGLDrawable,"@@:");
        BOOL legacyQueue=class_addMethod(queueDescriptor,sel_registerName("setIsOpenGLQueue:"),(IMP)setOpenGLQueue,"v@:c");
        if(legacyQueue)class_addMethod(queueDescriptor,sel_registerName("isOpenGLQueue"),(IMP)isOpenGLQueue,"c@:");
        Class descriptor = objc_getClass("MTLRenderPipelineDescriptorInternal");
        // Research only: Catalina's PVG pipeline serializer does not transmit
        // the native OpenGL-mode bit (private flags bit7). Lowering is handled
        // explicitly above. Full descriptor copy/cache semantics remain to be
        // validated before this adapter is suitable for global activation.
        if(class_addMethod(descriptor,sel_registerName("setOpenGLModeEnabled:"),(IMP)setOpenGLMode,"v@:c"))
            class_addMethod(descriptor,sel_registerName("openGLModeEnabled"),(IMP)openGLMode,"c@:");
        Class passDescriptor=objc_getClass("MTLRenderPassDescriptorInternal");
        class_addMethod(passDescriptor,sel_registerName("setDefaultRasterSampleCount:"),(IMP)requireSupportedRasterSamples,"v@:Q");
        // Live inspection of donor writeDescriptor confirms its PVG wire
        // format omits dither (also on Catalina, which has native accessors).
        // Preserve observable descriptor state; do not claim hardware dithering.
        if(class_addMethod(passDescriptor,sel_registerName("setDitherEnabled:"),(IMP)setDither,"v@:c"))
            class_addMethod(passDescriptor,sel_registerName("isDitherEnabled"),(IMP)isDither,"c@:");
        for(NSString *name in @[@"MTLRenderPassColorAttachmentDescriptorInternal",@"MTLRenderPassDepthAttachmentDescriptorInternal",@"MTLRenderPassStencilAttachmentDescriptorInternal"])
            class_addMethod(objc_getClass(name.UTF8String),sel_registerName("setYInvert:"),(IMP)requireDisabledGLFeature,"v@:c");
        if(class_addMethod(passDescriptor,sel_registerName("setOpenGLModeEnabled:"),(IMP)setOpenGLMode,"v@:c"))
            class_addMethod(passDescriptor,sel_registerName("openGLModeEnabled"),(IMP)openGLMode,"c@:");
        for(NSString *setter in @[@"setPointCoordLowerLeft:",@"setPointSizeOutputVS:",@"setTwoSideEnabled:",@"setNeedsCustomBorderColorSamplers:"])
            class_addMethod(descriptor,sel_registerName(setter.UTF8String),(IMP)requireDisabledGLFeature,"v@:c");
        // Absent-only, default-state bring-up. Reject nondefault coverage;
        // dropping those values would silently break real MSAA semantics.
        class_addMethod(descriptor,sel_registerName("setSampleCoverageInvert:"),(IMP)defaultSampleCoverageInvert,"v@:c");
        class_addMethod(descriptor,sel_registerName("setSampleCoverage:"),(IMP)defaultSampleCoverage,"v@:f");
        if(class_addMethod(descriptor,sel_registerName("setLogicOperationEnabled:"),(IMP)disabledLogicOperation,"v@:c"))
            class_addMethod(descriptor,sel_registerName("setLogicOperation:"),(IMP)storeDisabledLogicOperation,"v@:Q");
        Class textureDescriptor=objc_getClass("MTLTextureDescriptorInternal");
        Class textureBase=objc_getClass("MTLIOAccelTexture");
        SEL oldView=sel_registerName("initWithTexture:pixelFormat:textureType:levels:slices:");
        Method oldViewMethod=class_getInstanceMethod(textureBase,oldView);
        if(oldViewMethod) {
            originalTextureViewInit=method_getImplementation(oldViewMethod);
            class_addMethod(textureBase,sel_registerName("initWithTexture:pixelFormat:textureType:levels:slices:swizzle:"),
                (IMP)initIdentityTextureView,"@@:@QQ{_NSRange=QQ}{_NSRange=QQ}I");
        }
        Class samplerDescriptor=objc_getClass("MTLSamplerDescriptorInternal");
        if(class_addMethod(samplerDescriptor,sel_registerName("setForceSeamsOnCubemapFiltering:"),(IMP)requireDisabledGLFeature,"v@:c"))
            class_addMethod(samplerDescriptor,sel_registerName("forceSeamsOnCubemapFiltering"),(IMP)noUnalignedVertexFetch,"c@:");
        if(class_addMethod(samplerDescriptor,sel_registerName("setPixelFormat:"),(IMP)requireInvalidSamplerFormat,"v@:Q"))
            class_addMethod(samplerDescriptor,sel_registerName("pixelFormat"),(IMP)zeroSamplerValue,"Q@:");
        for(unsigned component=0;component<4;component++) {
            NSString *name=[NSString stringWithFormat:@"setCustomBorderColorValue_%u:",component];
            if(class_addMethod(samplerDescriptor,sel_registerName(name.UTF8String),(IMP)requireZeroBorderComponent,"v@:I")) {
                name=[NSString stringWithFormat:@"customBorderColorValue_%u",component];
                class_addMethod(samplerDescriptor,sel_registerName(name.UTF8String),(IMP)zeroSamplerValue,"I@:");
            }
        }
        // With identity channel mapping, enabling write swizzles changes no
        // pixel values. Retain the flag, but reject nonidentity keys until the
        // older texture-view/serializer path can carry them correctly.
        class_addMethod(textureDescriptor,sel_registerName("setWriteSwizzleEnabled:"),(IMP)setWriteSwizzle,"v@:c");
        class_addMethod(textureDescriptor,sel_registerName("writeSwizzleEnabled"),(IMP)writeSwizzle,"c@:");
        class_addMethod(textureDescriptor,sel_registerName("swizzleKey"),(IMP)identitySwizzle,"I@:");
        class_addMethod(textureDescriptor,sel_registerName("setSwizzleKey:"),(IMP)requireIdentitySwizzle,"v@:I");
        class_addMethod(descriptor, sel_registerName("setDepthStencilWriteDisabled:"),
            (IMP)defaultDepthStencilWriteMode, "v@:c");
        class_addMethod(objc_getClass("MTLRenderPassDescriptorInternal"),
            sel_registerName("setPointCoordYFlipEnabled:"), (IMP)researchPointCoordFlip, "v@:c");
        if(legacyQueue)preserveDescriptorCopies(); // Mojave-only absent API path
        for (id device in devices) {
            if (![NSStringFromClass([device class]) isEqualToString:@"AppleParavirtDevice"])
                continue;
            installSwizzleSerialization(device);
            if(getenv("PVG_GL_TRACE_TEXTURES")&&!originalTextureCreate) {
                SEL create=sel_registerName("newTextureWithDescriptor:");
                Method method=class_getInstanceMethod([device class],create);
                if(method) {
                    originalTextureCreate=method_getImplementation(method);
                    class_replaceMethod([device class],create,(IMP)traceTextureCreate,method_getTypeEncoding(method));
                }
            }
            class_addMethod(objc_getClass("AppleParavirtTexture"),sel_registerName("swizzleKey"),(IMP)textureViewSwizzle,"I@:");
            if(legacyQueue&&!originalQueue) {
                SEL create=sel_registerName("newCommandQueueWithDescriptor:");
                Method method=class_getInstanceMethod([device class],create);
                if(method) {
                    originalQueue=method_getImplementation(method);
                    class_addMethod(objc_getClass("AppleParavirtCommandQueue"),sel_registerName("isOpenGLQueue"),(IMP)isOpenGLQueue,"c@:");
                    class_replaceMethod([device class],create,(IMP)createGLQueue,method_getTypeEncoding(method));
                }
            }
            for (NSString *access in @[@"readsDepth", @"writesDepth", @"readsStencil", @"writesStencil"])
                class_addMethod(objc_getClass("AppleParavirtDepthStencilState"),
                    sel_registerName(access.UTF8String), (IMP)conservativeDepthStencilAccess, "c@:");
            // Big Sur's write-only test calls IOAccelResourceTestEvent(res,1);
            // Catalina's isComplete calls the same API with 0 (all accesses).
            // Reuse the older, stronger test without touching private ivars.
            // This can conservatively delay reuse while readers are active,
            // but must never report a pending writer as completed.
            for (NSString *name in @[@"AppleParavirtTexture", @"AppleParavirtBuffer"]) {
                Class resource = objc_getClass(name.UTF8String);
                Method complete = class_getInstanceMethod(resource, sel_registerName("isComplete"));
                if (complete) class_addMethod(resource, sel_registerName("isWriteComplete"),
                    method_getImplementation(complete), method_getTypeEncoding(complete));
            }
            SEL selector = sel_registerName("vendorName");
            if(getenv("PVG_GL_MAX_SAMPLES_4")&&!originalSampleCountSupport) {
                SEL samples=sel_registerName("supportsTextureSampleCount:");
                Method support=class_getInstanceMethod([device class],samples);
                if(support) {
                    originalSampleCountSupport=method_getImplementation(support);
                    class_replaceMethod([device class],samples,
                        (IMP)supportsConservativeSampleCount,method_getTypeEncoding(support));
                }
            }
            Method method = class_getInstanceMethod([device class], selector);
            if (method) class_replaceMethod([device class], selector,
                (IMP)intelProfile, method_getTypeEncoding(method));
            class_addMethod([device class], sel_registerName("supportsUnalignedVertexFetch"),
                (IMP)noUnalignedVertexFetch, "c@:");
            // Mojave lacks the later format capability query. Do not claim
            // 32-bit-per-channel MSAA before that format has been validated;
            // ordinary RGBA8 MSAA is independent and remains enabled.
            class_addMethod([device class], sel_registerName("supports32bpcMSAATextures"),
                (IMP)noUnalignedVertexFetch, "c@:");
            // Complete the donor initialization query set on older Metal.
            // Only absent methods get conservative answers; native capability
            // results remain intact. These optional features need a separate
            // format/semantics matrix before advertising support on Mojave.
            for (NSString *query in @[@"supportsTexture2DMultisampleArray",
                @"supportsBlackOrWhiteSamplerBorderColors", @"supportsCustomBorderColor",
                @"supportsSamplerAddressModeClampToHalfBorder", @"supportsMirrorClampToEdgeSamplerMode",
                @"supportsQueryTextureLOD", @"supportsForceSeamsOnCubemaps", @"supportsArgumentBuffersTier2",
                @"hasUnifiedMemory"])
                class_addMethod([device class],sel_registerName(query.UTF8String),(IMP)noUnalignedVertexFetch,"c@:");
            class_addMethod([device class], sel_registerName("supportsPrimitiveType:"),
                (IMP)basicPrimitiveType, "c@:Q");
            // Big Sur's GLCoreIR entry uses bitcode type 1; Catalina's GLIR
            // implementation builds the same type-1 function metadata. Big
            // Sur's separate legacy GLIR entry can instead select type 3.
            // Verified by disassembling both framework implementations.
            Method glIR = class_getInstanceMethod([device class],
                sel_registerName("newFunctionWithGLIR:inputsDescription:functionType:"));
            if (glIR) {
                originalGLIR = method_getImplementation(glIR);
                class_addMethod([device class],
                    sel_registerName("newFunctionWithGLCoreIR:inputsDescription:functionType:"),
                    (IMP)coreIRFunction, method_getTypeEncoding(glIR));
            } else if (([NSProcessInfo processInfo].operatingSystemVersion.majorVersion == 10 &&
                        [NSProcessInfo processInfo].operatingSystemVersion.minorVersion == 14) ||
                       getenv("PVG_GL_TEST_MOJAVE_LOWERING")) {
                // Normal Mojave launches need the GLIR lowering bridge too.
                // This loop is restricted to AppleParavirtDevice; Catalina's
                // existing GLIR implementation continues through the branch
                // above. No native-frontend experimental hooks are enabled.
                class_addMethod([device class],sel_registerName("newFunctionWithGLCoreIR:inputsDescription:functionType:"),
                    (IMP)mojaveCoreIRFunction,"@@:^v@Q");
            }
            SEL pipeline = sel_registerName("newRenderPipelineStateWithDescriptor:options:reflection:error:");
            Method pipelineMethod = class_getInstanceMethod([device class], pipeline);
            if (pipelineMethod && !originalPipeline) {
                originalPipeline = method_getImplementation(pipelineMethod);
                class_replaceMethod([device class], pipeline, (IMP)tracePipeline,
                    method_getTypeEncoding(pipelineMethod));
            }
            SEL simple=sel_registerName("newRenderPipelineStateWithDescriptor:error:");
            Method simpleMethod=class_getInstanceMethod([device class],simple);
            if(simpleMethod&&!originalSimplePipeline) {
                originalSimplePipeline=method_getImplementation(simpleMethod);
                class_replaceMethod([device class],simple,(IMP)traceSimplePipeline,method_getTypeEncoding(simpleMethod));
            }
        }
    }
}
