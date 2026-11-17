#pragma once

// Publish a finished, private VM directory on the same filesystem. The
// destination must be new; an existing empty directory is a collision too.
// Returns 0 or -1 with errno. On failure the prepared directory remains intact.
int VIMPublishPreparedPackage(const char *prepared, const char *destination);
