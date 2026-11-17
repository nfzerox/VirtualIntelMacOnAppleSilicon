/*
 * Copyright (c) 2017 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include "ClosureFileSystemPhysical.h"
#include "GuestRoot.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using dyld3::closure::FileSystemPhysical;

bool FileSystemPhysical::getRealPath(const char possiblePath[MAXPATHLEN], char realPath[MAXPATHLEN]) const {
    char resolved[MAXPATHLEN];
    if (!guestRealpath(possiblePath, resolved))
        return false;
    const char* runtime = resolved + guestRoot.size();
    strlcpy(realPath, *runtime ? runtime : "/", MAXPATHLEN);
    return true;
}

bool FileSystemPhysical::loadFile(const char* path, LoadedFileInfo& info, char realerPath[MAXPATHLEN],
                                  void (^error)(const char* format, ...)) const {
    char resolved[MAXPATHLEN];
    if (!guestRealpath(path, resolved))
        return false;
    int fd = ::open(resolved, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno != ENOENT && errno != ENOTDIR)
            error("could not open %s: %s", path, strerror(errno));
        return false;
    }
    struct stat st;
    if (::fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 4096) {
        error("not a complete Mach-O file: %s", path);
        ::close(fd);
        return false;
    }
    void* mapping = ::mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    const int mapError = errno;
    ::close(fd);
    if (mapping == MAP_FAILED) {
        error("could not map %s: %s", path, strerror(mapError));
        return false;
    }
    info.fileContent = mapping;
    info.fileContentLen = st.st_size;
    info.hostMapping = mapping;
    info.hostMappingLen = st.st_size;
    info.sliceOffset = 0;
    info.sliceLen = st.st_size;
    info.inode = st.st_ino;
    info.mtime = st.st_mtime;
    info.path = path;
    const char* runtime = resolved + guestRoot.size();
    if (!strcmp(runtime, path))
        realerPath[0] = 0;
    else
        strlcpy(realerPath, runtime, MAXPATHLEN);
    info.unload = [](const LoadedFileInfo& owned) {
        ::munmap((void*)owned.hostMapping, (size_t)owned.hostMappingLen);
    };
    return true;
}

void FileSystemPhysical::unloadFile(const LoadedFileInfo& info) const {
    if (info.unload)
        info.unload(info);
}

void FileSystemPhysical::unloadPartialFile(LoadedFileInfo& info, uint64_t start, uint64_t length) const {
    // A guest slice may start at 4 KiB, not the host's 16 KiB boundary.
    // Keep the complete read-only mapping until unloadFile.
    info.fileContent = (const uint8_t*)info.fileContent + start;
    info.fileContentLen = length;
}

bool FileSystemPhysical::fileExists(const char* path, uint64_t* inode, uint64_t* mtime, bool* issetuid) const {
    char resolved[MAXPATHLEN];
    struct stat st;
    if (!guestRealpath(path, resolved) || ::stat(resolved, &st))
        return false;
    if (inode) *inode = st.st_ino;
    if (mtime) *mtime = st.st_mtime;
    if (issetuid) *issetuid = st.st_mode & (S_ISUID | S_ISGID);
    return true;
}
