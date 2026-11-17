import ctypes
import errno
import lzma
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest


SOURCE = Path(__file__).resolve().parents[1]
PROGRESS = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_void_p)


def record(payload, encoded=None):
    stored = payload if encoded is None else encoded
    return struct.pack(">QQ", len(payload), len(stored)) + stored


def stream(*records, maximum=16 * 1024**2):
    return b"pbzx" + struct.pack(">Q", maximum) + b"".join(records)


def der(tag, content):
    length = len(content)
    if length < 128:
        return bytes((tag, length)) + content
    encoded = length.to_bytes((length.bit_length() + 7) // 8, "big")
    return bytes((tag, 0x80 | len(encoded))) + encoded + content


def mtree(*records):
    payload = b"pbze" + struct.pack(">Q", 16 * 1024**2) + b"".join(records)
    return der(0x30, der(0x16, b"IM4P") + der(0x16, b"xmtr") + der(0x16, b"Tree") + der(4, payload))


@unittest.skipUnless(sys.platform == "darwin", "Uses the macOS liblzma ABI")
class NativePBZXTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(folder.cleanup)
        dylib = Path(folder.name) / "pbzx.dylib"
        subprocess.run(("xcrun", "clang", "-dynamiclib", "-Wall", "-Wextra", "-Werror",
                        "-I", SOURCE / "vendor/xz-api", SOURCE / "tools/host-install-pbzx.c", "-llzma.5", "-lcompression",
                        "-o", dylib), check=True)
        cls.library = ctypes.CDLL(str(dylib), use_errno=True)
        cls.library.VIMDecodePBZX.argtypes = (ctypes.c_int, ctypes.c_int, PROGRESS, ctypes.c_void_p)
        cls.library.VIMDecodePBZX.restype = ctypes.c_int
        cls.library.VIMDecodeIntelMTree.argtypes = cls.library.VIMDecodePBZX.argtypes
        cls.library.VIMDecodeIntelMTree.restype = ctypes.c_int

    def decode(self, data, callback=None, *, intel_mtree=False):
        with tempfile.TemporaryFile() as source, tempfile.TemporaryFile() as output:
            source.write(data)
            source.seek(0)
            decoder = self.library.VIMDecodeIntelMTree if intel_mtree else self.library.VIMDecodePBZX
            status = decoder(source.fileno(), output.fileno(), PROGRESS(callback) if callback else PROGRESS(), None)
            code = ctypes.get_errno() if status else 0
            output.seek(0)
            return code, output.read()

    def test_mixed_records_and_progress(self):
        raw, plain = b"native archive header", b"content" * 4000
        source = stream(record(raw), record(plain, lzma.compress(plain)))
        seen = []
        def progress(read, written, _):
            seen.append((read, written))
            return True
        self.assertEqual(self.decode(source, progress), (0, raw + plain))
        self.assertEqual(seen[-1], (len(source), len(raw + plain)))
        self.assertEqual(seen, sorted(seen))

    def test_rejects_bad_headers_and_truncation(self):
        for source in (b"PBZX", stream(maximum=2**32), stream(b"short"),
                       stream(record(b"sample")[:-1]), stream(record(b"too large"), maximum=8),
                       stream(record(b"incorrect stored format", b"smaller"))):
            with self.subTest(source=source):
                self.assertEqual(self.decode(source), (errno.EBADMSG, b""))

    def test_pipeline_reuses_slots_with_larger_blocks_and_keeps_order(self):
        blocks=[bytes((i,))*size for i,size in enumerate((71,8000,500000,300,120000,900000,19,77777))]
        encoded=[record(block,lzma.compress(block)) if i%3 else record(block)
                 for i,block in enumerate(blocks)]
        seen=[]
        def progress(read,written,_):
            seen.append((read,written));return True
        source=stream(*encoded,maximum=1024*1024)
        self.assertEqual(self.decode(source,progress),(0,b''.join(blocks)))
        self.assertEqual(seen,sorted(seen))
        self.assertEqual(seen[-1],(len(source),sum(map(len,blocks))))

    def test_later_decoder_failure_drains_inflight_work(self):
        block=b'content'*40000
        source=stream(record(block,lzma.compress(block)),record(block,lzma.compress(block)),
                      record(block,lzma.compress(block)[:-3]))
        self.assertEqual(self.decode(source)[0],errno.EBADMSG)
        self.assertEqual(self.decode(stream(record(b'next call'))),(0,b'next call'))

    def test_checks_xz_footer_and_exact_decoded_length(self):
        plain = b"apple" * 1024
        encoded = lzma.compress(plain)
        for corrupt in (record(plain, encoded[:-1]), record(plain[:-1], encoded),
                        record(plain, encoded + b"junk")):
            self.assertEqual(self.decode(stream(corrupt)), (errno.EBADMSG, b""))

    def test_cancellation_stops_before_the_next_write(self):
        plain = b"apple" * 200000
        code, output = self.decode(stream(record(plain, lzma.compress(plain))),
                                   lambda read, written, _: written < 256 * 1024)
        self.assertEqual(code, errno.ECANCELED)
        self.assertEqual(output, plain[:256 * 1024])

    def test_pause_remains_available_when_a_nonblocking_pipe_is_full(self):
        read_fd, write_fd = os.pipe()
        try:
            os.set_blocking(write_fd, False)
            while True:
                try:
                    os.write(write_fd, bytes(4096))
                except BlockingIOError:
                    break
            with tempfile.TemporaryFile() as source:
                source.write(stream(record(bytes(256 * 1024))))
                source.seek(0)
                seen = []

                def progress(_, written, _context):
                    seen.append(written)
                    return len(seen) < 4

                started = time.monotonic()
                status = self.library.VIMDecodePBZX(source.fileno(), write_fd, PROGRESS(progress), None)
                self.assertEqual((status, ctypes.get_errno()), (-1, errno.ECANCELED))
                self.assertEqual(seen, [0] * 4)
                self.assertGreater(time.monotonic() - started, 0.2)
        finally:
            os.close(write_fd)
            os.close(read_fd)

    def test_intel_mtree_accepts_exact_lzfse_stream_and_raw_blocks(self):
        raw, plain = b"Apple Archive", b"signed tree record\0" * 4000
        compression = ctypes.CDLL("/usr/lib/libcompression.dylib")
        encode = compression.compression_encode_buffer
        encode.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                           ctypes.c_void_p, ctypes.c_int)
        encode.restype = ctypes.c_size_t
        destination = ctypes.create_string_buffer(len(plain))
        count = encode(destination, len(destination), plain, len(plain), None, 0x801)
        self.assertTrue(0 < count < len(plain))
        encoded = destination.raw[:count]
        source = mtree(record(raw), record(plain, encoded))
        progress = []
        def observe(read, written, _):
            progress.append((read, written))
            return True
        self.assertEqual(self.decode(source, observe, intel_mtree=True), (0, raw + plain))
        self.assertEqual(progress[-1], (len(source), len(raw + plain)))
        self.assertEqual(progress, sorted(progress))
        for corrupt in (record(plain[:-1], encoded), record(plain, encoded[:-1]), record(plain, encoded + b"tail")):
            self.assertEqual(self.decode(mtree(corrupt), intel_mtree=True), (errno.EBADMSG, b""))

    def test_intel_mtree_checks_container_boundaries_and_can_cancel(self):
        plain = b"mtree" * 100
        source = mtree(record(plain))
        for invalid in (b"\x30\x80", source[:-1], source.replace(b"xmtr", b"amtr"),
                        source.replace(b"pbze", b"pbzx"), source + b"x"):
            self.assertEqual(self.decode(invalid, intel_mtree=True)[0], errno.EBADMSG)
        self.assertEqual(self.decode(source, lambda *_: False, intel_mtree=True), (errno.ECANCELED, b""))


if __name__ == "__main__":
    unittest.main()
