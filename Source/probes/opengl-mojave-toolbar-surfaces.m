/* Debugger diagnostic, not a guest driver or a runtime interposer.
 * Invoke VIMInspectMojaveToolbarSurfaces() in the actual Keynote process.
 * The private query signatures were checked against native 18G9323 SkyLight;
 * SLSGetSurfaceList's capacity is BYTES. All operations below are read-only.
 * Using a typed period build avoids LLDB's conflicting imported CGS aliases.
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#include <dlfcn.h>

__attribute__((visibility("default")))
id VIMInspectMojaveToolbarSurfaces(void)
{
    NSWindow *window=nil;
    for(NSWindow *candidate in [NSApp windows])
        if([candidate.title isEqualToString:@"General Preferences"]){window=candidate;break;}
    if(!window)return @{@"Error":@"Open Keynote General Preferences first."};
    int (*connectionID)(void)=dlsym(RTLD_DEFAULT,"SLSMainConnectionID");
    int (*getCount)(int,int,unsigned *)=dlsym(RTLD_DEFAULT,"SLSGetSurfaceCount");
    int (*getList)(int,int,unsigned,unsigned *,unsigned *)=dlsym(RTLD_DEFAULT,"SLSGetSurfaceList");
    int (*getBounds)(int,int,unsigned,CGRect *)=dlsym(RTLD_DEFAULT,"SLSGetSurfaceBounds");
    int (*getResolution)(int,int,unsigned,CGFloat *,CGSize *)=dlsym(RTLD_DEFAULT,"SLSGetSurfaceResolution");
    int (*getBinding)(int,int,unsigned,int *,int *,int *)=dlsym(RTLD_DEFAULT,"SLSGetSurfaceBinding");
    int (*getOpacity)(int,int,unsigned,unsigned char *)=dlsym(RTLD_DEFAULT,"SLSGetSurfaceOpacity");
    if(!connectionID||!getCount||!getList||!getBounds||!getResolution||!getBinding||!getOpacity)
        return @{@"Error":@"This diagnostic requires the measured Mojave surface queries."};
    int connection=connectionID(),wid=(int)window.windowNumber;
    unsigned count=0,returned=0,identifiers[32]={0};
    int countError=getCount(connection,wid,&count);
    int listError=getList(connection,wid,sizeof(identifiers),identifiers,&returned);
    NSMutableArray *surfaces=[NSMutableArray array];
    for(unsigned i=0;!listError&&i<returned&&i<32;++i){
        CGRect bounds=CGRectZero;CGSize pixels=CGSizeZero;CGFloat scale=0;
        int mode=-1,first=-1,second=-1;unsigned char opaque=0;
        int boundsError=getBounds(connection,wid,identifiers[i],&bounds);
        int resolutionError=getResolution(connection,wid,identifiers[i],&scale,&pixels);
        int bindingError=getBinding(connection,wid,identifiers[i],&mode,&first,&second);
        int opacityError=getOpacity(connection,wid,identifiers[i],&opaque);
        [surfaces addObject:@{@"ID":@(identifiers[i]),
            @"Bounds":NSStringFromRect(NSMakeRect(bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height)),
            @"Pixels":NSStringFromSize(NSMakeSize(pixels.width,pixels.height)),@"Scale":@(scale),@"Mode":@(mode),
            @"BindingFirst":@(first),@"BindingSecond":@(second),@"Opaque":@(opaque),
            @"BoundsError":@(boundsError),@"ResolutionError":@(resolutionError),
            @"BindingError":@(bindingError),@"OpacityError":@(opacityError)}];
    }
    return @{@"Window":@(wid),@"Connection":@(connection),@"StyleMask":@(window.styleMask),
        @"Count":@(count),@"Returned":@(returned),@"CountError":@(countError),
        @"ListError":@(listError),@"Surfaces":surfaces};
}

/* Pass only a still-live context observed in this process's CGLSetSurface
 * trace. Query its chosen format/renderer without rebinding or drawing. */
__attribute__((visibility("default")))
id VIMInspectMojaveCGLContext(void *opaque)
{
    CGLContextObj context=opaque;
    if(!context)return @{@"Error":@"A live observed CGL context is required."};
    GLint screen=0,renderer=0,dimensions[2]={0};
    CGLError screenError=CGLGetVirtualScreen(context,&screen);
    CGLError rendererError=CGLGetParameter(context,kCGLCPCurrentRendererID,&renderer);
    CGLError sizeError=CGLGetParameter(context,kCGLCPSurfaceBackingSize,dimensions);
    CGLPixelFormatObj format=CGLGetPixelFormat(context);
    if(!format)return @{@"Error":@"The observed context has no pixel format."};
    NSMutableDictionary *report=[@{@"Renderer":@(renderer),@"RendererError":@(rendererError),
        @"Screen":@(screen),@"ScreenError":@(screenError),
        @"Width":@(dimensions[0]),@"Height":@(dimensions[1]),@"SizeError":@(sizeError)} mutableCopy];
    const CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFARendererID,
        kCGLPFADoubleBuffer,kCGLPFAColorSize,kCGLPFAAlphaSize,kCGLPFADisplayMask,kCGLPFAColorFloat};
    NSArray *names=@[@"Accelerated",@"FormatRenderer",@"DoubleBuffer",@"ColorSize",@"AlphaSize",@"ScreenMask",@"FloatColor"];
    for(unsigned i=0;i<sizeof(attributes)/sizeof(*attributes);++i){
        GLint value=-1;CGLError error=CGLDescribePixelFormat(format,screen,attributes[i],&value);
        report[names[i]]=@{@"Value":@(value),@"Error":@(error)};
    }
    return [report autorelease];
}
