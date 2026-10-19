"""Bounded, read-only allocation trace at the native IOAccel boundary."""
import json
import lldb
import os
import shlex
import struct
import time

def allocation(frame, location, _):
    process = frame.GetThread().GetProcess()
    address = frame.FindRegister('rsi').GetValueAsUnsigned()
    size = frame.FindRegister('rdx').GetValueAsUnsigned() & 0xffffffff
    if size < 0x9c or size > 0x10000:
        return False
    error = lldb.SBError()
    data = process.ReadMemory(address, size, error)
    if not error.Success() or len(data) < 0x9c:
        return False
    pixel_format = struct.unpack_from('<H', data, 0x7e)[0]
    if pixel_format not in (252, 255, 260):
        return False
    print('PVG_DEPTH_RESOURCE ' + json.dumps({
        'size': size, 'format': pixel_format,
        'qwords': ['%016x' % value for value in struct.unpack_from('<'+'Q'*(len(data)//8), data)],
        'stack': [frame.GetThread().GetFrameAtIndex(i).GetFunctionName() for i in range(min(6,frame.GetThread().GetNumFrames()))],
    }), flush=True)
    return False

def run(debugger, command, result, _):
    args = shlex.split(command)
    if not args:
        result.SetError('executable and optional arguments required')
        return
    target = debugger.CreateTarget(args[0])
    breakpoint = target.BreakpointCreateByName('IOAccelResourceCreate', 'IOAccelerator')
    breakpoint.SetScriptCallbackFunction(__name__+'.allocation')
    debugger.SetAsync(True)
    launch = lldb.SBLaunchInfo(args[1:])
    # The guest user's home, even when the debugger itself runs under sudo.
    home = os.path.expanduser('~' + os.environ.get('SUDO_USER', ''))
    launch.SetEnvironmentEntries([
        'HOME='+home, 'LC_ALL=C', 'PVG_GL_TEST_INTEL_PROFILE=1',
        'PVG_GL_TRACE_TEXTURES=1',
    ], True)
    launch.SetWorkingDirectory(home)
    # Debugger may need administrator privileges, but the app must retain
    # the ordinary guest user's identity and sandbox context.
    launch.SetUserID(501)
    launch.SetGroupID(20)
    error=lldb.SBError()
    process = target.Launch(launch,error)
    if not error.Success():
        debugger.SetAsync(False)
        result.SetError(str(error))
        return
    deadline = time.monotonic()+25
    try:
        while time.monotonic()<deadline:
            state = process.GetState()
            if state in (lldb.eStateExited,lldb.eStateCrashed,lldb.eStateDetached,lldb.eStateInvalid):
                break
            time.sleep(.1)
    finally:
        if process.IsValid() and process.GetState() not in (lldb.eStateExited,lldb.eStateDetached):
            process.Kill()
        debugger.SetAsync(False)

def __lldb_init_module(debugger, _):
    debugger.HandleCommand('command script add -f '+__name__+'.run pvg-depth-trace')
