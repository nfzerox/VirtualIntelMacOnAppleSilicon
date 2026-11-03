/* Darwin 10 has pthread TLS, but not dyld's Lion-era TLV machinery.
 * Use real process-private pthread keys, never borrow Apple's reserved GL
 * TLS slot (which differs between releases). Only the Snow Leopard build
 * takes this path; later guests retain their existing compiler TLS.
 * The key is initialized once at library load, not on every GL call.
 * This storage owns only its slots; existing context binding code retains
 * responsibility for context lifetime and its thread-exit reference. */
#ifndef VIM_PERIOD_THREAD_STORAGE_H
#define VIM_PERIOD_THREAD_STORAGE_H
#include <pthread.h>
#include <stdlib.h>
#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
#define VIM_PERIOD_PTHREAD_TLS 1
#define VIM_DEFINE_PERIOD_STORAGE(Type, Name) \
    static pthread_key_t Name##_key; \
    __attribute__((constructor)) static void Name##_initialize(void) \
    { if(pthread_key_create(&Name##_key,free))abort(); } \
    static inline Type *Name##_storage(void) \
    { Type *value=(Type *)pthread_getspecific(Name##_key); \
      if(!value) { value=(Type *)calloc(1,sizeof(*value)); \
        if(!value||pthread_setspecific(Name##_key,value))abort(); } \
      return value; }
#else
#define VIM_PERIOD_PTHREAD_TLS 0
#endif
#endif
