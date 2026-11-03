/* Guest-local, cross-bitness byte stream. Only fixed-width offsets/counters
 * live in the memory entry. Mach rights, pointers and native API tables do not.
 * This is not the guest/host graphics wire or a second GL implementation. */
#ifndef VIM_GL_MACH_STREAM_H
#define VIM_GL_MACH_STREAM_H
#include <sys/types.h>
#include <sys/uio.h>
struct DGMachStream;
typedef void (*DGMachWorker)(struct DGMachStream *);
struct DGMachStream *DGMachOpen(void);
ssize_t DGMachRead(struct DGMachStream *,void *,size_t);
ssize_t DGMachWrite(struct DGMachStream *,const void *,size_t);
ssize_t DGMachReadv(struct DGMachStream *,const struct iovec *,int);
ssize_t DGMachWritev(struct DGMachStream *,const struct iovec *,int);
int DGMachShutdown(struct DGMachStream *);
int DGMachClose(struct DGMachStream *);
unsigned DGMachLiveCount(void);
int DGMachServe(DGMachWorker);
#endif
