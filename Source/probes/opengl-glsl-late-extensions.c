/* Pure source rewrite checks; no renderer. Build: clang opengl-glsl-late-extensions.c && ./a.out */
#include "../PassthroughOpenGL/glsl-late-extensions.h"
#include <assert.h>
#include <stdio.h>
static void check(const char *input, const char *expected) {
    size_t length = strlen(input), header = 0, moved_length = 0;
    char *copy = malloc(length + 1);
    memcpy(copy, input, length + 1);
    char *moved = DGGLSLHoistLateExtensions(copy, length, &header, &moved_length);
    if (!expected) {
        assert(!moved && !strcmp(copy, input));
    } else {
        assert(moved);
        char joined[1024];
        snprintf(joined, sizeof(joined), "%.*s%.*s%s", (int)header, copy, (int)moved_length, moved, copy + header);
        if (strcmp(joined, expected)) {
            fprintf(stderr, "got:\n%s\nexpected:\n%s\n", joined, expected);
            assert(0);
        }
    }
    free(moved);
    free(copy);
}
int main(void) {
    // VectorKit's shape: tokens, even under #ifdef, then the directive.
    check("#define highp\n#ifdef GL_ES\nprecision mediump float;\n#endif\nuniform vec4 u;\n#extension A : enable\nvoid "
          "main() {}\n",
          "#extension A : enable\n#define highp\n#ifdef GL_ES\nprecision mediump float;\n#endif\nuniform vec4 u;\n     "
          "                \nvoid main() {}\n");
    check("#version 120\nuniform vec4 u;\n  # extension A : enable\n#extension B : enable",
          "#version 120\n# extension A : enable\n#extension B : enable\nuniform vec4 u;\n                        \n    "
          "                 ");
    // CRLF, and a trailing comment left behind, including one that spans lines.
    check("int a;\r\n#extension A : enable // why\r\nint b;\n",
          "#extension A : enable \nint a;\r\n                      // why\r\nint b;\n");
    check("int a;\n#extension A : enable /* why\nnot */\nint b;\n",
          "#extension A : enable \nint a;\n                      /* why\nnot */\nint b;\n");
    check("/* c */ int a;\n/* c */ #extension A : enable\n",
          "#extension A : enable\n/* c */ int a;\n/* c */                      \n");
    // Left alone: nothing late, in a comment, or conditional.
    check("#version 120\n// note\n#extension A : enable\nint a;\n", NULL);
    check("int a;\n// #extension A : enable\n/*\n#extension A : enable\n*/\n", NULL);
    check("int a; /* open\n#extension A : enable\n*/\n", NULL);
    check("int a;\n#ifdef X\n#extension A : enable\n#endif\n", NULL);
    check("", NULL);
    puts("PASS");
    return 0;
}
