#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>
#import <Metal/Metal.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#import <IOSurface/IOSurface.h>
#import <OpenGL/CGLIOSurface.h>
#include <unistd.h>
@interface ARGBProbeProvider : NSObject
@end
@implementation ARGBProbeProvider
- (void)provideImageData:(void *)data bytesPerRow:(size_t)row origin:(size_t)x :(size_t)y size:(size_t)width :(size_t)height userInfo:(id)info {
    (void)x;(void)y;(void)info;
    for(size_t j=0;j<height;j++)for(size_t i=0;i<width;i++) {
        unsigned char *p=(unsigned char *)data+j*row+i*4;
        p[0]=255;p[1]=17;p[2]=34;p[3]=51;
    }
}
@end
int main(int argc, char **argv) {
    alarm(30);
    @autoreleasepool {
        if (argc != 3 && argc != 4) return 64;
        unsigned size = argc == 4 ? (unsigned)atoi(argv[3]) : 256;
        if (!size || size > 4096) return 64;
        CIImage *image = strcmp(argv[1], "solid") == 0 ?
            [[CIImage imageWithColor:[CIColor colorWithRed:1 green:0.5 blue:0.25]] imageByCroppingToRect:CGRectMake(0,0,256,256)] :
            [CIImage imageWithContentsOfURL:[NSURL fileURLWithPath:@(argv[1])]];
        if(!strcmp(argv[1],"provider")) {
            CGColorSpaceRef adobe=CGColorSpaceCreateWithName(kCGColorSpaceAdobeRGB1998);
            image=[CIImage imageWithImageProvider:[ARGBProbeProvider new]
                size:1870 :1052 format:kCIFormatARGB8 colorSpace:adobe
                options:@{kCIImageProviderTileSize:@[@256,@256]}];
            CGColorSpaceRelease(adobe);
        }
        IOSurfaceRef sourceSurface=NULL;
        if (!strcmp(argv[1],"surface-source")) {
            unsigned width=getenv("PVG_SOURCE_WIDTH")?atoi(getenv("PVG_SOURCE_WIDTH")):256;
            unsigned height=getenv("PVG_SOURCE_HEIGHT")?atoi(getenv("PVG_SOURCE_HEIGHT")):256;
            if(!width||!height||width>8192||height>8192)return 64;
            unsigned pitch=getenv("PVG_SOURCE_ROW_BYTES")?atoi(getenv("PVG_SOURCE_ROW_BYTES")):(width*4+255)&~255;
            if(pitch<width*4||pitch>width*4+4096)return 64;
            sourceSurface=IOSurfaceCreate((CFDictionaryRef)@{(id)kIOSurfaceWidth:@(width),
                (id)kIOSurfaceHeight:@(height),(id)kIOSurfaceBytesPerElement:@4,
                (id)kIOSurfaceBytesPerRow:@(pitch),(id)kIOSurfacePixelFormat:@(0x52474241)});
            if(!sourceSurface)return 7;
            if(getenv("PVG_SOURCE_PRIME")) {
                CIContext *prime=[CIContext contextWithMTLDevice:MTLCreateSystemDefaultDevice()];
                unsigned char initial[4*16*16]={0};
                CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
                [prime render:[CIImage imageWithIOSurface:sourceSurface] toBitmap:initial
                    rowBytes:16*4 bounds:CGRectMake(0,0,16,16) format:kCIFormatRGBA8 colorSpace:space];
                CGColorSpaceRelease(space);
            }
            if(IOSurfaceLock(sourceSurface,0,NULL))return 7;
            unsigned char *sourcePixels=IOSurfaceGetBaseAddress(sourceSurface);
            for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
                unsigned char *pixel=sourcePixels+y*pitch+x*4;
                pixel[0]=51;pixel[1]=102;pixel[2]=153;pixel[3]=255;
            }
            IOSurfaceUnlock(sourceSurface,0,NULL);
            CGColorSpaceRef sourceColor=NULL;
            if(getenv("PVG_SOURCE_PROFILE")) {
                NSData *icc=[NSData dataWithContentsOfFile:@(getenv("PVG_SOURCE_PROFILE"))];
                sourceColor=icc?CGColorSpaceCreateWithICCData((CFDataRef)icc):NULL;
                if(!sourceColor)return 10;
            }
            image=[CIImage imageWithIOSurface:sourceSurface options:sourceColor?
                @{kCIImageColorSpace:(id)sourceColor}:nil];
            if(sourceColor)CGColorSpaceRelease(sourceColor);
        }
        if (!image) return 1;
        if(getenv("PVG_SOURCE_FLIP"))
            image=[image imageByApplyingTransform:CGAffineTransformMake(1,0,0,-1,0,image.extent.size.height)];
        CGRect e = image.extent;
        if (!strstr(argv[2], "crop"))
            image = [image imageByApplyingTransform:CGAffineTransformMakeScale(size/e.size.width,size/e.size.height)];
        CGLContextObj gl = NULL; CGLPixelFormatObj pixelFormat = NULL;
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        if (strstr(argv[2], "display")) {
            CGColorSpaceRelease(cs);
            cs = CGDisplayCopyColorSpace(CGMainDisplayID());
            if (!cs) return 9;
            image = [image imageByColorMatchingWorkingSpaceToColorSpace:cs];
            image = [image imageByColorMatchingColorSpaceToWorkingSpace:cs];
        }
        BOOL surfaceTarget = !strcmp(argv[2], "gl-surface") || !strcmp(argv[2], "gl-surface-crop");
        BOOL framebuffer = !strcmp(argv[2], "gl-fbo") || surfaceTarget;
        BOOL useGL = !strcmp(argv[2], "gl") || framebuffer;
        if (useGL) {
            CGLPixelFormatAttribute attrs[] = {kCGLPFAAccelerated, 0};
            GLint count = 0;
            if (CGLChoosePixelFormat(attrs, &pixelFormat, &count) || !pixelFormat ||
                CGLCreateContext(pixelFormat, NULL, &gl) || CGLSetCurrentContext(gl)) return 3;
        }
        CIContext *ctx = useGL ?
            [CIContext contextWithCGLContext:gl pixelFormat:pixelFormat colorSpace:cs options:nil] :
            strncmp(argv[2], "cpu", 3) == 0 ?
            [CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES}] :
            [CIContext contextWithMTLDevice:MTLCopyAllDevices().firstObject];
        size_t length = (size_t)size*size*4;
        unsigned char *pixels = calloc(1,length);
        if (!pixels) return 4;
        if (framebuffer) {
            GLuint texture, target;
            GLenum textureTarget=surfaceTarget?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D;
            IOSurfaceRef surface=NULL;
            glGenTextures(1,&texture); glBindTexture(textureTarget,texture);
            if(surfaceTarget) {
                surface=IOSurfaceCreate((CFDictionaryRef)@{(id)kIOSurfaceWidth:@(size),
                    (id)kIOSurfaceHeight:@(size),(id)kIOSurfaceBytesPerElement:@4,
                    (id)kIOSurfaceBytesPerRow:@(size*4),(id)kIOSurfacePixelFormat:@(0x42475241)});
                if(!surface||CGLTexImageIOSurface2D(gl,textureTarget,GL_RGBA,size,size,
                    GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0))return 7;
            } else glTexImage2D(textureTarget,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            glGenFramebuffersEXT(1,&target); glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,target);
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,textureTarget,texture,0);
            if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT) return 5;
            glViewport(0,0,size,size);
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,size,0,size,-1,1);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            CGRect sourceRect=strstr(argv[2],"crop")?CGRectMake(0,0,size,size):image.extent;
            [ctx drawImage:image inRect:CGRectMake(0,0,size,size) fromRect:sourceRect];
            glFinish(); glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            GLenum error=glGetError();
            if (error) { fprintf(stderr,"GL error=%x\n",error); return 6; }
            glDeleteFramebuffersEXT(1,&target); glDeleteTextures(1,&texture);
            if(surface)CFRelease(surface);
        } else if (strstr(argv[2], "surface")) {
            IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)@{
                (id)kIOSurfaceWidth:@(size), (id)kIOSurfaceHeight:@(size),
                (id)kIOSurfaceBytesPerElement:@4, (id)kIOSurfaceBytesPerRow:@(size*4),
                (id)kIOSurfacePixelFormat:@(0x52474241)});
            if (!surface) return 7;
            [ctx render:image toIOSurface:surface bounds:CGRectMake(0,0,size,size) colorSpace:cs];
            if (IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL)) return 8;
            memcpy(pixels,IOSurfaceGetBaseAddress(surface),length);
            IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
            CFRelease(surface);
        } else [ctx render:image toBitmap:pixels rowBytes:size*4 bounds:CGRectMake(0,0,size,size) format:kCIFormatRGBA8 colorSpace:cs];
        unsigned long long sum = 0;
        for (size_t i=0; i<length; i++) if (i%4 != 3) sum += pixels[i];
        fprintf(stderr, "%s %ux%u RGB checksum=%llu\n", argv[2], size, size, sum);
        free(pixels);
        CGColorSpaceRelease(cs);
        ctx = nil;
        if (gl) { CGLSetCurrentContext(NULL); CGLDestroyContext(gl); }
        if (pixelFormat) CGLDestroyPixelFormat(pixelFormat);
        if (sourceSurface) CFRelease(sourceSurface);
        return sum == 0 ? 2 : 0;
    }
}
