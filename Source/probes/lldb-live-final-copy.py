"""Capture the IOSurface feeding WindowServer's final TextureCopy draw.

Load this script into LLDB after attaching to WindowServer, then call
``pvgfinal.install()`` and continue.  It follows public/private Metal objects
at runtime instead of relying on per-boot addresses.  Once a display-sized
TextureCopy source reaches an IOAccel display completion, the script performs
a synchronized IOSurface readback, writes it under ``/tmp``, and stops so the
command file can detach immediately.
"""

import lldb
import hashlib
import os


MANIFEST = "/tmp/pvg-final-copy-source.txt"
OUTPUT_PREFIX = "/tmp/pvg-final-copy-source"
TEXTURE_COPY_FRAGMENT_SHA256 = (
    "02cc334837e6cac588ac7b5394397bd7758690e75baef18129cbb7dce46e7aa5"
)

target = None
pipeline_names = {}
texture_copy_encoders = set()
source_texture = 0
source_surface = 0
source_width = 0
source_height = 0
source_bpr = 0
source_format = 0
source_storage = 0


def _register(frame, name):
    return frame.FindRegister(name).GetValueAsUnsigned()


def _expression(text, language=lldb.eLanguageTypeC):
    options = lldb.SBExpressionOptions()
    options.SetLanguage(language)
    options.SetIgnoreBreakpoints(True)
    options.SetTimeoutInMicroSeconds(5_000_000)
    return target.EvaluateExpression(text, options)


def _unsigned_property(obj, selector):
    if not obj:
        return 0
    value = _expression(
        "(unsigned long long)((unsigned long long (*)(void *, void *))"
        "objc_msgSend)((void *)0x{:x}, ((void *(*)(const char *))"
        "sel_registerName)(\"{}\"))".format(obj, selector)
    )
    return value.GetValueAsUnsigned() if value.GetError().Success() else 0


def _fragment_name(pipeline):
    value = _expression(
        "(const char *)[[[[(id)0x{:x} valueForKey:@\"_fragmentFunction\"] "
        "valueForKey:@\"function\"] valueForKey:@\"name\"] UTF8String]".format(
            pipeline
        ),
        lldb.eLanguageTypeObjC_plus_plus,
    )
    if not value.GetError().Success():
        return ""
    address = value.GetValueAsUnsigned()
    if not address:
        return ""
    error = lldb.SBError()
    name = target.GetProcess().ReadCStringFromMemory(address, 256, error)
    return name if error.Success() else ""


def _fragment_resource_digest(pipeline):
    resource = _expression(
        "(unsigned long long)[(id)0x{:x} valueForKey:@\"_resource2\"]".format(
            pipeline
        ),
        lldb.eLanguageTypeObjC_plus_plus,
    ).GetValueAsUnsigned()
    if not resource:
        return ""
    address = _expression(
        "(unsigned long long)[(id)0x{:x} contents]".format(resource),
        lldb.eLanguageTypeObjC_plus_plus,
    ).GetValueAsUnsigned()
    length = _expression(
        "(unsigned long long)[(id)0x{:x} length]".format(resource),
        lldb.eLanguageTypeObjC_plus_plus,
    ).GetValueAsUnsigned()
    if not address or not length or length > (1 << 20):
        return ""
    error = lldb.SBError()
    data = target.GetProcess().ReadMemory(address, length, error)
    if not error.Success() or len(data) != length:
        return ""
    return hashlib.sha256(data).hexdigest()


def _append(line):
    with open(MANIFEST, "a") as output:
        output.write(line + "\n")


def on_pipeline(frame, _location, _dict):
    encoder = _register(frame, "rdi")
    pipeline = _register(frame, "rdx")
    pipeline_ref = _unsigned_property(pipeline, "pipelineRef")
    name = pipeline_names.get(pipeline_ref)
    if name is None:
        name = _fragment_name(pipeline)
        digest = _fragment_resource_digest(pipeline)
        if not name and digest == TEXTURE_COPY_FRAGMENT_SHA256:
            name = "TextureCopy"
        pipeline_names[pipeline_ref] = name
        _append(
            "pipeline ref=0x{:x} object=0x{:x} fragment={} resource2={}".format(
                pipeline_ref,
                pipeline,
                name or "(unknown)",
                digest or "(unavailable)",
            )
        )
    if name == "TextureCopy":
        texture_copy_encoders.add(encoder)
    else:
        texture_copy_encoders.discard(encoder)
    return False


def on_fragment_texture(frame, _location, _dict):
    global source_texture, source_surface, source_width, source_height
    global source_bpr, source_format, source_storage

    encoder = _register(frame, "rdi")
    texture = _register(frame, "rdx")
    index = _register(frame, "rcx")
    if encoder not in texture_copy_encoders or index != 0 or not texture:
        return False

    width = _unsigned_property(texture, "width")
    height = _unsigned_property(texture, "height")
    if width < 1000 or height < 700:
        return False

    if source_texture and source_texture != texture:
        _expression(
            "(void)[(id)0x{:x} release]".format(source_texture),
            lldb.eLanguageTypeObjC_plus_plus,
        )
    if source_texture != texture:
        _expression(
            "(void)[(id)0x{:x} retain]".format(texture),
            lldb.eLanguageTypeObjC_plus_plus,
        )

    source_texture = texture
    source_width = width
    source_height = height
    source_bpr = width * 4
    source_format = _unsigned_property(texture, "pixelFormat")
    source_storage = _unsigned_property(texture, "storageMode")
    source_surface = _unsigned_property(texture, "iosurface")
    if source_surface:
        source_bpr = _expression(
            "(unsigned long)((unsigned long (*)(void *))"
            "IOSurfaceGetBytesPerRow)((void *)0x{:x})".format(source_surface)
        ).GetValueAsUnsigned()
    _append(
        "source texture=0x{:x} surface=0x{:x} dimensions={}x{} bpr={} "
        "format=0x{:x} storage={}".format(
            source_texture,
            source_surface,
            source_width,
            source_height,
            source_bpr,
            source_format,
            source_storage,
        )
    )
    return False


def on_display_completion(frame, _location, _dict):
    if not source_texture:
        return False
    if not source_surface:
        _append("completion reached but TextureCopy source has no IOSurface")
        return True

    surface_id = _expression(
        "(unsigned int)((unsigned int (*)(void *))IOSurfaceGetID)"
        "((void *)0x{:x})".format(source_surface)
    ).GetValueAsUnsigned()
    allocation_size = _expression(
        "(unsigned long)((unsigned long (*)(void *))IOSurfaceGetAllocSize)"
        "((void *)0x{:x})".format(source_surface)
    ).GetValueAsUnsigned()
    lock_result = _expression(
        "(int)((int (*)(void *, unsigned int, unsigned int *))IOSurfaceLock)"
        "((void *)0x{:x}, 1, (unsigned int *)0)".format(source_surface)
    ).GetValueAsSigned()
    base = _expression(
        "(unsigned long long)((void *(*)(void *))IOSurfaceGetBaseAddress)"
        "((void *)0x{:x})".format(source_surface)
    ).GetValueAsUnsigned()

    path = "{}-id{}-{}x{}-bpr{}.bin".format(
        OUTPUT_PREFIX, surface_id, source_width, source_height, source_bpr
    )
    copied = 0
    error_text = ""
    if lock_result == 0 and base and allocation_size:
        error = lldb.SBError()
        data = target.GetProcess().ReadMemory(base, allocation_size, error)
        if error.Success() and len(data) == allocation_size:
            with open(path, "wb") as output:
                output.write(data)
            copied = len(data)
        else:
            error_text = error.GetCString() or "short read"
    if lock_result == 0:
        _expression(
            "(int)((int (*)(void *, unsigned int, unsigned int *))"
            "IOSurfaceUnlock)((void *)0x{:x}, 1, (unsigned int *)0)".format(
                source_surface
            )
        )
    _append(
        "completion surfaceID={} lock={} base=0x{:x} size={} copied={} "
        "path={} error={}".format(
            surface_id,
            lock_result,
            base,
            allocation_size,
            copied,
            path,
            error_text or "none",
        )
    )
    return True


def install():
    global target
    target = lldb.debugger.GetSelectedTarget()
    try:
        os.unlink(MANIFEST)
    except OSError:
        pass

    def implementation(class_name, selector):
        value = _expression(
            "(unsigned long long)((void *(*)(void *, void *))"
            "class_getMethodImplementation)(((void *(*)(const char *))"
            "objc_getClass)(\"{}\"), ((void *(*)(const char *))"
            "sel_registerName)(\"{}\"))".format(class_name, selector)
        )
        return value.GetValueAsUnsigned()

    pipeline_address = implementation(
        "MTLSerializerRenderCommandEncoder", "setRenderPipelineState:"
    )
    texture_address = implementation(
        "MTLSerializerRenderCommandEncoder", "setFragmentTexture:atIndex:"
    )
    pipeline_breakpoint = target.BreakpointCreateByAddress(pipeline_address)
    pipeline_breakpoint.SetScriptCallbackFunction("pvgfinal.on_pipeline")
    texture_breakpoint = target.BreakpointCreateByAddress(texture_address)
    texture_breakpoint.SetScriptCallbackFunction("pvgfinal.on_fragment_texture")
    completion_breakpoint = target.BreakpointCreateByName(
        "_ioAccelDisplayPipeTransactionNotifyCallback"
    )
    completion_breakpoint.SetScriptCallbackFunction(
        "pvgfinal.on_display_completion"
    )
    print(
        "PVG final-copy trace installed pipeline=0x{:x}/{} texture=0x{:x}/{} "
        "completion={}".format(
            pipeline_address,
            pipeline_breakpoint.GetNumLocations(),
            texture_address,
            texture_breakpoint.GetNumLocations(),
            completion_breakpoint.GetNumLocations(),
        )
    )
