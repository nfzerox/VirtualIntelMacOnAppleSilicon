"""Bounded QuickTime metadata enumeration for the period guest LLDB.

Attach to the existing player, `command script import THIS_FILE`, run
`vim-document-state`, then `process detach` and `quit`. An optional exact
object pointer surveys a controller already found in that metadata, without
guessing its class layout. Pause playback before attaching. This calls the
target's Objective-C runtime (including allocating/freeing an ivar list), so
bounded enumeration does not guarantee bounded debugger attach/detach or
runtime-lock waits. Keep an independent SSH session available to end a stuck
debugger; do not reboot the VM to clear it. No injected library, hooks, player
setters, or launch environment changes. This is a development diagnostic, not
a guest dependency or a shipped playback implementation.
"""
import re
import struct
import lldb


def inspect(debugger, command, result, internal_dict):
    del internal_dict
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if process.GetState() != lldb.eStateStopped or target.GetAddressByteSize() != 8:
        result.SetError("Attach to the stopped 64-bit player first.")
        return

    def native(name):
        matches = target.FindFunctions(name, lldb.eFunctionNameTypeFull)
        for index in range(matches.GetSize()):
            context = matches.GetContextAtIndex(index)
            symbol = context.GetSymbol()
            address = symbol.GetStartAddress() if symbol.IsValid() else context.GetFunction().GetStartAddress()
            value = address.GetLoadAddress(target)
            if value not in (0, lldb.LLDB_INVALID_ADDRESS):
                return value
        raise RuntimeError("No loaded runtime symbol: " + name)

    def number(expression):
        # objc++ avoids the old objc evaluator's nonconstant-initializer bug.
        # Keep each target expression scalar; metadata traversal stays here.
        reply = lldb.SBCommandReturnObject()
        debugger.GetCommandInterpreter().HandleCommand(
            "expr -l objc++ -f hex -- (unsigned long long)(" + expression + ")", reply)
        if not reply.Succeeded():
            raise RuntimeError(reply.GetError())
        value = re.search(r"=\s*(0x[0-9a-fA-F]+)", reply.GetOutput())
        if not value:
            raise RuntimeError("Unexpected scalar expression result: " + reply.GetOutput())
        return int(value.group(1), 16)

    def string(address):
        if not address:
            return "(null)"
        error = lldb.SBError()
        value = process.ReadCStringFromMemory(address, 512, error)
        if error.Fail():
            raise RuntimeError(str(error))
        return value

    def call(name, signature, arguments):
        return number("((" + signature + ")0x%x)(%s)" % (native(name), arguments))

    try:
        supplied=command.strip()
        if supplied:
            if not re.match(r"^0x[0-9a-fA-F]+$",supplied):
                result.SetError("Use the exact object pointer from the previous metadata survey.")
                return
            document=int(supplied,16)
        else:
            document = number('(id)[(id)[(id)[(id)((void *(*)(const char *))0x%x)("NSDocumentController") '
                              'sharedDocumentController] documents] lastObject]' % native("objc_getClass"))
        if not document:
            result.SetError("The player has no open document.")
            return
        result.PutCString("DOCUMENT 0x%x" % document)
        kind = call("object_getClass", "void *(*)(void *)", "(void *)0x%x" % document)
        for level in range(4):
            if not kind:
                break
            name = string(call("class_getName", "const char *(*)(void *)", "(void *)0x%x" % kind))
            result.PutCString("CLASS " + name)
            if name in ("NSDocument", "NSObject", "NSViewController", "NSView", "NSWindow", "CALayer"):
                break
            # The count is a real target-side scalar, not a host pointer.
            counter = "$vim_count_%d" % level
            reply = lldb.SBCommandReturnObject()
            debugger.GetCommandInterpreter().HandleCommand("expr -l objc++ -- unsigned int " + counter + "=0", reply)
            if not reply.Succeeded():
                raise RuntimeError(reply.GetError())
            ivars = call("class_copyIvarList", "void *(*)(void *,unsigned int *)", "(void *)0x%x,&%s" % (kind, counter))
            try:
                count = number(counter)
                for index in range(min(count, 64)):
                    error = lldb.SBError()
                    raw = process.ReadMemory(ivars + index * 8, 8, error)
                    if error.Fail():
                        raise RuntimeError(str(error))
                    ivar = struct.unpack("<Q", raw)[0]
                    arg = "(void *)0x%x" % ivar
                    label = string(call("ivar_getName", "const char *(*)(void *)", arg))
                    encoding = string(call("ivar_getTypeEncoding", "const char *(*)(void *)", arg))
                    offset = call("ivar_getOffset", "long (*)(void *)", arg)
                    value = ""
                    if encoding.startswith("@"):
                        raw = process.ReadMemory(document + offset, 8, error)
                        if error.Fail():
                            raise RuntimeError(str(error))
                        value = " value=0x%x" % struct.unpack("<Q", raw)[0]
                    result.PutCString("IVAR %s type=%s offset=%d%s" % (label, encoding, offset, value))
            finally:
                if ivars:
                    number("({ ((void (*)(void *))0x%x)((void *)0x%x); 0; })" % (native("free"), ivars))
            kind = call("class_getSuperclass", "void *(*)(void *)", "(void *)0x%x" % kind)
    except RuntimeError as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, internal_dict):
    del internal_dict
    debugger.HandleCommand("command script add -f quicktime_document_state.inspect vim-document-state")
