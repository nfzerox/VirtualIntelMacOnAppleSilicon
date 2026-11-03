/* OpenGL 1.5/ARB buffer mapping, without a modern/core-profile dependency.
 * Run on a native Intel renderer or a period guest. All pixels/data are ours. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;
static void check(int good, const char *name)
{ printf("%s %s\n", good ? "PASS" : "FAIL", name); failures += !good; fflush(stdout); }

int main(void)
{
    CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, kCGLPFANoRecovery, 0};
    CGLPixelFormatObj format = NULL;
    CGLContextObj context = NULL;
    GLint count = 0;
    CGLError error = CGLChoosePixelFormat(attributes, &format, &count);
    if (!error && format) error = CGLCreateContext(format, NULL, &context);
    if (format) CGLDestroyPixelFormat(format);
    check(!error && context, "accelerated period context");
    if (!context) return 1;
    CGLSetCurrentContext(context);
    printf("renderer=%s version=%s pointer-bits=%u\n", glGetString(GL_RENDERER),
           glGetString(GL_VERSION), (unsigned)(sizeof(void *) * 8));
    GLuint buffer = 0;
    unsigned char seed[128], actual[128];
    for (unsigned i = 0; i < sizeof(seed); ++i) seed[i] = (unsigned char)i;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(seed), seed, GL_DYNAMIC_DRAW);
    unsigned char *mapped = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
    check(mapped && !glGetError(), "read-write buffer map");
    if (mapped) {
        GLint state = 0;
        void *pointer = NULL;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &state);
        glGetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &pointer);
        check(state && pointer == mapped && !glGetError(), "native mapped-state and guest pointer");
        check(!memcmp(mapped, seed, sizeof(seed)), "initial mapped bytes");
        mapped[17] = seed[17] = 0xd3;
        check(glUnmapBuffer(GL_ARRAY_BUFFER) && !glGetError(), "unmap publishes writes");
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(actual), actual);
        check(!memcmp(actual, seed, sizeof(seed)) && !glGetError(), "readback preserves modified and untouched bytes");
    }
    const GLfloat vertices[] = {-1, -1, 1, -1, 0, 1};
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), NULL, GL_DYNAMIC_DRAW);
    mapped = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    check(mapped && !glGetError(), "write-only vertex map");
    if (mapped) {
        memcpy(mapped, vertices, sizeof(vertices));
        check(glUnmapBuffer(GL_ARRAY_BUFFER) && !glGetError(), "vertex unmap");
        GLuint texture = 0, fbo = 0;
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glGenFramebuffersEXT(1, &fbo); glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, texture, 0);
        check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT,
              "pixel verification framebuffer");
        glViewport(0, 0, 16, 16); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glColor4f(1, 0, 1, 1); glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, NULL); glDrawArrays(GL_TRIANGLES, 0, 3);
        unsigned char pixel[4] = {0};
        glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(pixel[0] == 255 && !pixel[1] && pixel[2] == 255 && pixel[3] == 255 && !glGetError(),
              "GPU consumes mapped vertex bytes");
        glDisableClientState(GL_VERTEX_ARRAY); glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        glDeleteFramebuffersEXT(1, &fbo); glDeleteTextures(1, &texture);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0); glDeleteBuffers(1, &buffer);
    CGLSetCurrentContext(NULL); CGLDestroyContext(context);
    return failures ? 1 : 0;
}
