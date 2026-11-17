/* Adapted from LightClick (MIT), commit52d12048.
 * A new doublebuffer starts from the attached CPU backing, before client writes.
 * Later imports must preserve the independently rendered BACK contents. */
#ifndef VIM_INITIAL_BACKING_POLICY_H
#define VIM_INITIAL_BACKING_POLICY_H
#include <stdint.h>
#include <string.h>

struct VIMInitialBackingState {
    unsigned char attempted, back_exposed;
};
/* Rollback/A-B control: only the exact value 0 disables initialization. */
static inline int VIMInitialBackingEnabled(const char *value) {
    return !value || strcmp(value, "0");
}
static inline int VIMInitialBackingClaim(int enabled, struct VIMInitialBackingState *state,
                                        int doubled, uintptr_t front, uintptr_t back,
                                        unsigned front_texture, unsigned back_texture,
                                        int width, int height, uint64_t serial,
                                        uint64_t x, uint64_t y, uint64_t imported_width, uint64_t imported_height) {
    if (!enabled || !doubled || !front || !back || front == back || !front_texture || !back_texture ||
        front_texture == back_texture || width < 1 || height < 1 || x || y ||
        imported_width != (uint64_t)width || imported_height != (uint64_t)height || serial ||
        state->attempted || state->back_exposed)
        return 0;
    state->attempted = 1; // Consume even if the import or BACK initialization subsequently fails.
    return 1;
}
static inline void VIMInitialBackingExpose(int enabled, struct VIMInitialBackingState *state) {
    if (enabled)
        state->back_exposed = 1;
}
#endif
