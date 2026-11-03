/* Leopard's clean installer rebuilds /private/var/db, but preserves added
 * Library support files. Keep its root-only installation journal alongside
 * our support, not in a directory the native install transaction replaces.
 * Later, already-qualified guest journals retain their existing locations. */
#ifndef VIM_OPENGL_STATE_PATH_H
#define VIM_OPENGL_STATE_PATH_H
#if defined(VIM_MODERN_ACCOUNT_SETUP)
#define VIM_OPENGL_STATE_PATH @"/private/var/db/VirtualIntelMac/AutomaticSetup"
#elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#define VIM_OPENGL_STATE_PATH @"/Library/Application Support/Virtual Intel Mac/State/LegacySetup"
#else
#define VIM_OPENGL_STATE_PATH @"/private/var/db/VirtualIntelMac/LegacySetup"
#endif
#endif
