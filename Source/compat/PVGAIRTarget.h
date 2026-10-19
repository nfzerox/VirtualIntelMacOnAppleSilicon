#pragma once

// Match the native Metal parser's deployment target, not the donor compiler.
// Existing builds target Catalina; the isolated Mojave build targets 10.14.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
#define PVG_AIR_TARGET "air64-apple-macosx10.14.0"
#else
#define PVG_AIR_TARGET "air64-apple-macosx10.15.0"
#endif
