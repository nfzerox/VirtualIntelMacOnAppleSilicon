#pragma once

#include <dlfcn.h>

using BOMBom = struct VIMBOMStorage*;
using BOMFSObject = struct VIMBOMObject*;
using BOMBomEnumerator = struct VIMBOMEnumerator*;

struct HostBOM {
    BOMBom (*open)(const char*, bool, void*);
    void (*close)(BOMBom);
    BOMFSObject (*root)(BOMBom);
    BOMBomEnumerator (*begin)(BOMBom, BOMFSObject);
    void (*end)(BOMBomEnumerator);
    BOMFSObject (*next)(BOMBomEnumerator);
    bool (*isBinary)(BOMFSObject);
    const char* (*name)(BOMFSObject);
    void (*freeObject)(BOMFSObject);

    template<typename T> static bool load(void* handle, T& fn, const char* symbol) {
        fn = reinterpret_cast<T>(dlsym(handle, symbol));
        return fn != nullptr;
    }

    bool load() {
        void* handle = dlopen("/System/Library/PrivateFrameworks/Bom.framework/Versions/A/Bom", RTLD_LOCAL | RTLD_NOW);
        return handle && load(handle, open, "BOMBomOpenWithSys") &&
               load(handle, close, "BOMBomFree") &&
               load(handle, root, "BOMBomGetRootFSObject") &&
               load(handle, begin, "BOMBomEnumeratorNew") &&
               load(handle, end, "BOMBomEnumeratorFree") &&
               load(handle, next, "BOMBomEnumeratorNext") &&
               load(handle, isBinary, "BOMFSObjectIsBinaryObject") &&
               load(handle, name, "BOMFSObjectPathName") &&
               load(handle, freeObject, "BOMFSObjectFree");
    }
};
