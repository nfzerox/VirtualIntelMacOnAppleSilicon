/* Snow Leopard: try the original native SMB client with explicit Kerberos
 * principals. The caller owns the temporary ticket cache and mountpoint. */
#include <CoreFoundation/CoreFoundation.h>
#include <NetFS/NetFSPlugin.h>
#include <limits.h>
#include <stdio.h>
#include <sys/mount.h>

#ifdef VIM_SNOW_PRIVATE_GSS
int snow_private_gss_start(const char *, const char *);
void snow_private_gss_stop(void);
#endif

int main(int argc, char **argv)
{
    if (argc != 5 && argc != 6) return 64; /* plus optional read-only fixture */
    CFStringRef input[4];
    for (int i = 0; i < 4; ++i) {
        input[i] = CFStringCreateWithCString(NULL, argv[i + 1], kCFStringEncodingUTF8);
        if (!input[i]) return 64;
    }
    CFURLRef url = CFURLCreateWithString(NULL, input[0], NULL);
    CFURLRef path = CFURLCreateWithFileSystemPath(NULL,
        CFSTR("/System/Library/Filesystems/NetFSPlugins/smb.bundle"),
        kCFURLPOSIXPathStyle, true);
    if (!url || !path) return 65;
    CFPlugInRef plugin = CFPlugInCreate(NULL, path);
    if (!plugin) return 66;
    CFArrayRef factories = CFPlugInFindFactoriesForPlugInTypeInPlugIn(kNetFSTypeID, plugin);
    if (!factories || !CFArrayGetCount(factories)) return 66;
    NetFSMountInterface_V1 **client = CFPlugInInstanceCreate(NULL,
        CFArrayGetValueAtIndex(factories, 0), kNetFSTypeID);
    if (!client) return 66;

    void *session = NULL;
    int status = (*client)->CreateSessionRef(&session);
    printf("CreateSessionRef: %d\n", status);
    if (status) return 1;
    const void *keys[] = { kNetFSClientPrincipalKey, kNetFSServicePrincipalKey };
    const void *values[] = { input[2], input[3] };
    CFDictionaryRef identity = CFDictionaryCreate(NULL, keys, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void *open_keys[] = { kNetFSUseKerberosKey, kNetFSKerberosInfoKey,
        kNetFSForceNewSessionKey };
    const void *open_values[] = { kCFBooleanTrue, identity, kCFBooleanTrue };
    CFDictionaryRef options = CFDictionaryCreate(NULL, open_keys, open_values, 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryRef info = NULL;
#ifdef VIM_SNOW_PRIVATE_GSS
    if (!snow_private_gss_start(argv[3], argv[4])) {
        fputs("Private GSS task port unavailable\n", stderr);
        return 67;
    }
#endif
    status = (*client)->OpenSession(url, session, options, &info);
    printf("OpenSession: %d, Kerberos: %d\n", status, info &&
        CFDictionaryGetValue(info, kNetFSMountedByKerberosKey) == kCFBooleanTrue);
    if (info) CFRelease(info);
    if (!status) {
        int flags = MNT_RDONLY | MNT_NOSUID | MNT_NODEV;
        CFNumberRef flag_number = CFNumberCreate(NULL, kCFNumberIntType, &flags);
        const void *mount_keys[] = { kNetFSMountFlagsKey };
        const void *mount_values[] = { flag_number };
        CFDictionaryRef mount_options = CFDictionaryCreate(NULL, mount_keys, mount_values, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        info = NULL;
        status = (*client)->Mount(session, url, input[1], mount_options, &info);
        printf("Mount: %d\n", status);
        if (!status && argc == 6) {
            struct statfs fs;
            char file[PATH_MAX], line[128];
            int length = snprintf(file, sizeof(file), "%s/%s", argv[2], argv[5]);
            FILE *readback = NULL;
            if (length > 0 && length < (int)sizeof(file) && !statfs(argv[2], &fs) &&
                !strcmp(fs.f_fstypename, "smbfs") && (fs.f_flags & MNT_RDONLY))
                readback = fopen(file, "r");
            if (readback && fgets(line, sizeof(line), readback)) printf("Read-only SMB file: %s", line);
            else { perror("Read-only SMB fixture"); status = 1; }
            if (readback) fclose(readback);
        }
        if (info) CFRelease(info);
        CFRelease(mount_options);
        CFRelease(flag_number);
    }
    (*client)->CloseSession(session);
#ifdef VIM_SNOW_PRIVATE_GSS
    snow_private_gss_stop();
#endif
    (*client)->Release(client);
    CFRelease(options);
    CFRelease(identity);
    CFRelease(factories);
    CFRelease(plugin);
    CFRelease(path);
    CFRelease(url);
    for (int i = 0; i < 4; ++i) CFRelease(input[i]);
    return status != 0;
}
