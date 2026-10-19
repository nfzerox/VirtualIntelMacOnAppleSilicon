// Explicit debugger invocation only. No constructor/interposition/installation.
#import <Foundation/Foundation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#import <ImageIO/ImageIO.h>
void VIMCaptureGLState(const char *tag) {@autoreleasepool {
    if(strcmp(tag,"before")&&strcmp(tag,"after"))return;
    NSMutableDictionary *state=[NSMutableDictionary dictionary];
    state[@"EntryError"]=@(glGetError());
    for(NSNumber *selector in @[@(GL_DRAW_BUFFER),@(GL_READ_BUFFER),@(GL_FRAMEBUFFER_BINDING_EXT),
        @(GL_RED_BITS),@(GL_ALPHA_BITS),@(GL_DEPTH_BITS),@(GL_STENCIL_BITS),@(GL_SAMPLES),
        @(GL_SAMPLE_BUFFERS),@(GL_STENCIL_FUNC),@(GL_STENCIL_REF),@(GL_STENCIL_VALUE_MASK),
        @(GL_DEPTH_FUNC),@(GL_BLEND_SRC),@(GL_BLEND_DST),@(GL_CULL_FACE_MODE),@(GL_FRONT_FACE)]) {
        GLint value=0;glGetIntegerv(selector.unsignedIntValue,&value);
        state[[NSString stringWithFormat:@"0x%x",selector.unsignedIntValue]]=@(value);
    }
    for(NSNumber *selector in @[@(GL_DEPTH_TEST),@(GL_STENCIL_TEST),@(GL_SCISSOR_TEST),
        @(GL_BLEND),@(GL_CULL_FACE),@(GL_ALPHA_TEST),@(GL_COLOR_LOGIC_OP),@(GL_FRAMEBUFFER_SRGB_EXT)])
        state[[NSString stringWithFormat:@"enabled-0x%x",selector.unsignedIntValue]]=@(glIsEnabled(selector.unsignedIntValue)!=0);
    GLint viewport[4]={0};glGetIntegerv(GL_VIEWPORT,viewport);
    state[@"Viewport"]=@[@(viewport[0]),@(viewport[1]),@(viewport[2]),@(viewport[3])];
    GLfloat projection[16]={0},model[16]={0};
    glGetFloatv(GL_PROJECTION_MATRIX,projection);glGetFloatv(GL_MODELVIEW_MATRIX,model);
    NSMutableArray *p=[NSMutableArray array],*m=[NSMutableArray array];
    for(unsigned i=0;i<16;i++){[p addObject:@(projection[i])];[m addObject:@(model[i])];}
    state[@"Projection"]=p;state[@"Modelview"]=m;
    CGLContextObj context=CGLGetCurrentContext();CGLPixelFormatObj format=CGLGetPixelFormat(context);
    GLint colorFloat=0,colorSize=0,screen=0;
    CGLGetVirtualScreen(context,&screen);
    CGLDescribePixelFormat(format,screen,kCGLPFAColorFloat,&colorFloat);
    CGLDescribePixelFormat(format,screen,kCGLPFAColorSize,&colorSize);
    state[@"ColorFloat"]=@(colorFloat);state[@"ColorSize"]=@(colorSize);
    if(viewport[2]>0&&viewport[3]>0&&viewport[2]<=8192&&viewport[3]<=8192) {
        size_t w=viewport[2],h=viewport[3];NSMutableData *pixels=[NSMutableData dataWithLength:w*h*4];
        GLint read=0;glGetIntegerv(GL_READ_BUFFER,&read);glReadBuffer(GL_BACK);
        glReadPixels(viewport[0],viewport[1],(GLsizei)w,(GLsizei)h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.mutableBytes);
        glReadBuffer(read);
        uint8_t *b=pixels.mutableBytes;size_t center=(h/2*w+w/2)*4;
        state[@"Center"]=@[@(b[center]),@(b[center+1]),@(b[center+2]),@(b[center+3])];
        CGColorSpaceRef cs=CGColorSpaceCreateDeviceRGB();
        CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)pixels);
        CGImageRef image=CGImageCreate(w,h,8,32,w*4,cs,(CGBitmapInfo)kCGImageAlphaLast,provider,NULL,NO,kCGRenderingIntentDefault);
        NSURL *url=[NSURL fileURLWithPath:[NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"pvg-aperture-probes/gl-%s.png",tag]]];
        CGImageDestinationRef dest=CGImageDestinationCreateWithURL((__bridge CFURLRef)url,CFSTR("public.png"),1,NULL);
        if(image&&dest){CGImageDestinationAddImage(dest,image,NULL);CGImageDestinationFinalize(dest);}
        if(dest)CFRelease(dest);if(image)CGImageRelease(image);CGDataProviderRelease(provider);CGColorSpaceRelease(cs);
    }
    state[@"ExitError"]=@(glGetError());
    NSData *json=[NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:NULL];
    [json writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"pvg-aperture-probes/gl-%s.json",tag]] atomically:YES];
}}
