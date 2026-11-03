"""Transient exact-Mavericks continuation-only scheduler experiment.

vim-continuation-scheduler on|off|status EXPECTED_BACKEND_UUID

Only the post-render call site may select the EXISTING native open-loop
deadline calculation. Initial transaction admission keeps the original native
global-flag test and VBL batching. No global flag, pixels, damage, clock or
buffer count is changed. Qualify real Dock deferrals before accepting this.

Requires a stopped OWNED-driver x64 WindowServer, never the physical reference.
On first enable, mmap/mprotect are explicit inferior system calls, using only
non-fixed address hints (never MAP_FIXED). No host change or Apple disk edit.
One small RX page is retained until process exit even after off: another
stopped thread could have its PC in the trampoline. No use-after-free/reuse.
"""
import json
import struct
import lldb

UUID = 'ED5E3C377E2F38858091DFC9B44B38E8'
MARKER = b'VIM-CG-POSTREARM-v1\0'


def command(debugger, text, result, internal_dict):
    del internal_dict
    args = text.split()
    if len(args) != 2 or args[0] not in ('on', 'off', 'status'):
        result.SetError('Use vim-continuation-scheduler on|off|status EXPECTED_BACKEND_UUID')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != 'WindowServer' or process.GetState() != lldb.eStateStopped:
        result.SetError('Stop the owned-driver guest WindowServer; never use the physical reference.')
        return

    def symbol(name, module_name, expected=None):
        found = set()
        entries = target.FindSymbols(name)
        for i in range(entries.GetSize()):
            e = entries.GetContextAtIndex(i)
            module = e.GetModule()
            if module.GetFileSpec().GetFilename() != module_name:
                continue
            if expected and (module.GetUUIDString() or '').replace('-', '').upper() != expected:
                continue
            at = e.GetSymbol().GetStartAddress().GetLoadAddress(target)
            if at != lldb.LLDB_INVALID_ADDRESS:
                found.add(at)
        if len(found) != 1:
            raise RuntimeError('Missing/ambiguous qualified symbol: ' + name)
        return next(iter(found))

    def read(at, n):
        error = lldb.SBError()
        value = process.ReadMemory(at, n, error)
        if error.Fail() or len(value) != n:
            raise RuntimeError('Cannot read complete qualified memory.')
        return value

    def write(at, value):
        error = lldb.SBError()
        if process.WriteMemory(at, value, error) != len(value) or error.Fail() or read(at, len(value)) != value:
            raise RuntimeError('Memory write/readback failed; inspect before resuming.')

    def call(address, signature, arguments):
        options = lldb.SBExpressionOptions()
        options.SetIgnoreBreakpoints(True)
        options.SetUnwindOnError(True)
        options.SetTimeoutInMicroSeconds(3000000)
        value = target.GetProcess().GetSelectedThread().GetFrameAtIndex(0).EvaluateExpression(
            '((%s)0x%x)(%s)' % (signature, address, arguments), options)
        if value.GetError().Fail():
            raise RuntimeError('Explicit scratch-page system call failed: ' + str(value.GetError()))
        return value.GetValueAsUnsigned()

    def relative(destination, after):
        distance = destination - after
        if not -(1 << 31) <= distance < (1 << 31):
            raise RuntimeError('Scratch page is outside the exact native branch range.')
        return struct.pack('<i', distance)

    try:
        wanted = args[1].replace('-', '').upper()
        symbol('dg_common_state_hits', 'libVIMPassthroughOpenGL.dylib', wanted)
        begin = symbol('begin_deferred_update_cycle', 'CoreGraphics', UUID)
        update = symbol('CGXUpdateDisplay', 'CoreGraphics', UUID)
        if read(begin, 4) != b'\x55\x48\x89\xe5':
            raise RuntimeError('Unmatched native frame-pointer prologue.')
        patch = begin + 0x6f
        post = update + 0x293d
        if read(post, 5) != b'\xe8' + relative(begin, post + 5):
            raise RuntimeError('Unmatched post-render native call site.')
        if read(patch + 7, 6) != b'\x74\x13\x66\x0f\x2e\xc1':
            raise RuntimeError('Unmatched native branch bodies.')
        flag = symbol('gOpenLoopScheduler', 'CoreGraphics', UUID)
        # dyld's shared-cache placement rewrites this RIP-relative reference
        # between __TEXT and __DATA even though the image UUID is unchanged.
        # Derive/validate it from the actual loaded symbol, not the loose file.
        original = b'\x80\x3d' + relative(flag, patch + 7) + b'\x00'
        for name, expected in (('gDeferredUpdates', 1), ('gDeferredUpdatesPref', 1),
                               ('gForceDeferredUpdates', 0), ('gTripleBufferUpdates', 0),
                               ('gOpenLoopScheduler', 0)):
            if read(symbol(name, 'CoreGraphics', UUID), 1) != struct.pack('B', expected):
                raise RuntimeError('Changed/unsafe scheduler flags; no patch made.')

        def stub(at):
            # RBP+8 is proved by the native prologue, not an inferred object
            # offset. Preserve RAX/all XMMs; both branches enter original code.
            code = b'\x50\x48\xb8' + struct.pack('<Q', post + 5) + b'\x48\x39\x45\x08\x58'
            code += b'\x0f\x84' + relative(begin + 0x78, at + len(code) + 6)
            code += b'\x80\x3d' + relative(flag, at + len(code) + 7) + b'\x00'
            code += b'\x0f\x85' + relative(begin + 0x78, at + len(code) + 6)
            code += b'\xe9' + relative(begin + 0x8b, at + len(code) + 5)
            return code

        code = read(patch, 7)
        page = None
        if code != original:
            if code[:1] != b'\xe9' or code[5:] != b'\x90\x90':
                raise RuntimeError('Unrecognized native patch; no write made.')
            page = patch + 5 + struct.unpack('<i', code[1:5])[0]
            marker = MARKER + struct.pack('<QQQ', begin, post + 5, flag)
            if page % 4096 or read(page, 40) != stub(page) or read(page + 64, len(marker)) != marker:
                raise RuntimeError('Unrecognized continuation trampoline; no write made.')
        if args[0] == 'on' and page is None:
            mmap = symbol('mmap', 'libsystem_kernel.dylib')
            protect = symbol('mprotect', 'libsystem_kernel.dylib')
            unmap = symbol('munmap', 'libsystem_kernel.dylib')
            # SDK: MAP_PRIVATE|MAP_ANON=0x1002, PROT_READ|WRITE=3.
            # mmap may choose somewhere else; range-check before any patch.
            hint = (patch - 0x40000000) & ~4095
            page = call(mmap, 'void *(*)(void *, unsigned long, int, int, int, long)',
                        '(void *)0x%x, 4096, 3, 0x1002, -1, 0' % hint)
            if page in (0, (1 << 64) - 1):
                raise RuntimeError('No scratch mapping; native code unchanged.')
            try:
                replacement = b'\xe9' + relative(page, patch + 5) + b'\x90\x90'
                marker = MARKER + struct.pack('<QQQ', begin, post + 5, flag)
                write(page, stub(page));write(page + 64, marker)
                if call(protect, 'int (*)(void *, unsigned long, int)', '(void *)0x%x, 4096, 5' % page):
                    raise RuntimeError('Scratch RX protection failed; native code unchanged.')
            except RuntimeError:
                call(unmap, 'int (*)(void *, unsigned long)', '(void *)0x%x, 4096' % page)
                raise
            try:
                write(patch, replacement)
            except RuntimeError:
                write(patch, original)
                raise RuntimeError('Patch failed; original branch restored; RX scratch retained until exit.')
            code = replacement
        elif args[0] == 'off' and page is not None:
            write(patch, original);code = original
        result.PutCString(json.dumps(dict(ContinuationOnly=code != original,
            Patch=hex(patch), RetainedScratch=hex(page) if page is not None else None,
            InitialAdmission='native closed-loop/global-flag path',
            Next='Detach AND quit before actual pacing and Dock-batching qualification.')))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-continuation-scheduler')
