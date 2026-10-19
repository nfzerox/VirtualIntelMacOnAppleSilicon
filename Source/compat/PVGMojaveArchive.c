/* Scoped to the donor GPUCompilerImpl, not a general libarchive replacement.
 * Apple libarchive-83.100.2 archive_virtual.c identifies finish as the old
 * spelling of free. Its bzip2 setter clears existing filters before adding
 * bzip2; GPUCompilerImpl calls it exactly once immediately after write_new
 * and set_format_pax_restricted (20G1443 disassembly at 0x7fff67429102).
 * Thus the old single-compressor API is equivalent for this consumer.
 */
struct archive;
extern int archive_write_finish(struct archive *);
extern int archive_write_set_compression_bzip2(struct archive *);
int archive_write_free(struct archive *a) { return archive_write_finish(a); }
int archive_write_add_filter_bzip2(struct archive *a) {
    return archive_write_set_compression_bzip2(a);
}
