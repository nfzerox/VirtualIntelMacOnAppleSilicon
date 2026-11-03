/* Private in-process executor transport. No guest/host pointer wire ABI.
 * The caller owns its endpoint until all caller-side I/O returns, then closes
 * it exactly once. Shutdown may run concurrently with I/O and wakes both ends;
 * close itself is not a substitute for retaining an endpoint during a call.
 * Normal close preserves already accepted request bytes for the worker to
 * drain, as socket close does; shutdown explicitly aborts queued work.
 * The worker owns the other reference until its callback returns. */
#ifndef DG_MEMORY_STREAM_H
#define DG_MEMORY_STREAM_H
#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>
struct DGMemoryStream;
typedef void (*DGMemoryWorker)(struct DGMemoryStream *);
struct DGMemoryStream *DGMemoryStreamOpen(DGMemoryWorker);
ssize_t DGMemoryClientRead(struct DGMemoryStream *,void *,size_t);
ssize_t DGMemoryClientWrite(struct DGMemoryStream *,const void *,size_t);
ssize_t DGMemoryClientReadv(struct DGMemoryStream *,const struct iovec *,int);
ssize_t DGMemoryClientWritev(struct DGMemoryStream *,const struct iovec *,int);
ssize_t DGMemoryServerRead(struct DGMemoryStream *,void *,size_t);
ssize_t DGMemoryServerWrite(struct DGMemoryStream *,const void *,size_t);
ssize_t DGMemoryServerWritev(struct DGMemoryStream *,const struct iovec *,int);
int DGMemoryStreamShutdown(struct DGMemoryStream *);
int DGMemoryStreamClose(struct DGMemoryStream *);
/* Retains the caller reference until the worker callback has returned. The
 * callback, not EOF/shutdown, must establish any external GPU completion. No
 * concurrent caller I/O may remain; shutdown is still the cancellation API. */
int DGMemoryStreamCloseAndWait(struct DGMemoryStream *);
unsigned DGMemoryStreamLiveCount(void); /* Diagnostic observation, no polling service. */
#endif
