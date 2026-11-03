/* Read-only host GL capability survey for the 10.8/10.9 graphics study.
 * Creates offscreen accelerated contexts; no VM, guest, drawing or file output.
 */
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>

static int has_extension(const char *list, const char *name)
{
    size_t n = strlen(name);
    const char *p = list;
    while (p && (p = strstr(p, name))) {
        if ((p == list || p[-1] == ' ') && (!p[n] || p[n] == ' ')) {
            return 1;
        }
        p += n;
    }
    return 0;
}

static int survey(CGLOpenGLProfile profile, const char *label)
{
    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)profile,
        kCGLPFAColorSize, 24, 0
    };
    CGLPixelFormatObj format = NULL;
    CGLContextObj context = NULL;
    GLint count = 0;
    CGLError error = CGLChoosePixelFormat(attrs, &format, &count);
    if (error || !format) {
        printf("%s unavailable: %s\n", label, CGLErrorString(error));
        return 1;
    }
    error = CGLCreateContext(format, NULL, &context);
    CGLDestroyPixelFormat(format);
    if (error) {
        printf("%s context failed: %s\n", label, CGLErrorString(error));
        return 1;
    }
    error = CGLSetCurrentContext(context);
    if (error) {
        printf("%s cannot become current: %s\n", label, CGLErrorString(error));
        CGLDestroyContext(context);
        return 1;
    }
    printf("%s renderer=%s version=%s\n", label,
           glGetString(GL_RENDERER), glGetString(GL_VERSION));
    if (profile == kCGLOGLPVersion_Legacy) {
        const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
        const char *names[] = {
            "GL_ARB_vertex_program", "GL_ARB_fragment_program",
            "GL_ARB_texture_rectangle", "GL_APPLE_client_storage",
            "GL_APPLE_texture_range", "GL_EXT_framebuffer_object",
            "GL_APPLE_fence"
        };
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            printf("  %s=%d\n", names[i], has_extension(extensions, names[i]));
        }
    }
    CGLSetCurrentContext(NULL);
    CGLDestroyContext(context);
    return 0;
}

int main(void)
{
    int result = survey(kCGLOGLPVersion_Legacy, "legacy");
    result |= survey(kCGLOGLPVersion_3_2_Core, "core");
    return result;
}
