#pragma once

#include <cerrno>
#include <cstring>
#include <deque>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <unistd.h>
#include <vector>

inline std::string guestRoot;
inline std::string guestDataRoot;
inline std::vector<std::pair<std::string,std::string>> guestFirmlinks;
static char* guestRealpath(const char*,char*,char*);

static bool guestPathPrefix(const std::string& path,const std::string& prefix) {
    return path==prefix||(path.size()>prefix.size()&&path.compare(0,prefix.size(),prefix)==0&&path[prefix.size()]=='/');
}

// Catalina's cache must describe the final System/Data inodes, not a copied
// HFS staging tree. Route its original firmlinks without mounting either
// private volume over the host's root or following host absolute symlinks.
static inline bool guestConfigureDataRoot(const std::string& data) {
    char manifest[PATH_MAX];
    if(!guestRealpath("/usr/share/firmlinks",manifest,nullptr))return false;
    std::ifstream input(manifest);
    if(!input)return false;
    std::vector<std::pair<std::string,std::string>> links;
    std::string line;
    auto safe=[](const std::string& path) {
        if(path.empty()||path[0]=='/'||path.back()=='/')return false;
        size_t start=0;
        do {
            size_t end=path.find('/',start);
            std::string part=path.substr(start,end==std::string::npos?end:end-start);
            if(part.empty()||part=="."||part=="..")return false;
            if(end==std::string::npos)break;
            start=end+1;
        } while(start<path.size());
        return true;
    };
    while(std::getline(input,line)) {
        if(line.empty())continue;
        size_t tab=line.find('\t');
        if(tab==std::string::npos||line[0]!='/'||line.find('\t',tab+1)!=std::string::npos)return false;
        std::string logical=line.substr(1,tab-1),destination=line.substr(tab+1);
        if(!safe(logical)||!safe(destination))return false;
        if(logical=="AppleInternal"&&destination=="AppleInternal")continue;
        for(const auto& old:links)
            if(guestPathPrefix("/"+logical,old.first)||guestPathPrefix(old.first,"/"+logical)||
               guestPathPrefix("/"+destination,old.second)||guestPathPrefix(old.second,"/"+destination))return false;
        links.emplace_back("/"+logical,"/"+destination);
    }
    if(input.bad()||links.empty())return false;
    guestDataRoot=data;guestFirmlinks=std::move(links);return true;
}

static inline std::string guestPhysicalPath(const std::string& logical) {
    if(!guestDataRoot.empty()) {
        const std::string mount="/System/Volumes/Data";
        if(guestPathPrefix(logical,mount))return guestDataRoot+logical.substr(mount.size());
        for(const auto& link:guestFirmlinks)
            if(guestPathPrefix(logical,link.first))return guestDataRoot+link.second+logical.substr(link.first.size());
    }
    return guestRoot+logical;
}

static void pushComponents(const std::string& path, std::deque<std::string>& pending) {
    std::deque<std::string> prefix;
    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        if (end > start)
            prefix.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    pending.insert(pending.begin(), prefix.begin(), prefix.end());
}

// Resolve absolute symlinks relative to the installed volume, not the host.
// A resolved path is always a path under the private guest input root.
static char* guestRealpath(const char* name, char* out, char* logicalOut=nullptr) {
    if (!name || name[0] != '/') {
        errno = EINVAL;
        return nullptr;
    }
    std::string path(name);
    if (path == guestRoot)
        path = "/";
    else if (path.compare(0, guestRoot.size() + 1, guestRoot + "/") == 0)
        path.erase(0, guestRoot.size());
    else if(!guestDataRoot.empty()&&guestPathPrefix(path,guestDataRoot)) {
        path.erase(0,guestDataRoot.size());
        bool mapped=false;
        for(const auto& link:guestFirmlinks)if(guestPathPrefix(path,link.second)) {
            path=link.first+path.substr(link.second.size());mapped=true;break;
        }
        if(!mapped)path="/System/Volumes/Data"+path;
    }
    std::deque<std::string> pending;
    pushComponents(path, pending);
    std::vector<std::string> resolved;
    unsigned symlinks = 0;
    while (!pending.empty()) {
        auto component = pending.front();
        pending.pop_front();
        if (component == ".")
            continue;
        if (component == "..") {
            if (!resolved.empty())
                resolved.pop_back();
            continue;
        }
        std::string logical;
        for (const auto& part : resolved)
            logical += "/" + part;
        logical += "/" + component;
        auto candidate = guestPhysicalPath(logical);
        struct stat info;
        if (::lstat(candidate.c_str(), &info))
            return nullptr;
        if (S_ISLNK(info.st_mode)) {
            char target[PATH_MAX];
            ssize_t length = ::readlink(candidate.c_str(), target, sizeof(target) - 1);
            if (length <= 0)
                return nullptr;
            if (++symlinks > 32) {
                errno = ELOOP;
                return nullptr;
            }
            if (target[0] == '/')
                resolved.clear();
            pushComponents(std::string(target, (size_t)length), pending);
        } else {
            if (!pending.empty() && !S_ISDIR(info.st_mode)) {
                errno = ENOTDIR;
                return nullptr;
            }
            resolved.push_back(std::move(component));
        }
    }
    std::string logical;
    for (const auto& part : resolved)
        logical += "/" + part;
    path=guestPhysicalPath(logical);
    if (path.size() >= PATH_MAX || logical.size() >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return nullptr;
    }
    strcpy(out, path.c_str());
    if(logicalOut)strcpy(logicalOut,logical.empty()?"/":logical.c_str());
    return out;
}
