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
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <signal.h>
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <dirent.h>

#include <vector>
#include <set>
#include <map>
#include <iostream>
#include <fstream>

#include "mega-dylib-utils.h"
#include "MultiCacheBuilder.h"
#include "MachOProxy.h"
#include "Manifest.h"
#include "Logging.h"
#include "../../dyld-host-compat/GuestRoot.hpp"

#if !__has_feature(objc_arc)
#error The use of libdispatch in this files requires it to be compiled with ARC in order to avoid leaks
#endif

const std::string anchorsDirPath = "/private/var/db/dyld/shared_region_roots";

bool parsePathsFile( const std::string& filePath, std::set<std::string>& paths ) {
    verboseLog( "parsing .paths file '%s'", filePath.c_str() );
    std::ifstream myfile( filePath );
        if ( myfile.is_open() ) {
		std::string line;
		while ( std::getline(myfile, line) ) {
			size_t pos = line.find('#');
			if ( pos != std::string::npos )
				line.resize(pos);
			while ( line.size() != 0 && isspace(line.back()) ) {
				line.pop_back();
			}
                        if ( !line.empty() ) paths.insert( line );
                }
                myfile.close();

                return true;
        }
        return false;
}

static bool parseDirectoryOfPathsFiles(const std::string& dirPath, std::set<std::string>& paths)
{
	DIR* dir = ::opendir(dirPath.c_str());
	if ( dir == NULL )
		return false;

	for (dirent* entry = ::readdir(dir); entry != NULL; entry = ::readdir(dir)) {
		if ( entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN ) {
			// only look at regular files ending in .paths
			if ( entry->d_namlen >= 6 && strcmp(&entry->d_name[entry->d_namlen-6], ".paths") == 0 ) {
				struct stat statBuf;
				std::string filePathStr = dirPath + "/" + entry->d_name;
				const char* filePath = filePathStr.c_str();
				if ( lstat(filePath, &statBuf) == -1 ) {
					warning("can't access file '%s'", filePath);
				}
				else if ( S_ISREG(statBuf.st_mode) ) {
					parsePathsFile(filePath, paths);
				}
				else {
					warning("not a regular file '%s'", filePath);
				}
			}
			else {
				warning("ignoring file with wrong extension '%s'", entry->d_name);
			}
		}
	}
	::closedir(dir);
	return true;
}

int main(int argc, const char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: sierra-cache /guest-root /guest-cache-directory\n");
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
    guestRoot = root;
    setReturnNonZeroOnTerminate();
    setWarnAnErrorPrefixes("sierra-cache: warning: ", "sierra-cache failed: ");

    char roots[PATH_MAX];
    std::set<std::string> paths;
    if (!guestRealpath(anchorsDirPath.c_str(), roots) ||
        !parseDirectoryOfPathsFiles(roots, paths) || paths.empty())
        terminate("could not read installed shared-region roots");

    const std::set<std::string> archs{"i386", "x86_64"};
    Manifest manifest(archs, "", root, paths);
    manifest.setPlatform("osx");
    manifest.calculateClosure(false);

    auto builder = std::make_shared<MultiCacheBuilder>(manifest, false, false, false, false, false);
    builder->buildCaches(output);
    builder->logStats();
    dumpLogAndExit(false);
    dispatch_main();
}
