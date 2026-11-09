/* Build-only context selection for the public diagnostic batch.
 * Read CGL's declarations before defining the fixture-only hint. Legacy GL
 * does not need the 10.8 automatic-graphics-switching pixel format key;
 * terminate at that point instead of rejecting a valid older renderer.
 * No OpenGL calls, expected pixels, or extension checks are substituted.
 */
#include <OpenGL/OpenGL.h>
#define kCGLPFASupportsAutomaticGraphicsSwitching 0
#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
/* The offline-renderer chooser hint also postdates Tiger. Keep accelerated /
 * no-recovery requirements; do not fail before reaching the actual GL tests. */
#define kCGLPFAAllowOfflineRenderers 0
#endif
#define VIM_PERIOD_COMPAT_ONLY 1
