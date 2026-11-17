// Finite native-AppKit format negotiation matching Chess's public requests.
// No injection, system changes, application patches or retry loops.
#define GL_SILENCE_DEPRECATION
#import <AppKit/AppKit.h>
#import <OpenGL/OpenGL.h>
#include "opengl-reference-renderer.h"
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv) {@autoreleasepool {
    setbuf(stdout,NULL);
    GLint intel=0;BOOL referenceIntel=NO,chessOrder=NO;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--reference-intel"))referenceIntel=YES;
        else if(!strcmp(argv[i],"--chess-order"))chessOrder=YES;
        else return 64;
    }
    if(referenceIntel) {
        intel=VIMReferenceIntelHD5000();
        if(!intel){fprintf(stderr,"Reference Intel renderer unavailable\n");return 77;}
        printf("reference Intel renderer=0x%x\n",intel);
    }
    CGLRendererInfoObj info=NULL;GLint renderers=0;
    if(chessOrder&&!CGLQueryRendererInfo(0xffffff,&info,&renderers))CGLDestroyRendererInfo(info);
    for(unsigned index=0;index<5;index++) {
        unsigned samples=chessOrder?8-index*2:index*2;
        NSOpenGLPixelFormatAttribute msaa[]={NSOpenGLPFAAllowOfflineRenderers,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFAWindow,NSOpenGLPFAAccelerated,
            NSOpenGLPFANoRecovery,NSOpenGLPFAColorSize,24,NSOpenGLPFAAlphaSize,8,
            NSOpenGLPFADepthSize,16,NSOpenGLPFAStencilSize,1,NSOpenGLPFAMultisample,
            NSOpenGLPFASampleBuffers,1,NSOpenGLPFASamples,samples,0};
        NSOpenGLPixelFormatAttribute plain[]={NSOpenGLPFAAllowOfflineRenderers,
            NSOpenGLPFADoubleBuffer,NSOpenGLPFAWindow,NSOpenGLPFAColorSize,24,
            NSOpenGLPFAAlphaSize,8,NSOpenGLPFADepthSize,16,NSOpenGLPFAStencilSize,1,0};
        NSOpenGLPixelFormatAttribute *attributes=samples?msaa:plain;
        NSOpenGLPixelFormatAttribute reference[40];
        if(intel) {
            unsigned count=samples?sizeof(msaa)/sizeof(msaa[0]):sizeof(plain)/sizeof(plain[0]);
            memcpy(reference,attributes,(count-1)*sizeof(*attributes));
            reference[count-1]=(NSOpenGLPixelFormatAttribute)kCGLPFASupportsAutomaticGraphicsSwitching;
            reference[count]=NSOpenGLPFARendererID;reference[count+1]=intel;
            reference[count+2]=0;attributes=reference;
        }
        @try {
            NSOpenGLPixelFormat *format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
            GLint actual=0;if(format)[format getValues:&actual forAttribute:NSOpenGLPFASamples forVirtualScreen:0];
            printf("samples=%u AppKit format=%s actual=%d\n",samples,format?"yes":"no",actual);
            [format release];
        } @catch(NSException *exception) {
            printf("samples=%u AppKit exception=%s reason=%s\n",samples,
                   exception.name.UTF8String,exception.reason.UTF8String);
        }
        CGLPixelFormatObj native=NULL;GLint count=0;
        CGLError error=CGLChoosePixelFormat((CGLPixelFormatAttribute *)attributes,&native,&count);
        printf("samples=%u CGL error=%d count=%d format=%s\n",samples,error,count,native?"yes":"no");
        if(native)CGLDestroyPixelFormat(native);
    }
    return 0;
}}
