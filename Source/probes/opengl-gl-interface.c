/* Read-only CGL/GLI seam probe. Build with the target OS SDK and OpenGL,
 * IOSurface and CoreFoundation frameworks. Never installs a renderer. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
static void check(int good, const char *name) {
    printf("%s %s\n", good ? "PASS" : "FAIL", name);
    if (!good) ++failures;
}
static GLuint shader(GLenum type, const char *text) {
    GLuint s = glCreateShader(type);
    GLint good = 0;
    glShaderSource(s, 1, &text, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &good);
    if (!good) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
    }
    check(good, "GLSL compile");
    return s;
}
static void number(CFMutableDictionaryRef d, CFStringRef key, int value) {
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &value);
    CFDictionarySetValue(d, key, n);
    CFRelease(n);
}
int main(int argc, char **argv) {
    int software = argc > 1 && strcmp(argv[1], "--software") == 0;
    CGLPixelFormatAttribute attrs[] = {
        software ? kCGLPFAAllowOfflineRenderers : kCGLPFAAccelerated,
        kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
        kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
        getenv("VIM_GL_OFFSCREEN") ? kCGLPFAOffScreen :
            (getenv("VIM_GL_ONLINE_ONLY") ? kCGLPFANoRecovery : kCGLPFAAllowOfflineRenderers),
        getenv("VIM_GL_NO_RECOVERY") ? kCGLPFANoRecovery : kCGLPFAAllowOfflineRenderers,
        (CGLPixelFormatAttribute)0
    };
    CGLPixelFormatObj pf = NULL;
    CGLContextObj ctx = NULL;
    GLint count = 0, accelerated = 0;
    CGLError e = CGLChoosePixelFormat(attrs, &pf, &count);
    if (e || !pf) { fprintf(stderr, "pixel format: %s\n", CGLErrorString(e)); return 2; }
    CGLDescribePixelFormat(pf, 0, kCGLPFAAccelerated, &accelerated);
    e = CGLCreateContext(pf, NULL, &ctx);
    if (e || !ctx) { fprintf(stderr, "context: %s\n", CGLErrorString(e)); return 2; }
    CGLSetCurrentContext(ctx);
    printf("pid=%d accelerated=%d renderer=%s version=%s\n", getpid(), accelerated,
           glGetString(GL_RENDERER), glGetString(GL_VERSION));
    printf("CGLContext bytes=%lu GLI dispatch bytes=%lu rend=%p clear=%p draw_arrays=%p\n",
           (unsigned long)sizeof(*ctx), (unsigned long)sizeof(ctx->disp),
           ctx->rend, ctx->disp.clear, ctx->disp.draw_arrays);
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const char *p = _dyld_get_image_name(i);
        if (strstr(p, "GLDriver") || strstr(p, "GLEngine") || strstr(p, "GLRenderer"))
            printf("image=%s\n", p);
    }
    fflush(stdout);
    const char *wait = getenv("VIM_GL_TRACE_WAIT");
    if (wait) sleep((unsigned)strtoul(wait, NULL, 10));

    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, tex, 0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT, "texture framebuffer");
    glViewport(0, 0, 64, 64);
    if (getenv("VIM_GL_USE_GLI")) {
        ctx->disp.clear_color(ctx->rend,0,0,1,1);
        ctx->disp.clear(ctx->rend,GL_COLOR_BUFFER_BIT);
        ctx->disp.begin(ctx->rend,GL_TRIANGLES);
        ctx->disp.color3f(ctx->rend,1,0,0); ctx->disp.vertex2f(ctx->rend,-1,-1);
        ctx->disp.color3f(ctx->rend,0,1,0); ctx->disp.vertex2f(ctx->rend,1,-1);
        ctx->disp.color3f(ctx->rend,1,1,0); ctx->disp.vertex2f(ctx->rend,0,1);
        ctx->disp.end(ctx->rend);
    } else {
        glClearColor(0, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0); glVertex2f(-1, -1);
        glColor3f(0, 1, 0); glVertex2f(1, -1);
        glColor3f(1, 1, 0); glVertex2f(0, 1);
        glEnd();
    }
    unsigned char pixel[4] = {0};
    glReadPixels(32, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("fixed pixel=%u,%u,%u,%u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    check(pixel[0] > 20 && pixel[1] > 20 && pixel[2] < 20, "fixed-function triangle readback");

    GLuint vs = shader(GL_VERTEX_SHADER, "#version 120\nvoid main(){gl_Position=gl_Vertex;}");
    GLuint fs = shader(GL_FRAGMENT_SHADER, "#version 120\nvoid main(){gl_FragColor=vec4(0.25,0.5,0.75,1.0);}");
    GLuint program = glCreateProgram();
    glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
    GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked); check(linked, "GLSL link");
    glUseProgram(program);
    glBegin(GL_TRIANGLES);
    glVertex2f(-1,-1); glVertex2f(1,-1); glVertex2f(0,1);
    glEnd();
    glReadPixels(32,24,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    printf("shader pixel=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    check(abs(pixel[0]-64)<=1 && abs(pixel[1]-128)<=1 && abs(pixel[2]-191)<=1, "GLSL triangle readback");
    glUseProgram(0);

    if (getenv("VIM_GL_APPLE_COMPLETION")) {
        GLuint fence=0;
        ctx->disp.gen_fences_APPLE(ctx->rend,1,&fence);
        ctx->disp.set_fence_APPLE(ctx->rend,fence);ctx->disp.finish_fence_APPLE(ctx->rend,fence);
        check(ctx->disp.is_fence_APPLE(ctx->rend,fence) && ctx->disp.test_fence_APPLE(ctx->rend,fence),"APPLE named fence completion");
        ctx->disp.delete_fences_APPLE(ctx->rend,1,&fence);
        ctx->disp.finish_object_APPLE(ctx->rend,GL_TEXTURE,tex);
        check(ctx->disp.test_object_APPLE(ctx->rend,GL_TEXTURE,tex),"APPLE texture object completion");
        check(glGetError()==GL_NO_ERROR,"APPLE completion error state");
    }

    CGLContextObj shared = NULL;
    e = CGLCreateContext(pf, ctx, &shared);
    check(e == kCGLNoError && shared, "shared context create");
    if (shared) {
        CGLSetCurrentContext(shared);
        check(glIsTexture(tex), "texture identity across shared contexts");
        CGLSetCurrentContext(ctx);
        CGLDestroyContext(shared);
    }

    if (!getenv("VIM_GL_SKIP_IOSURFACE")) {
    CFMutableDictionaryRef properties = CFDictionaryCreateMutable(NULL,0,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    number(properties,kIOSurfaceWidth,64); number(properties,kIOSurfaceHeight,64);
    number(properties,kIOSurfaceBytesPerElement,4); number(properties,kIOSurfaceBytesPerRow,256);
    number(properties,kIOSurfacePixelFormat,0x42475241); /* BGRA */
    IOSurfaceRef surface = IOSurfaceCreate(properties);
    CFRelease(properties);
    GLuint surfaceTex = 0;
    glGenTextures(1,&surfaceTex); glBindTexture(GL_TEXTURE_RECTANGLE_ARB,surfaceTex);
    e = surface ? CGLTexImageIOSurface2D(ctx,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,64,64,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0) : kCGLBadValue;
    printf("IOSurface import=%s\n",CGLErrorString(e));
    check(e==kCGLNoError,"IOSurface texture import");
    if (e==kCGLNoError) {
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB,surfaceTex,0);
        check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"IOSurface framebuffer");
        if(getenv("VIM_GL_IOSURFACE_DRAW_ONLY")) glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,0);
        GLint expected_read=0,expected_draw=0,expected_read_buffer=0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&expected_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&expected_draw);
        glGetIntegerv(GL_READ_BUFFER,&expected_read_buffer);
        glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
        if(getenv("VIM_GL_IOSURFACE_FLUSH_FENCE")) {
            GLuint fence=0;glGenFencesAPPLE(1,&fence);glSetFenceAPPLE(fence);glFlush();
            glFinishFenceAPPLE(fence);glDeleteFencesAPPLE(1,&fence);
        } else glFinish();
        GLint actual_read=0,actual_draw=0,actual_read_buffer=0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&actual_read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&actual_draw);
        glGetIntegerv(GL_READ_BUFFER,&actual_read_buffer);
        check(actual_read==expected_read && actual_draw==expected_draw &&
              actual_read_buffer==expected_read_buffer,"surface publication preserves framebuffer state");
        IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);
        const unsigned char *base=IOSurfaceGetBaseAddress(surface);
        check(base && base[0]==255 && base[1]==0 && base[2]==255 && base[3]==255,
              "GPU completion visible through IOSurface CPU mapping");
        IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
    }
    glDeleteTextures(1,&surfaceTex); if(surface) CFRelease(surface);
    } else { printf("SKIP IOSurface (explicit offscreen-only test)\n"); }
    check(glGetError()==GL_NO_ERROR,"final GL error");
    glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(fs);
    glDeleteFramebuffersEXT(1,&fbo); glDeleteTextures(1,&tex);
    CGLSetCurrentContext(NULL); CGLDestroyContext(ctx); CGLDestroyPixelFormat(pf);
    return failures ? 1 : 0;
}
