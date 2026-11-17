/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
 *
 * Copyright (c) 2014 Apple Inc. All rights reserved.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <dirent.h>
#include <dispatch/dispatch.h>
#include <pthread/pthread.h>
#include <CoreFoundation/CoreFoundation.h>
#include "HostBOM.h"
#include "GuestRoot.hpp"

#include <algorithm>
#include <vector>
#include <unordered_set>
#include <unordered_set>
#include <iostream>
#include <fstream>

#include "FileUtils.h"
#include "StringUtils.h"
#include "DyldSharedCache.h"
#include "MachOFile.h"
#include "MachOAnalyzer.h"
#include "ClosureFileSystemPhysical.h"

struct MappedMachOsByCategory
{
    std::string                                 archName;
    std::vector<DyldSharedCache::MappedMachO>   dylibsForCache;
    std::vector<DyldSharedCache::MappedMachO>   otherDylibsAndBundles;
    std::vector<DyldSharedCache::MappedMachO>   mainExecutables;
};

static const char* sAllowedPrefixes[] = {
    "/bin/",
    "/sbin/",
    "/usr/",
    "/System",
    "/Applications/App Store.app/",
    "/Applications/Automator.app/",
    "/Applications/Calculator.app/",
    "/Applications/Calendar.app/",
    "/Applications/Chess.app/",
    "/Applications/Contacts.app/",
//    "/Applications/DVD Player.app/",
    "/Applications/Dashboard.app/",
    "/Applications/Dictionary.app/",
    "/Applications/FaceTime.app/",
    "/Applications/Font Book.app/",
    "/Applications/Image Capture.app/",
    "/Applications/Launchpad.app/",
    "/Applications/Mail.app/",
    "/Applications/Maps.app/",
    "/Applications/Messages.app/",
    "/Applications/Mission Control.app/",
    "/Applications/Notes.app/",
    "/Applications/Photo Booth.app/",
//    "/Applications/Photos.app/",
    "/Applications/Preview.app/",
    "/Applications/QuickTime Player.app/",
    "/Applications/Reminders.app/",
    "/Applications/Safari.app/",
    "/Applications/Siri.app/",
    "/Applications/Stickies.app/",
    "/Applications/System Preferences.app/",
    "/Applications/TextEdit.app/",
    "/Applications/Time Machine.app/",
    "/Applications/iBooks.app/",
    "/Applications/iTunes.app/",
    "/Applications/Utilities/Activity Monitor.app",
    "/Applications/Utilities/AirPort Utility.app",
    "/Applications/Utilities/Audio MIDI Setup.app",
    "/Applications/Utilities/Bluetooth File Exchange.app",
    "/Applications/Utilities/Boot Camp Assistant.app",
    "/Applications/Utilities/ColorSync Utility.app",
    "/Applications/Utilities/Console.app",
    "/Applications/Utilities/Digital Color Meter.app",
    "/Applications/Utilities/Disk Utility.app",
    "/Applications/Utilities/Grab.app",
    "/Applications/Utilities/Grapher.app",
    "/Applications/Utilities/Keychain Access.app",
    "/Applications/Utilities/Migration Assistant.app",
    "/Applications/Utilities/Script Editor.app",
    "/Applications/Utilities/System Information.app",
    "/Applications/Utilities/Terminal.app",
    "/Applications/Utilities/VoiceOver Utility.app",
    "/Library/CoreMediaIO/Plug-Ins/DAL/"                // temp until plugins moved or closured working
};

static const char* sDontUsePrefixes[] = {
    "/usr/share",
    "/usr/local/",
    "/System/Library/Assets",
    "/System/Library/StagedFrameworks",
    "/System/Library/Kernels/",
    "/bin/zsh",                             // until <rdar://31026756> is fixed
    "/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/Metadata.framework/Versions/A/Support/mdworker", // these load third party plugins
    "/usr/bin/mdimport", // these load third party plugins
    "/System/Library/Developer/CoreSimulator/",  // ignore Marzipan
};


static bool verbose = false;
static HostBOM hostBOM{};

static bool guestFileInfo(const std::string& path, struct stat& info) {
    char resolved[PATH_MAX];
    return guestRealpath(path.c_str(), resolved) && ::stat(resolved, &info) == 0;
}



static bool addIfMachO(const dyld3::closure::FileSystem& fileSystem, const std::string& runtimePath, const struct stat& statBuf, bool requireSIP,
                       dev_t rootFS, std::vector<MappedMachOsByCategory>& files)
{
    // don't precompute closure info for any debug or profile dylibs
    if ( endsWith(runtimePath, "_profile.dylib") || endsWith(runtimePath, "_debug.dylib") || endsWith(runtimePath, "_profile") || endsWith(runtimePath, "_debug") )
        return false;
    if ( startsWith(runtimePath, "/usr/lib/system/introspection/") )
        return false;

    // Only use files on the same volume as the boot volume
    if (statBuf.st_dev != rootFS) {
        if ( verbose )
            fprintf(stderr, "update_dyld_shared_cache: warning: skipping overlay file '%s' which is not on the root volume\n", runtimePath.c_str());
        return false;
    }

    auto warningHandler = ^(const char* msg) {
        if ( verbose )
            fprintf(stderr, "update_dyld_shared_cache: warning: cannot build dlopen closure for '%s' because %s\n", runtimePath.c_str(), msg);
    };

    bool result = false;
    for (MappedMachOsByCategory& file : files) {
        Diagnostics diag;
        dyld3::closure::LoadedFileInfo loadedFileInfo = dyld3::MachOAnalyzer::load(diag, fileSystem, runtimePath.c_str(), file.archName.c_str(), dyld3::Platform::macOS);
        const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)loadedFileInfo.fileContent;
        if ( ma != nullptr ) {
            bool sipProtected = false; // isProtectedBySIP(fd);
            bool issetuid     = false;
            if ( ma->isDynamicExecutable() ) {
                // When SIP enabled, only build closures for SIP protected programs
                if ( !requireSIP || sipProtected ) {
                    //fprintf(stderr, "requireSIP=%d, sipProtected=%d, path=%s\n", requireSIP, sipProtected, fullPath.c_str());
                    issetuid = (statBuf.st_mode & (S_ISUID|S_ISGID));
                    file.mainExecutables.emplace_back(runtimePath, ma, loadedFileInfo.sliceLen, issetuid, sipProtected, loadedFileInfo.sliceOffset, statBuf.st_mtime, statBuf.st_ino);
                }
            }
            else if ( ma->canBePlacedInDyldCache(runtimePath.c_str(), ^(const char* msg) {}) ) {
                // when SIP is enabled, only dylib protected by SIP can go in cache
                if ( !requireSIP || sipProtected )
                    file.dylibsForCache.emplace_back(runtimePath, ma, loadedFileInfo.sliceLen, issetuid, sipProtected, loadedFileInfo.sliceOffset, statBuf.st_mtime, statBuf.st_ino);
                else if ( ma->canHavePrecomputedDlopenClosure(runtimePath.c_str(), warningHandler) )
                    file.otherDylibsAndBundles.emplace_back(runtimePath, ma, loadedFileInfo.sliceLen, issetuid, sipProtected, loadedFileInfo.sliceOffset, statBuf.st_mtime, statBuf.st_ino);
            }
            else {
                if ( ma->isDylib() ) {
                    std::string installName = ma->installName();
                    if ( startsWith(installName, "@") && !contains(runtimePath, ".app/") && !contains(runtimePath, ".xpc/") ) {
                        if ( startsWith(runtimePath, "/usr/lib/") || startsWith(runtimePath, "/System/Library/") )
                            fprintf(stderr, "update_dyld_shared_cache: warning @rpath install name for system framework: %s\n", runtimePath.c_str());
                    }
                }
                else if ( ma->canHavePrecomputedDlopenClosure(runtimePath.c_str(), warningHandler) ) {
                    file.otherDylibsAndBundles.emplace_back(runtimePath, ma, loadedFileInfo.sliceLen, issetuid, sipProtected, loadedFileInfo.sliceOffset, statBuf.st_mtime, statBuf.st_ino);
                }
            }
            result = true;
        }
    }

    return result;
}

static void findAllFiles(const std::vector<std::string>& pathPrefixes, bool requireSIP, dev_t rootFS, std::vector<MappedMachOsByCategory>& files)
{
    std::unordered_set<std::string> skipDirs;
    for (const char* s : sDontUsePrefixes)
        skipDirs.insert(s);

    __block std::unordered_set<std::string> alreadyUsed;
    bool multiplePrefixes = (pathPrefixes.size() > 1);
    for (const std::string& prefix : pathPrefixes) {
        // get all files from overlay for this search dir
        dyld3::closure::FileSystemPhysical fileSystem(prefix.c_str());
        for (const char* searchDir : sAllowedPrefixes ) {
            iterateDirectoryTree(prefix, searchDir, ^(const std::string& dirPath) { return (skipDirs.count(dirPath) != 0); }, ^(const std::string& path, const struct stat& statBuf) {
                // ignore files that don't have 'x' bit set (all runnable mach-o files do)
                const bool hasXBit = ((statBuf.st_mode & S_IXOTH) == S_IXOTH);
                if ( !hasXBit && !endsWith(path, ".dylib") )
                    return;

                // ignore files too small
                if ( statBuf.st_size < 0x3000 )
                    return;

                // don't add paths already found using previous prefix
                if ( multiplePrefixes && (alreadyUsed.count(path) != 0) )
                    return;

                // if the file is mach-o, add to list
                if ( addIfMachO(fileSystem, path, statBuf, requireSIP, rootFS, files) ) {
                    if ( multiplePrefixes )
                        alreadyUsed.insert(path);
                }
            });
        }
    }
}


static void findOSFilesViaBOMS(const std::vector<std::string>& pathPrefixes, bool requireSIP, dev_t rootFS, std::vector<MappedMachOsByCategory>& files)
{
    __block std::unordered_set<std::string> runtimePathsFound;
    for (const std::string& prefix : pathPrefixes) {
        iterateDirectoryTree(prefix, "/System/Library/Receipts", ^(const std::string&) { return false; }, ^(const std::string& path, const struct stat& statBuf) {
            if ( !contains(path, "com.apple.pkg.") )
                return;
            if ( !endsWith(path, ".bom") )
                return;
            std::string fullPath = prefix + path;
            BOMBom bom = hostBOM.open(fullPath.c_str(), false, nullptr);
            if ( bom == nullptr )
                return;
            BOMFSObject rootFso = hostBOM.root(bom);
            if ( rootFso == nullptr ) {
                hostBOM.close(bom);
                return;
            }
            BOMBomEnumerator e = hostBOM.begin(bom, rootFso);
            hostBOM.freeObject(rootFso);
            if ( e == nullptr ) {
                fprintf(stderr, "Can't get enumerator for BOM root FSObject\n");
                hostBOM.close(bom);
                return;
            }
            //fprintf(stderr, "using BOM %s\n", path.c_str());
            while (BOMFSObject fso = hostBOM.next(e)) {
                if ( hostBOM.isBinary(fso) ) {
                    const char* runPath = hostBOM.name(fso);
                    if ( (runPath[0] == '.') && (runPath[1] == '/') )
                        ++runPath;
                    if ( runtimePathsFound.count(runPath) == 0 ) {
                        // only add files from sAllowedPrefixes and not in sDontUsePrefixes
                        bool inSearchDir = false;
                        for (const char* searchDir : sAllowedPrefixes ) {
                            if ( strncmp(searchDir, runPath, strlen(searchDir)) == 0 )  {
                                inSearchDir = true;
                                break;
                            }
                        }
                        if ( inSearchDir ) {
                            bool inSkipDir = false;
                            for (const char* skipDir : sDontUsePrefixes) {
                                if ( strncmp(skipDir, runPath, strlen(skipDir)) == 0 )  {
                                    inSkipDir = true;
                                    break;
                                }
                            }
                            if ( !inSkipDir ) {
                                for (const std::string& prefix2 : pathPrefixes) {
                                    struct stat statBuf2;
                                    std::string fullPath2 = prefix2 + runPath;
                                    if ( guestFileInfo(fullPath2, statBuf2) ) {
                                        dyld3::closure::FileSystemPhysical fileSystem(prefix2.c_str());
                                        if ( addIfMachO(fileSystem, runPath, statBuf2, requireSIP, rootFS, files) ) {
                                            runtimePathsFound.insert(runPath);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                hostBOM.freeObject(fso);
            }

            hostBOM.end(e);
            hostBOM.close(bom);
        });
    }
}


static bool dontCache(const std::string& volumePrefix, const std::string& archName,
                      const std::unordered_set<std::string>& pathsWithDuplicateInstallName,
                      const DyldSharedCache::MappedMachO& aFile, bool warn,
                      const std::unordered_set<std::string>& skipDylibs)
{
    if ( skipDylibs.count(aFile.runtimePath) )
        return true;
    if ( startsWith(aFile.runtimePath, "/usr/lib/system/introspection/") )
        return true;
    if ( startsWith(aFile.runtimePath, "/System/Library/QuickTime/") )
        return true;
    if ( startsWith(aFile.runtimePath, "/System/Library/Tcl/") )
        return true;
    if ( startsWith(aFile.runtimePath, "/System/Library/Perl/") )
        return true;
    if ( startsWith(aFile.runtimePath, "/System/Library/MonitorPanels/") )
        return true;
    if ( startsWith(aFile.runtimePath, "/System/Library/Accessibility/") )
        return true;
    if ( startsWith(aFile.runtimePath, "/usr/local/") )
        return true;

    // anything inside a .app bundle is specific to app, so should not be in shared cache
    if ( aFile.runtimePath.find(".app/") != std::string::npos )
        return true;

    if ( archName == "i386" ) {
        if ( startsWith(aFile.runtimePath, "/System/Library/CoreServices/") )
            return true;
        if ( startsWith(aFile.runtimePath, "/System/Library/Extensions/") )
            return true;
    }

    if ( aFile.runtimePath.find("//") != std::string::npos ) {
        if (warn) fprintf(stderr, "update_dyld_shared_cache: warning: %s skipping because of bad install name %s\n", archName.c_str(), aFile.runtimePath.c_str());
        return true;
    }

    const char* installName = aFile.mh->installName();
    if ( (pathsWithDuplicateInstallName.count(aFile.runtimePath) != 0) && (aFile.runtimePath != installName) ) {
        // <rdar://problem/46431467> if a dylib moves and a symlink is installed into its place, bom iterator will see both and issue a warning
        struct stat statBuf;
        bool isSymLink = ( (lstat((volumePrefix + aFile.runtimePath).c_str(), &statBuf) == 0) && S_ISLNK(statBuf.st_mode) );
        if (!isSymLink && warn) fprintf(stderr, "update_dyld_shared_cache: warning: %s skipping because of duplicate install name %s\n", archName.c_str(), aFile.runtimePath.c_str());
        return true;
    }

    if ( aFile.runtimePath != installName ) {
        // see if install name is a symlink to actual path
        std::string fullInstall = volumePrefix + installName;
        char resolvedPath[PATH_MAX];
        if ( guestRealpath(fullInstall.c_str(), resolvedPath) != NULL ) {
            std::string resolvedSymlink = resolvedPath;
            if ( !volumePrefix.empty() ) {
                resolvedSymlink = resolvedSymlink.substr(volumePrefix.size());
            }
            if ( aFile.runtimePath == resolvedSymlink ) {
                return false;
            }
        }
        // <rdar://problem/38000411> also if runtime path is a symlink to install name
        std::string fullRuntime = volumePrefix + aFile.runtimePath;
        if ( guestRealpath(fullRuntime.c_str(), resolvedPath) != NULL ) {
            std::string resolvedSymlink = resolvedPath;
            if ( !volumePrefix.empty() ) {
                resolvedSymlink = resolvedSymlink.substr(volumePrefix.size());
            }
            if ( resolvedSymlink == installName ) {
                return false;
            }
        }
        if (warn) fprintf(stderr, "update_dyld_shared_cache: warning: %s skipping because of bad install name %s\n", archName.c_str(), aFile.runtimePath.c_str());
        return true;
    }
    return false;
}

static void pruneCachedDylibs(const std::string& volumePrefix, const std::unordered_set<std::string>& skipDylibs, MappedMachOsByCategory& fileSet)
{
    std::unordered_set<std::string> pathsWithDuplicateInstallName;

    std::unordered_map<std::string, std::string> installNameToFirstPath;
    for (DyldSharedCache::MappedMachO& aFile : fileSet.dylibsForCache) {
        const char* installName = aFile.mh->installName();
        auto pos = installNameToFirstPath.find(installName);
        if ( pos == installNameToFirstPath.end() ) {
            installNameToFirstPath[installName] = aFile.runtimePath;
        }
        else {
            pathsWithDuplicateInstallName.insert(aFile.runtimePath);
            pathsWithDuplicateInstallName.insert(installNameToFirstPath[installName]);
        }
    }

    for (DyldSharedCache::MappedMachO& aFile : fileSet.dylibsForCache) {
        if ( dontCache(volumePrefix, fileSet.archName, pathsWithDuplicateInstallName, aFile, true, skipDylibs) ) {
            // <rdar://problem/46423929> don't build dlopen closures for symlinks to something in the dyld cache
            if ( pathsWithDuplicateInstallName.count(aFile.runtimePath) == 0 )
                fileSet.otherDylibsAndBundles.push_back(aFile);
        }
    }
    fileSet.dylibsForCache.erase(std::remove_if(fileSet.dylibsForCache.begin(), fileSet.dylibsForCache.end(),
        [&](const DyldSharedCache::MappedMachO& aFile) { return dontCache(volumePrefix, fileSet.archName, pathsWithDuplicateInstallName, aFile, false, skipDylibs); }),
        fileSet.dylibsForCache.end());
}

static void pruneOtherDylibs(const std::string& volumePrefix, MappedMachOsByCategory& fileSet)
{
    // other OS dylibs should not contain dylibs that are embedded in some .app bundle
    fileSet.otherDylibsAndBundles.erase(std::remove_if(fileSet.otherDylibsAndBundles.begin(), fileSet.otherDylibsAndBundles.end(),
                                                       [&](const DyldSharedCache::MappedMachO& aFile) { return (aFile.runtimePath.find(".app/") != std::string::npos); }),
                                        fileSet.otherDylibsAndBundles.end());
}
static void pruneExecutables(const std::string& volumePrefix, MappedMachOsByCategory& fileSet)
{
    // don't build closures for xcode shims in /usr/bin (e.g. /usr/bin/clang) which re-exec themselves to a tool inside Xcode.app
    fileSet.mainExecutables.erase(std::remove_if(fileSet.mainExecutables.begin(), fileSet.mainExecutables.end(),
        [&](const DyldSharedCache::MappedMachO& aFile) {
            if ( !startsWith(aFile.runtimePath, "/usr/bin/") )
                return false;
            __block bool isXcodeShim = false;
            aFile.mh->forEachDependentDylib(^(const char* loadPath, bool, bool, bool, uint32_t, uint32_t, bool &stop) {
                if ( strcmp(loadPath, "/usr/lib/libxcselect.dylib") == 0 )
                    isXcodeShim = true;
            });
            return isXcodeShim;
        }), fileSet.mainExecutables.end());
}

static bool existingCacheUpToDate(const std::string& existingCache, const std::vector<DyldSharedCache::MappedMachO>& currentDylibs)
{
    // if no existing cache, it is not up-to-date
    int fd = ::open(existingCache.c_str(), O_RDONLY);
    if ( fd < 0 )
        return false;

    // build map of found dylibs
    std::unordered_map<std::string, const DyldSharedCache::MappedMachO*> currentDylibMap;
    for (const DyldSharedCache::MappedMachO& aFile : currentDylibs) {
        //fprintf(stderr, "0x%0llX 0x%0llX  %s\n", aFile.inode, aFile.modTime, aFile.runtimePath.c_str());
        currentDylibMap[aFile.runtimePath] = &aFile;
    }

    // make sure all dylibs in existing cache have same mtime and inode as found dylib
    __block bool foundMismatch = false;
    const uint64_t cacheMapLen = 0x40000000;
    void *p = ::mmap(NULL, cacheMapLen, PROT_READ, MAP_PRIVATE, fd, 0);
    if ( p != MAP_FAILED ) {
        const DyldSharedCache* cache = (DyldSharedCache*)p;
        cache->forEachImageEntry(^(const char* installName, uint64_t mTime, uint64_t inode) {
            bool foundMatch = false;
            auto pos = currentDylibMap.find(installName);
            if ( pos != currentDylibMap.end() ) {
                const DyldSharedCache::MappedMachO* foundDylib = pos->second;
                if ( (foundDylib->inode == inode) && (foundDylib->modTime == mTime) ) {
                    foundMatch = true;
                }
            }
            if ( !foundMatch ) {
                // use slow path and look for any dylib with a matching inode and mtime
                bool foundSlow = false;
                for (const DyldSharedCache::MappedMachO& aFile : currentDylibs) {
                    if ( (aFile.inode == inode) && (aFile.modTime == mTime) ) {
                        foundSlow = true;
                        break;
                    }
                }
                if ( !foundSlow ) {
                    foundMismatch = true;
                    if ( verbose )
                        fprintf(stderr, "rebuilding dyld cache because dylib changed: %s\n", installName);
                }
            }
         });
        ::munmap(p, cacheMapLen);
    }

    ::close(fd);

    return !foundMismatch;
}


inline uint32_t absolutetime_to_milliseconds(uint64_t abstime)
{
    return (uint32_t)(abstime/1000/1000);
}

static bool runningOnHaswell()
{
    // check system is capable of running x86_64h code
    struct host_basic_info  info;
    mach_msg_type_number_t  count    = HOST_BASIC_INFO_COUNT;
    mach_port_t             hostPort = mach_host_self();
    kern_return_t           result   = host_info(hostPort, HOST_BASIC_INFO, (host_info_t)&info, &count);
    mach_port_deallocate(mach_task_self(), hostPort);

    return ( (result == KERN_SUCCESS) && (info.cpu_subtype == CPU_SUBTYPE_X86_64_H) );
}



#define TERMINATE_IF_LAST_ARG( s )      \
    do {                                \
        if ( i == argc - 1 ) {          \
            fprintf(stderr, s );        \
            return 1;                   \
        }                               \
    } while ( 0 )

int main(int argc, const char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: mojave-cache /guest-root /guest-cache-directory\n");
        return 2;
    }
    char root[PATH_MAX], output[PATH_MAX];
    struct stat local, system, destination;
    if (!realpath(argv[1], root) || !realpath(argv[2], output) ||
        !strcmp(root, "/") || strncmp(root, output, strlen(root)) ||
        output[strlen(root)] != '/' ||
        stat(root, &local) || stat("/", &system) || stat(output, &destination) ||
        !S_ISDIR(local.st_mode) || !S_ISDIR(destination.st_mode) ||
        local.st_dev == system.st_dev || destination.st_dev != local.st_dev) {
        fprintf(stderr, "use a separate guest volume and its cache directory\n");
        return 2;
    }
    if (!hostBOM.load()) {
        fprintf(stderr, "the host receipt reader is unavailable\n");
        return 1;
    }
    guestRoot = root;
    const std::string rootPath = root, cacheDir = output;
    const std::vector<std::string> pathPrefixes{rootPath};
    const std::unordered_set<std::string> archStrs{"i386", "x86_64"};
    const std::unordered_set<std::string> skipDylibs;
    const dev_t rootFS = local.st_dev;
    const bool force = true, searchDisk = false, dylibsRemoved = false;

    uint64_t t1 = mach_absolute_time();

    // find all mach-o files for requested architectures
    bool requireDylibsBeRootlessProtected = isProtectedBySIP(cacheDir);
    __block std::vector<MappedMachOsByCategory> allFileSets;
    if ( archStrs.count("x86_64") )
        allFileSets.push_back({"x86_64"});
    if ( archStrs.count("x86_64h") )
        allFileSets.push_back({"x86_64h"});
    if ( archStrs.count("i386") )
        allFileSets.push_back({"i386"});
    if ( searchDisk )
        findAllFiles(pathPrefixes, requireDylibsBeRootlessProtected, rootFS, allFileSets);
    else {
        std::unordered_set<std::string> runtimePathsFound;
        findOSFilesViaBOMS(pathPrefixes, requireDylibsBeRootlessProtected, rootFS, allFileSets);
    }

    // nothing in OS uses i386 dylibs, so only dylibs used by third party apps need to be in cache
    for (MappedMachOsByCategory& fileSet : allFileSets) {
        pruneCachedDylibs(rootPath, skipDylibs, fileSet);
        pruneOtherDylibs(rootPath, fileSet);
        pruneExecutables(rootPath, fileSet);
   }

    uint64_t t2 = mach_absolute_time();
    if ( verbose ) {
        if ( searchDisk )
            fprintf(stderr, "time to scan file system and construct lists of mach-o files: %ums\n", absolutetime_to_milliseconds(t2-t1));
        else
            fprintf(stderr, "time to read BOM and construct lists of mach-o files: %ums\n", absolutetime_to_milliseconds(t2-t1));
    }

    // build caches in parallel on machines with at leat 4GB of RAM
    uint64_t memSize = 0;
    size_t sz = sizeof(memSize);;
    bool buildInParallel = false;
    if ( sysctlbyname("hw.memsize", &memSize, &sz, NULL, 0) == 0 ) {
        if ( memSize >= 0x100000000ULL )
            buildInParallel = true;
    }
    dispatch_queue_t dqueue = buildInParallel ? dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0)
                                              : dispatch_queue_create("serial-queue", DISPATCH_QUEUE_SERIAL);

    // build all caches
    __block bool cacheBuildFailure = false;
    dispatch_apply(allFileSets.size(), dqueue, ^(size_t index) {
        MappedMachOsByCategory& fileSet = allFileSets[index];
        const std::string outFile = cacheDir + "/dyld_shared_cache_" + fileSet.archName;

        DyldSharedCache::MappedMachO (^loader)(const std::string&) = ^DyldSharedCache::MappedMachO(const std::string& runtimePath) {
            if ( skipDylibs.count(runtimePath) )
                return DyldSharedCache::MappedMachO();
            for (const std::string& prefix : pathPrefixes) {
                std::string fullPath = prefix + runtimePath;
                struct stat statBuf;
                if ( guestFileInfo(fullPath, statBuf) ) {
                    dyld3::closure::FileSystemPhysical fileSystem(prefix.c_str());
                    char resolvedPath[PATH_MAX];
                    if ( guestRealpath(fullPath.c_str(), resolvedPath) != NULL ) {
                        std::string resolvedSymlink = resolvedPath;
                        if ( !rootPath.empty() ) {
                            resolvedSymlink = resolvedSymlink.substr(rootPath.size());
                        }
                        if ( (runtimePath != resolvedSymlink) && !contains(runtimePath, "InputContext") ) {  //HACK remove InputContext when fixed
                           // path requested is a symlink path, check if real path already loaded
                            for (const DyldSharedCache::MappedMachO& aDylibMapping : fileSet.dylibsForCache) {
                                if ( aDylibMapping.runtimePath == resolvedSymlink ) {
                                    if ( verbose )
                                        fprintf(stderr, "verifySelfContained, redirect %s to %s\n", runtimePath.c_str(), aDylibMapping.runtimePath.c_str());
                                    return aDylibMapping;
                                }
                            }
                        }
                    }

                    std::vector<MappedMachOsByCategory> mappedFiles;
                    mappedFiles.push_back({fileSet.archName});
                   if ( addIfMachO(fileSystem, runtimePath, statBuf, requireDylibsBeRootlessProtected, rootFS, mappedFiles) ) {
                        if ( !mappedFiles.back().dylibsForCache.empty() ) {
                            if ( verbose )
                                fprintf(stderr, "verifySelfContained, add %s\n", mappedFiles.back().dylibsForCache.back().runtimePath.c_str());
                            return mappedFiles.back().dylibsForCache.back();
                        }
                    }
                }
            }
            return DyldSharedCache::MappedMachO();
        };
        size_t startCount = fileSet.dylibsForCache.size();
        std::vector<std::pair<DyldSharedCache::MappedMachO, std::set<std::string>>> excludes;
        DyldSharedCache::verifySelfContained(fileSet.dylibsForCache, loader, excludes);
        for (size_t i=startCount; i < fileSet.dylibsForCache.size(); ++i) {
            fprintf(stderr, "update_dyld_shared_cache: warning: %s not in .bom, but adding required dylib %s\n", fileSet.archName.c_str(), fileSet.dylibsForCache[i].runtimePath.c_str());
        }
        for (auto& exclude : excludes) {
            std::string reasons = "(\"";
            for (auto i = exclude.second.begin(); i != exclude.second.end(); ++i) {
                reasons += *i;
                if (i != --exclude.second.end()) {
                    reasons += "\", \"";
                }
            }
            reasons += "\")";
            fprintf(stderr, "update_dyld_shared_cache: warning: %s rejected from cached dylibs: %s (%s)\n", fileSet.archName.c_str(), exclude.first.runtimePath.c_str(), reasons.c_str());
            fileSet.otherDylibsAndBundles.push_back(exclude.first);
        }

        // check if cache is already up to date
        if ( !force ) {
            if ( existingCacheUpToDate(outFile, fileSet.dylibsForCache) )
                return;
        }

         // add any extra dylibs needed which were not in .bom
        fprintf(stderr, "update_dyld_shared_cache: %s incorporating %lu OS dylibs, tracking %lu others, building closures for %lu executables\n",
                        fileSet.archName.c_str(), fileSet.dylibsForCache.size(), fileSet.otherDylibsAndBundles.size(), fileSet.mainExecutables.size());
        //for (const DyldSharedCache::MappedMachO& aFile : fileSet.otherDylibsAndBundles) {
        //    fprintf(stderr, "  %s\n", aFile.runtimePath.c_str());
        //}


        // build cache new cache file
        DyldSharedCache::CreateOptions options;
        options.outputFilePath               = outFile;
        options.outputMapFilePath            = cacheDir + "/dyld_shared_cache_" + fileSet.archName + ".map";
        options.archName                     = fileSet.archName;
        options.platform                     = dyld3::Platform::macOS;
        options.excludeLocalSymbols          = false;
        options.optimizeStubs                = false;
        options.optimizeObjC                 = true;
        options.codeSigningDigestMode        = DyldSharedCache::SHA256only;
        options.dylibsRemovedDuringMastering = dylibsRemoved;
        options.inodesAreSameAsRuntime       = true;
        options.cacheSupportsASLR            = (fileSet.archName != "i386");
        options.forSimulator                 = false;
        options.isLocallyBuiltCache          = true;
        options.verbose                      = verbose;
        options.evictLeafDylibsOnOverflow    = true;
        options.pathPrefixes                 = pathPrefixes;
        DyldSharedCache::CreateResults results = DyldSharedCache::create(options, fileSet.dylibsForCache, fileSet.otherDylibsAndBundles, fileSet.mainExecutables);

        // print any warnings
        for (const std::string& warn : results.warnings) {
            fprintf(stderr, "update_dyld_shared_cache: warning: %s %s\n", fileSet.archName.c_str(), warn.c_str());
        }
        if ( results.errorMessage.empty() ) {
        }
        else {
            fprintf(stderr, "update_dyld_shared_cache: %s\n", results.errorMessage.c_str());
            cacheBuildFailure = true;
        }
    });


    // we could unmap all input files, but tool is about to quit

    return (cacheBuildFailure ? 1 : 0);
}
