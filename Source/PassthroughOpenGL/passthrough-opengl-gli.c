/* PassthroughOpenGL GLEngine entry boundary. Apple CGL/libGL remain
 * unchanged. Unsupported contracts return errors rather than silently
 * suppressing work. Record layout is measured on 10.9 against native
 * CGL's record copies, not the published CGLPixelFormatObj wrapper layout. */
#define VIM_GLI_PLUGIN 1
#include "direct-cgl.c"
#include <stddef.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include "renderer-memory.h"
static void VIMGLITrace(const char *format,...)
{
    static unsigned emitted;
    if(!getenv("VIM_GLI_TRACE") || __sync_fetch_and_add(&emitted,1)>=128) return;
    char message[1024];va_list ap;va_start(ap,format);
    vsnprintf(message,sizeof(message),format,ap);va_end(ap);
    VIMGLStderr("VIM GLI %s\n",message);
    syslog(LOG_NOTICE,"VIM GLI %s",message);
}
#include "cgs-drawable.c"
#include "native-cgl-recovery.c"
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 && VIM_GLI_DISPATCH_COUNT == 759
static unsigned VIMTigerDispatchCount(void)
{
    pthread_once(&vim_recovery_once,VIMFixNativeRecovery);
    return vim_recovery_ready?vim_native_dispatch_count:0;
}
static void VIMTigerPopulatePrefix(GLIFunctionDispatch *primary,void *secondary,
    const GLIFunctionDispatch *source,unsigned count)
{
    /* Genuine10.4u686 and measured restore759 headers have identical named
     * prototypes for the first686 entries. Copy only that native prefix,
     * retaining the real secondary routing mask. memcpy avoids aliasing
     * differently typed function pointers. This is context creation only;
     * there is no per-command adapter, trampoline or per-frame copy. */
    _Static_assert(sizeof(GLIFunctionDispatch)==759*sizeof(uintptr_t),"Tiger GLI pointer table");
    for(unsigned i=0;i<count;++i){
        size_t offset=i*sizeof(uintptr_t);uintptr_t route=0;
        if(secondary)memcpy(&route,(char *)secondary+offset,sizeof(route));
        memcpy((char *)(route?secondary:(void *)primary)+offset,
            (const char *)source+offset,sizeof(route));
    }
}
#endif
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
#include "tiger-coreimage-state.c"
#else
static int VIMTigerPrepareCIState(void){return 1;}
#endif

#define GLI_EXPORT __attribute__((visibility("default")))
GLI_EXPORT IOReturn VIMGLINotifyCPUSurfaceCopy(uint32_t surface,const IOAccelDeviceRegion *region,
    int32_t destination_x,int32_t destination_y)
{ return VIMImportCPUSurfaceCopy(surface,region,destination_x,destination_y); }
GLI_EXPORT uint32_t VIMGLITryGPUFramebufferCopy(io_connect_t connection,uint32_t surface,
    const IOAccelDeviceRegion *region,int32_t destination_x,int32_t destination_y,IOReturn *status)
{ return VIMTryGPUFramebufferCopy(connection,surface,region,destination_x,destination_y,status); }
#define VIM_ENGINE_ID 0x00030000U
#define VIM_RENDERER_ID 0x01036000U
/* This engine's actual native Apple Software Renderer uses its proper public
 * GenericFloat family, with our own engine identity. Matching CGL explicitly
 * recognizes 0x20400 under kCGLRendererIDMatchingMask during recovery. An
 * arbitrary virtual software family fails that native selection contract.
 * Hardware sorts first under CGL's descending rotated renderer-ID ordering. */
#define VIM_SOFTWARE_RENDERER_ID 0x01030400U
struct GLIPixelFormat {
    struct GLIPixelFormat *next;
    uint32_t renderer,flags,buffers,color,accum,depth,stencil;
    uint16_t aux_depth_stencil,aux,samples,sample_buffers;
    uint32_t sample_modes,sample_alpha,display_mask;
};
/* Physical 13F1911: i386 gliChoosePixelFormat returns records 0x34 apart;
 * x86_64 returns 0x38. Only the leading pointer changes width. */
_Static_assert(sizeof(struct GLIPixelFormat)==48+sizeof(void *),"audited GLI format ABI");
_Static_assert(offsetof(struct GLIPixelFormat,display_mask)==44+sizeof(void *),"GLI display mask ABI");
struct GLIRenderer { struct GLIRenderer *next; uint32_t data[32]; };
_Static_assert(sizeof(struct GLIRenderer)==128+sizeof(void *),"audited GLI renderer ABI");
static pthread_once_t vim_native_recovery_once=PTHREAD_ONCE_INIT;
static int vim_native_recovery_available;
static int vim_native_gl4_available;
static GLint vim_video_mb,vim_texture_mb;
static int vim_memory_valid;
static uint32_t vim_hardware_display_mask=1,vim_software_display_mask=1;
#if VIM_GLI_DISPATCH_COUNT < 974
static void (*vim_compat_texture_barrier)(void);
#endif
static void VIMQueryNativeRecovery(void)
{
    /* One cold context supplies both memory and native availability. No extra
     * dummy framebuffer, TLS switch, or second native context on app launch.
     * Explicit zero retains the old mode for bounded diagnostic comparisons. */
    const char *enabled=getenv("VIM_GL_NATIVE_RECOVERY");
    struct DGNativeRendererInfo info={0};
    vim_memory_valid=DGQueryRendererDetailsWithGL4(&vim_video_mb,&vim_texture_mb,
        enabled&&!strcmp(enabled,"0")?NULL:&info,&vim_native_gl4_available);
    if(vim_memory_valid)
        vim_native_recovery_available=(info.flags&(DG_NATIVE_HARDWARE|DG_NATIVE_SOFTWARE))==
            (DG_NATIVE_HARDWARE|DG_NATIVE_SOFTWARE);
}
static int VIMNativeRecoveryAvailable(void)
{pthread_once(&vim_native_recovery_once,VIMQueryNativeRecovery);return vim_native_recovery_available;}
static int VIMNativeGL4Available(void)
{
    if(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__<1090)return 0;
    pthread_once(&vim_native_recovery_once,VIMQueryNativeRecovery);
    return vim_native_gl4_available;
}
/* High Sierra CGL's native GL4 format mask is 0x40000; context-family
 * flags are separately shifted by CGL before calling gliCreateContext. */
#define VIM_GLI_GL4_FORMAT 0x40000u

GLI_EXPORT void gliInitializeLibrary(void *devices,void *plugins,unsigned count,
    void *unbind,void *bind,void *io
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
    ,unsigned flags
#endif
    )
{
    (void)plugins;(void)unbind;(void)io;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
    (void)flags;
#endif
    vim_native_bind_surface=(VIMNativeBindSurface)bind;
    vim_native_accelerator=devices && count ? *(const io_service_t *)devices : 0;
    VIMSetRendererAccelerator(vim_native_accelerator);
#if VIM_GLI_DISPATCH_COUNT < 974
    // 10.8 cgls still issues private key 300 before sampling queued drawable
    // writes, but its public 972-slot GLI table has no texture_barrier_NV.
    // Resolve the existing canonical backend operation once; do not extend
    // the period dispatch ABI or reject this native ordering requirement.
    vim_compat_texture_barrier=DGGetProcAddress("glTextureBarrierNV");
#endif
    /* Native gfxInitializeLibrary assigns one bit per input accelerator slot,
     * merging slots with the same io_service_t. Its generic software device
     * covers every slot (all bits when no devices exist). These are GL display
     * masks, not a hardcoded count of visible windows/physical displays. */
    vim_hardware_display_mask=0;
    vim_software_display_mask=devices&&count&&count<32?((UINT32_C(1)<<count)-1):UINT32_MAX;
    if(devices&&vim_native_accelerator){
        const io_service_t *services=devices;
        for(unsigned i=0;i<count&&i<32;++i)
            if(services[i]==vim_native_accelerator)vim_hardware_display_mask|=UINT32_C(1)<<i;
    }
    // Qualified paired-driver defaults are process-local to this renderer.
    // A fresh installation must not depend on temporary research marker files.
    // The backing protocol still negotiates admission with the actual kernel;
    // explicit process-local "0" overrides remain useful for controlled probes.
#if !defined(__i386__) || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ != 1040
    setenv("VIM_NATIVE_WINDOW_BACKING","1",0);
    setenv("VIM_RETAINED_CGS_SURFACES","1",0);
    setenv("VIM_DEFERRED_CGS_SURFACES","1",0);
    setenv("VIM_SHARED_VRAM_SURFACES","1",0);
#endif
    // Diagnostics/other unqualified experiments are NOT product defaults.
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    if(setenv){
#endif
    if(!access("/var/tmp/vim-gli-trace",F_OK)) {
        setenv("VIM_GLI_TRACE","1",0);
    }
    if(!access("/var/tmp/vim-gl-damage-present",F_OK)) setenv("VIM_GL_DAMAGE_PRESENT","1",0);
    if(!access("/var/tmp/vim-gl-scatter-readback",F_OK)) setenv("VIM_GL_SCATTER_READBACK","1",0);
    if(!access("/var/tmp/vim-gl-scatter-verify",F_OK)) setenv("VIM_GL_SCATTER_VERIFY","1",0);
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    }
#endif
    VIMGLITrace("initialize slots=%u hardware_mask=%x software_mask=%x",count,
        vim_hardware_display_mask,vim_software_display_mask);
}
GLI_EXPORT void gliTerminateLibrary(void) {VIMReleaseRendererAccelerator();}
GLI_EXPORT unsigned char gliGetVersion(GLint *major,GLint *minor,GLint *engine)
{
    if(!major || !minor || !engine) return 0;
    if(!VIMTigerPrepareCIState())return 0;
    pthread_once(&vim_recovery_once,VIMFixNativeRecovery);
    if(!vim_recovery_ready) return 0;
    *major=2;*minor=3;
    /* Native CGL enumerates GLEngine*.bundle and rejects duplicate engine IDs.
     * Keep Apple's original engine available for real software recovery. */
    *engine=VIM_ENGINE_ID;
    return 1;
}
GLI_EXPORT CGLError gliChoosePixelFormat(struct GLIPixelFormat **out,const GLint *attrs)
{
    if(!out || !attrs) return kCGLBadAddress;
    *out=NULL;
    if(getenv("VIM_GLI_TRACE")) {
        char line[768]="choose:";size_t length=strlen(line);
        for(unsigned i=0;i<64 && attrs[i];++i)
            length+=(size_t)snprintf(line+length,sizeof(line)-length," %x",attrs[i]);
        VIMGLITrace("%s",line);
    }
    GLint depth=0,stencil=0;int double_buffer=0,no_recovery=0,software_only=0,minimum_policy=0;
    int full_screen=0,explicit_window=0;
    for(unsigned i=0;attrs[i];++i) {
        switch(attrs[i]) {
        case 4: /* Native CGL internal choose marker, observed at this seam. */
        case kCGLPFAAllRenderers:case kCGLPFAMaximumPolicy:break;
        case kCGLPFAFullScreen:full_screen=1;break;
        case kCGLPFAWindow:explicit_window=1;break;
        case kCGLPFAOffScreen:
        /* Native iMovie/QuickTime explicitly requests a pbuffer-capable
         * format. The selected record already advertises this capability;
         * CGL's real surface path implements it (including texture sharing).
         * Do not reject this selector while allowing the very same pbuffer
         * on an otherwise identical window-capable format. */
        case kCGLPFAPBuffer:
        case kCGLPFAAccelerated:
        case kCGLPFABackingStore:
        case kCGLPFAClosestPolicy:case kCGLPFAAllowOfflineRenderers:break;
        case kCGLPFADoubleBuffer:double_buffer=1;break;
        case kCGLPFAMinimumPolicy:minimum_policy=1;break;
        case kCGLPFANoRecovery:no_recovery=1;break;
        /* ARGB8888 is described by native CGL as 32 color bits, including
         * alpha. Physical Mavericks Intel accepts both 24- and 32-bit color
         * requests for this mode. Rejecting 32 while advertising that same
         * 32-bit mode forces period WebKit's WebGL chooser into Float. */
        case kCGLPFAColorSize:if(attrs[++i]>32) return kCGLBadPixelFormat;break;
        case kCGLPFAAlphaSize:if(attrs[++i]>8) return kCGLBadPixelFormat;break;
        /* An explicit zero requests no accumulation storage, which this
         * format already provides. Tiger iTunes Artwork's default format
         * includes AccumSize,0; rejecting the selector even at zero makes
         * AppKit return nil and its saver later calls a null GL dispatch.
         * Do not advertise or admit nonzero accumulation buffers. */
        case kCGLPFAAccumSize: {
            GLint bits=attrs[++i];if(bits<0)return kCGLBadValue;
            if(bits)return kCGLBadPixelFormat;
            break;
        }
#if VIM_GLI_DISPATCH_COUNT == 757 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
        /* Actual8R4088/9J3050 cgls uses {Window,20,8,21,8,22,8,Depth,0,Stencil,0}.
         * These private GLI component-size selectors bypass public CGL's
         * attribute conversion. Native Leopard gldChoosePixelFormat consumes
         * each nonnegative size; our actual ARGB8888 supports up to8 each.
         * Do not reject the compositor's RGB8 request as an unknown public
         * enum or claim higher precision than the selected format. */
        case 20:case 21:case 22: {
            GLint bits=attrs[++i];
            if(bits<0)return kCGLBadValue;
            if(bits>8)return kCGLBadPixelFormat;
            break;
        }
#endif
        case kCGLPFADepthSize:depth=attrs[++i];if(depth<0)return kCGLBadValue;if(depth>32)return kCGLBadPixelFormat;break;
        case kCGLPFAStencilSize:stencil=attrs[++i];if(stencil>8) return kCGLBadPixelFormat;break;
        case kCGLPFADisplayMask:++i;break;
        case kCGLPFARendererID: {
            unsigned id=(unsigned)attrs[++i]&0x00fe7f00U;
            if(id==(VIM_SOFTWARE_RENDERER_ID&0x00fe7f00U))software_only=1;
            else if(id!=(VIM_RENDERER_ID&0x00fe7f00U))return kCGLBadPixelFormat;
            break;
        }
        default:return kCGLBadAttribute;
        }
        if(i>256) return kCGLBadAttribute;
    }
    /* Physical15G31 Intel accepts Maps' DepthSize32 under the default
     * closest policy and describes its actual selected depth as24. Keep
     * that real storage/description; do not reject the request or pretend
     * our24-bit renderbuffer acquired32-bit precision. */
    if(depth>24){if(minimum_policy)return kCGLBadPixelFormat;depth=24;}
    int available=VIMNativeRecoveryAvailable(),recovery=!no_recovery&&available;
    if(software_only&&!available)return kCGLBadPixelFormat;
    struct GLIPixelFormat *p=calloc(1,sizeof(*p));
    if(!p) return kCGLBadAlloc;
    p->renderer=VIM_RENDERER_ID;
    /* Genuine Tiger Intel950's chooser uses base0x510, fullscreen bit0x2
     * and window bit0x1. A fullscreen-only request suppresses the default
     * window bit; an explicit combined request retains both. Tiger CGL's
     * own scorer rejects missing requested bits before creating a context.
     * Always returning0x511 therefore made fullscreen choose return no
     * formats, including Keynote's ordinary slideshow request. Later OS
     * formats retain their independently measured capability record. */
    p->flags=__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__==1040?
        (full_screen?(0x512U|(explicit_window?1U:0U)):0x511U):0x0003270f;
    if(VIMNativeGL4Available())p->flags|=VIM_GLI_GL4_FORMAT;
    // Selected-format buffer flags differ from renderer capability masks:
    // single/monoscopic is represented by absence of double/stereo flags.
    p->buffers=double_buffer ? kCGLDoubleBufferBit : 0;
    p->color=kCGLARGB8888Bit;p->accum=__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__==1040?0:0x80000000U;
    p->depth=depth ? kCGL24Bit : kCGL0Bit;
    p->stencil=stencil ? kCGL8Bit : kCGL0Bit;p->display_mask=vim_hardware_display_mask;
    if(recovery||software_only){
        struct GLIPixelFormat *sw=software_only?p:calloc(1,sizeof(*sw));
        if(!sw){free(p);return kCGLBadAlloc;}
        *sw=*p;sw->renderer=VIM_SOFTWARE_RENDERER_ID;
        /* Measured native Mavericks GLI software flags from the real engine,
         * not the hardware record's accelerated/compliance bits. */
        sw->flags=__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__==1040?0x24dd:0x000626dd;
        if(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__>=1090 && !VIMNativeGL4Available())
            sw->flags&=~VIM_GLI_GL4_FORMAT;
        sw->display_mask=vim_software_display_mask;
        if(!software_only)p->next=sw;
    }
    *out=p;return kCGLNoError;
}
GLI_EXPORT CGLError gliDestroyPixelFormat(struct GLIPixelFormat *p)
{
    while(p) { struct GLIPixelFormat *next=p->next;free(p);p=next; }
    return kCGLNoError;
}
GLI_EXPORT CGLError gliQueryRendererInfo(struct GLIRenderer **out,unsigned mask)
{
    if(!out) return kCGLBadAddress;
    *out=NULL;
    int hardware=(mask&vim_hardware_display_mask)!=0;
    int software=(mask&vim_software_display_mask)&&VIMNativeRecoveryAvailable();
    if(!hardware&&!software)return kCGLNoError;
    struct GLIRenderer *r=calloc(1,sizeof(*r));
    if(!r) return kCGLBadAlloc;
    r->data[0]=VIM_RENDERER_ID;r->data[1]=__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__==1040?0xb513:0x0003270f;
    r->data[2]=kCGLMonoscopicBit|kCGLSingleBufferBit|kCGLDoubleBufferBit;
    r->data[3]=kCGLARGB8888Bit;r->data[5]=kCGL0Bit|kCGL24Bit;
    r->data[6]=kCGL0Bit|kCGL8Bit;
#if VIM_GLI_DISPATCH_COUNT != 757 && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ != 1040
    r->data[7]=vim_hardware_display_mask;
#endif
    VIMNativeRecoveryAvailable();
    if(VIMNativeGL4Available())r->data[1]|=VIM_GLI_GL4_FORMAT;
    if(!vim_memory_valid) { free(r);return kCGLBadRendererInfo; }
    /* Native CGL prepends one pointer to the GLI record. DescribeRenderer's
     * MB fields are public +0x54/0x5c (64-bit) or +0x4c/0x54 (i386). */
#if VIM_GLI_DISPATCH_COUNT == 757 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    /* Native9J3050 CoreGraphics compares GLI record+0x30 (i386) against
     * GLCompositorMinimumVRAM. The physical9L30 driver's gldGetRendererInfo
     * writes its real video/texture MB to+0x30/+0x34 in the56-byte record;
     * public CGLDescribeRenderer uses the same compact period fields.
     * Newer records moved these fields. Reporting only the newer positions
     * falsely describes zero VRAM to Leopard's desktop admission gate. */
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    /* Tiger's compact native records contain bytes, not the later public
     * Megabytes properties queried by the host transport. Preserve units
     * and saturate rather than wrap a modern host's larger GPU budget. */
    r->data[11]=VIMRendererMemoryBytes(vim_video_mb);
    r->data[12]=VIMRendererMemoryBytes(vim_texture_mb);
#else
    r->data[11]=(uint32_t)vim_video_mb;r->data[12]=(uint32_t)vim_texture_mb;
#endif
#else
    r->data[17]=(uint32_t)vim_video_mb;r->data[19]=(uint32_t)vim_texture_mb;
#endif
    if(software){
        struct GLIRenderer *sw=hardware?calloc(1,sizeof(*sw)):r;if(!sw){free(r);return kCGLBadAlloc;}
        *sw=*r;sw->data[0]=VIM_SOFTWARE_RENDERER_ID;sw->data[1]=__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__==1040?0x65d:0x000626dd;
        if(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__>=1090 && !VIMNativeGL4Available())
            sw->data[1]&=~VIM_GLI_GL4_FORMAT;
#if VIM_GLI_DISPATCH_COUNT == 757 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
        sw->data[11]=sw->data[12]=0;
#else
        sw->data[7]=vim_software_display_mask;sw->data[17]=sw->data[19]=0;
#endif
        if(hardware)r->next=sw;
    }
    *out=r;return kCGLNoError;
}
GLI_EXPORT CGLError gliDestroyRendererInfo(struct GLIRenderer *r)
{
    while(r) { struct GLIRenderer *next=r->next;free(r);r=next; }return kCGLNoError;
}
GLI_EXPORT CGLError gliCreateContext(GLIContext *out,const struct GLIPixelFormat *p,
    GLIContext share,GLIFunctionDispatch *dispatch,void *fallback,unsigned flags)
{
    (void)fallback;
    if(!out || !dispatch) return kCGLBadAddress;
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 && VIM_GLI_DISPATCH_COUNT == 759
    unsigned native_slots=VIMTigerDispatchCount();
    if(native_slots!=686&&native_slots!=759)return kCGLBadState;
#endif
    VIMGLITrace("create renderer=%x next=%x flags=%x",p?p->renderer:0,
        p&&p->next?p->next->renderer:0,flags);
    *out=NULL;if(!p)return kCGLBadPixelFormat;
    if(!VIMTigerPrepareCIState())return kCGLBadState;
    int software=p->renderer==VIM_SOFTWARE_RENDERER_ID;
    if(p->renderer!=VIM_RENDERER_ID&&!software)return kCGLBadPixelFormat;
    if(p->next&&(!VIMNativeRecoveryAvailable()||p->next->next||
       p->next->renderer!=(software?VIM_RENDERER_ID:VIM_SOFTWARE_RENDERER_ID)))return kCGLBadPixelFormat;
    unsigned family=flags&0x18;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1070 && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1090
    // Stock Lion/Mountain CGLCreateContext masks pixel-record byte3 with
    // 0x11 and forwards it unchanged: bit0x10 means GL3, not GL4. Mavericks+
    // instead shifts the profile family down (GL3=8, GL4=0x10). Measured
    // on the native physical frameworks and Lion's public CGL entry trace.
    family=(flags&0x10)?8:0;
#endif
    int allow_core=__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__>=1070;
    if(family && ((family!=8 && family!=0x10) || !allow_core ||
                  (family==0x10 && !VIMNativeGL4Available())))return kCGLBadPixelFormat;
    const unsigned profile=family==0x10?DG_CGL_PROFILE_GL4:family?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFADepthSize,
        p->depth==kCGL0Bit ? 0 : 24,kCGLPFAStencilSize,p->stencil==kCGL0Bit ? 0 : 8,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,
        kCGLPFADoubleBuffer,0};
    if(!(p->buffers&kCGLDoubleBufferBit)) attrs[7]=0;
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint count=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!e) e=CGLCreateContext(pf,share ? &((struct VIMContext *)share)->api : NULL,&ctx);
    if(pf) CGLReleasePixelFormat(pf);
    if(e) return e;
    struct VIMContext *v=(struct VIMContext *)ctx;
    v->native_renderer_screens=p->next?2:1;
    v->native_renderer_kinds=software?1:p->next?2:0;
    if(software){
        struct VIMSavedBinding previous=VIMSaveGLI(ctx->rend);struct DGNativeRendererInfo info={0};
        int selected=VIMBindGLI(ctx->rend)&&DGSelectNativeRenderer(1,&info)==1;
        if(selected)v->native_renderer_kind=1;
        VIMRestoreGLI(previous,ctx->rend);
        if(!selected){CGLDestroyContext(ctx);return kCGLBadRendererInfo;}
    }
    ctx->disp.swap_APPLE=VIMGLISwap;
    ctx->disp.flush_render_APPLE=VIMGLIFlushRender;
    ctx->disp.finish_render_APPLE=VIMGLIFinishRender;
    ctx->disp.flush=VIMGLIFlush;ctx->disp.finish=VIMGLIFinish;
    *out=ctx->rend;
    // The second table is an in/out routing mask, not a second flat output.
    // Nonzero slots route there; zero slots route to the primary table.
    // WindowServer passes an all-zero dummy which must never be overwritten.
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 && VIM_GLI_DISPATCH_COUNT == 759
    if(native_slots==686)VIMTigerPopulatePrefix(dispatch,fallback,&ctx->disp,native_slots);
    else
#endif
        VIMPopulateRoutedDispatch(dispatch,fallback,&ctx->disp);
    return kCGLNoError;
}
GLI_EXPORT CGLError gliCreateContextWithShared(GLIContext *out,const struct GLIPixelFormat *p,
    GLIContext share,GLIFunctionDispatch *dispatch,void *fallback,unsigned flags)
{ return gliCreateContext(out,p,share,dispatch,fallback,flags); }
GLI_EXPORT CGLError gliDestroyContext(GLIContext c)
{
    if(!c) return kCGLBadContext;
    return CGLDestroyContext(&((struct VIMContext *)c)->api);
}
static void VIMDestroyGLIDrawables(struct VIMContext *v)
{
    GLIContext c=(GLIContext)v;
    VIMEndScopedDrawable(c); // final backend retirement still drains on failure
    struct VIMCGSDrawable *d=v->drawable,*primary=v->primary_drawable;
    v->drawable=NULL;v->primary_drawable=NULL;
    /* Release GPU backing before destroying the backend that owns it. */
    if(d!=primary) VIMDestroyDrawable(d);
    VIMDestroyDrawable(primary);
    while(v->spare_drawables) {
        d=v->spare_drawables;v->spare_drawables=d->cache_next;d->cache_next=NULL;VIMDestroyDrawable(d);
    }
#if defined(VIM_GLI_CONNECTION_REUSE_EXPERIMENT)
    while(v->idle_drawable_connection_count) {
        unsigned index=--v->idle_drawable_connection_count;
        IOServiceClose(v->idle_drawable_connections[index]);v->idle_drawable_connections[index]=0;
    }
#endif
}
GLI_EXPORT CGLError gliGetInteger(GLIContext c,unsigned key,GLint *value)
{
    VIMGLITrace("get integer key=%x",key);
    if(!value) return kCGLBadAddress;
    if(key==kCGLCPDispatchTableSize) {
#if defined(__i386__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 && VIM_GLI_DISPATCH_COUNT == 759
        unsigned slots=VIMTigerDispatchCount();if(!slots)return kCGLBadState;
        *value=slots*sizeof(void *);
#else
        *value=VIM_GLI_DISPATCH_COUNT*sizeof(void *);
#endif
        return kCGLNoError;
    }
    if(!c) return kCGLBadContext;
    struct VIMContext *v=(struct VIMContext *)c;
    switch(key) {
    /* Native 14F27 CGLGetShareGroup asks the engine for a gfxSharedState*
     * with selector0x3a2, then returns the output without checking CGLError.
     * Our process-local GL object group is NOT a native OpenCL share group.
     * Publish no such group rather than leave a stack pointer uninitialized
     * or expose a VIMShareGroup to libGFXShared's incompatible lock layout. */
    case 0x3a2: { uintptr_t no_native_group=0;memcpy(value,&no_native_group,sizeof(no_native_group));break; }
    case 0x7aa:memcpy(value,&v->client_storage,sizeof(uintptr_t));break;
    case 0x2bd:*value=v->plugin_policy;break;
    case kCGLCPSwapInterval:*value=v->swap_interval;break;
    case kCGLCESwapLimit:*value=v->swap_limit;break;
    case kCGLCPCurrentRendererID:*value=v->native_renderer_kind?VIM_SOFTWARE_RENDERER_ID:VIM_RENDERER_ID;break;
    case kCGLCPGPUVertexProcessing:case kCGLCPGPUFragmentProcessing:*value=!v->native_renderer_kind;break;
    case kCGLCEMPEngine:*value=0;break;
    case kCGLCPMPSwapsInFlight:*value=v->mp_swaps;break;
    case kCGLCPHasDrawable:*value=v->drawable || !v->owns_buffer;break;
    default:return kCGLBadEnumeration;
    }return kCGLNoError;
}
GLI_EXPORT CGLError gliSetInteger(GLIContext c,unsigned key,const GLint *value)
{
    if(!c) return kCGLBadContext;if(!value) return kCGLBadAddress;
    struct VIMContext *v=(struct VIMContext *)c;
    switch(key) {
    case 0x7aa:memcpy(&v->client_storage,value,sizeof(uintptr_t));break;
    case 0x2bd:v->plugin_policy=*value;break;
    case kCGLCPSwapInterval:v->swap_interval=*value;break;
    case kCGLCESwapLimit:
        /* Actual 10K549 Intel gldSetInteger accepts this boolean (default1),
         * independently of swap interval. CGL's enable/disable/isEnabled and
         * Core Image use it. Our swaps already finish synchronously, so no
         * asynchronous queue can exceed either policy. Retain the requested
         * state; do not invent a delay, remove a fence or change vsync. */
        v->swap_limit=!!*value;break;
    case kCGLCPSurfaceOpacity:
        /* Native Intel's Mercury gldSetInteger explicitly accepts this
         * notification without changing GL state. CGL/CGS own surface opacity;
         * do not rewrite alpha or duplicate their window-state storage. */
        break;
    case kCGLCPMPSwapsInFlight:v->mp_swaps=*value;break;
    case kCGLCEMPEngine:if(*value) return kCGLBadState;break;
    case 300: {
        /* Native Intel Mavericks accepts key 300/value 0 and makes earlier
         * ordinary-window writes visible through the retained 3e7 texture.
         * Keep the real GL barrier; the executor also refreshes opposite-row
         * private views on the GPU. Do not substitute a no-op or glFinish. */
#if VIM_GLI_DISPATCH_COUNT >= 974
        void (*barrier)(void)=VIMFunction_texture_barrier_NV;
#else
        void (*barrier)(void)=vim_compat_texture_barrier;
#endif
        if(!barrier)return kCGLBadEnumeration;
        struct VIMSavedBinding previous=VIMSaveGLI(c);
        if(!VIMBindGLI(c)){VIMDiscardGLI(previous);return kCGLBadContext;}
        barrier();VIMRestoreGLI(previous,c);break;
    }
    case 0x3e5:case 0x3e6:case 0x3e7: {
        CGLError error=VIMTextureFromAcceleratorSurface(v,key,(const uint32_t *)value);
        if(error)VIMGLITrace("surface texture failed key=%x error=%d id=%x target=%x internal=%x size=%u,%u",key,error,value[0],value[1],value[2],value[3],value[4]);
        return error;
    }
#if VIM_HAS_IOSURFACE
    case 0x38e: {
        const uint32_t *a=(const uint32_t *)value;
        VIMGLITrace("IOSurface import id=%u target=%x size=%u,%u",a[0],a[1],a[3],a[4]);
        IOSurfaceRef s=IOSurfaceLookup(a[0]);if(!s) return kCGLBadValue;
        CGLError e=CGLTexImageIOSurface2D(&v->api,a[1],a[2],a[3],a[4],a[5],a[6],s,a[7]);
        CFRelease(s);return e;
    }
#endif
    default:VIMGLITrace("unsupported set integer=%x",key);return kCGLBadEnumeration;
    }return kCGLNoError;
}
GLI_EXPORT CGLError gliAttachDrawableWithOptions(GLIContext c,unsigned type,const void *data,
    unsigned screen,unsigned options)
{
    (void)options;
    VIMGLITrace("attach type=%x data=%p screen=%u",type,data,screen);
    if(!c)return kCGLBadContext;
    struct VIMContext *v=(struct VIMContext *)c;
    if(bound!=c && !VIMEndPreviousGLI())return kCGLBadDrawable;
    if(!VIMEndScopedDrawable(c))return kCGLBadDrawable;
    if(screen>=v->native_renderer_screens)return kCGLBadDisplay;
    /* Follow the record order CGL supplies, including software-only local
     * screen zero; do not assume the driver's original linked-list order. */
    unsigned kind=(v->native_renderer_kinds>>screen)&1;
    if(kind!=v->native_renderer_kind){
        struct VIMSavedBinding previous=VIMSaveGLI(c);struct DGNativeRendererInfo info={0};
        int selected=VIMBindGLI(c)&&DGSelectNativeRenderer(kind,&info)==1;
        if(selected){v->native_renderer_kind=kind;VIMInvalidateImportedDrawables(v);VIMInvalidateFramebufferAttachments(c);}
        VIMRestoreGLI(previous,c);
        if(!selected)return kCGLBadRendererInfo;
    }
    if(type==0 && !data) {
        struct VIMContext *v=(struct VIMContext *)c;
        VIMReplaceDrawable(v,NULL);return kCGLNoError;
    }
    if(type==kCGLPFAWindow && data) {
        struct VIMContext *v=(struct VIMContext *)c;
        const uint32_t *record=data;
        /* 10.8 CoreGraphics cglsAttachSurface explicitly writes {0,0,
         * acceleratorSurfaceID} and calls GLI with type 0x50. Unlike an
         * application CGL window it has no bind-surface callback/CGS window.
         * 10.9 uses type 0x5a for this same accelerator-owned contract below.
         * Bind the existing kernel surface; never manufacture a CGS window
         * or call client-side CGSGetSurfaceResolution on this server record. */
        if(!vim_native_bind_surface && !record[0] && !record[1] && record[2])
            return VIMCreateAcceleratorDrawable(v,record,0);
        if(VIMGLProductFeature("VIM_NATIVE_WINDOW_BACKING"))
            return VIMCreateNativeWindowDrawable(v,data);
        return VIMCreateDrawable(v,data);
    }
    if(type==0x5a && data) return VIMCreateAcceleratorDrawable((struct VIMContext *)c,data,1);
    if(type==kCGLPFAFullScreen && data) return VIMCreateFullscreen((struct VIMContext *)c,data);
    if(type!=kCGLPFAOffScreen || !data) return kCGLBadDrawable;
    struct Offscreen { GLint width,height,row,reserved;void *pixels; };
    const struct Offscreen *d=data;
    return CGLSetOffScreen(&((struct VIMContext *)c)->api,d->width,d->height,d->row,d->pixels);
}
GLI_EXPORT CGLError gliAttachDrawable(GLIContext c,unsigned type,const void *data,unsigned screen)
{ return gliAttachDrawableWithOptions(c,type,data,screen,0); }
// This entry is not semantically identical across the period engines.
// Actual 9J3050 and 10J3250 gliSwapBuffers validate/select dispatch and call the
// renderer's swap callback. 11G63's compatibility entry is a bare return;
// its real swapping uses the swap_APPLE dispatch slot. Preserve that split.
GLI_EXPORT void gliSwapBuffers(GLIContext c) {
#if VIM_GLI_DISPATCH_COUNT == 757 || VIM_GLI_DISPATCH_COUNT == 764 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
    if(c)VIMGLISwap(c);
#else
    (void)c;
#endif
}
GLI_EXPORT void gliGetAttribute(GLIContext c,unsigned mask,void *data)
{ (void)mask;(void)data;if(c) VIMUnsupported(c,973); }
GLI_EXPORT void gliSetAttribute(GLIContext c,unsigned mask,const void *data)
{ (void)mask;(void)data;if(c) VIMUnsupported(c,973); }
GLI_EXPORT void gliCopyAttributes(GLIContext source,GLIContext destination)
{ (void)source;if(destination) VIMUnsupported(destination,973); }
GLI_EXPORT CGLError gliGetMemoryAnnotationList(GLIContext c,void *data)
{ (void)c;(void)data;return kCGLBadEnumeration; }
