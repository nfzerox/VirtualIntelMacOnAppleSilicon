/* Adapted from LightClick (MIT), commiteb88b4c5.
 * Period Mac GLSL compilers took #extension anywhere. Today's rejects one after
 * any non-preprocessor token, so a shader that compiled on its own era's Mac
 * (VectorKit's Grid.frag) now fails. Move such directives to the top, the place
 * the compiler requires, and leave spaces behind so line numbers below the
 * inserted lines only shift by their count. */
#ifndef VIM_GLSL_LATE_EXTENSIONS_H
#define VIM_GLSL_LATE_EXTENSIONS_H
#include <stdlib.h>
#include <string.h>

/* Returns the new directive lines (free them) and blanks the old ones in
 * `source`; they go at offset *header, after any #version line. NULL means
 * nothing moved, or no memory, and `source` is unchanged.
 * shortcut: a late #extension inside #if stays put and still fails, since moving
 * it would drop its condition; hoist the whole block if an app ever needs that. */
static char *DGGLSLHoistLateExtensions(char *source, size_t length, size_t *header, size_t *moved_length) {
    int comment = 0, tokens = 0, depth = 0;
    char *moved = NULL;
    *header = *moved_length = 0;
    for (size_t at = 0, end; at < length; at = end + 1) {
        end = at;
        while (end < length && source[end] != '\n' && source[end] != '\r')
            ++end;
        size_t first = end, stop = end;
        for (size_t p = at; p < end;) {
            int opens = source[p] == '/' && p + 1 < end;
            if (comment) {
                if (source[p] == '*' && p + 1 < end && source[p + 1] == '/') {
                    comment = 0;
                    ++p;
                }
                ++p;
            } else if (opens && source[p + 1] == '/') {
                if (stop == end)
                    stop = p;
                break;
            } else if (opens && source[p + 1] == '*') {
                if (first < end && stop == end)
                    stop = p;
                comment = 1;
                p += 2;
            } else {
                if (first == end && source[p] != ' ' && source[p] != '\t' && source[p] != '\f' && source[p] != '\v')
                    first = p;
                ++p;
            }
        }
        if (first == end)
            continue;
        if (source[first] != '#') {
            tokens = 1;
            continue;
        }
        size_t name = first + 1;
        while (name < end && (source[name] == ' ' || source[name] == '\t'))
            ++name;
#define DG_DIRECTIVE(word) (end - name >= sizeof(word) - 1 && !memcmp(source + name, word, sizeof(word) - 1))
        if (DG_DIRECTIVE("if")) // #if, #ifdef, #ifndef
            ++depth;
        else if (DG_DIRECTIVE("endif"))
            --depth;
        else if (DG_DIRECTIVE("version") && !tokens && !depth)
            *header = end < length ? end + 1 : end;
        else if (DG_DIRECTIVE("extension") && tokens && !depth) {
            // Every moved line ends where its source line did, or at the end of the source: at most length + 1.
            if (!moved && !(moved = malloc(length + 1)))
                return NULL;
            memcpy(moved + *moved_length, source + first, stop - first);
            *moved_length += stop - first;
            moved[(*moved_length)++] = '\n';
            memset(source + first, ' ', stop - first);
        }
#undef DG_DIRECTIVE
    }
    return moved;
}
#endif
