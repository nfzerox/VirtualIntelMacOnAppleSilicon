"""Trace and capture the render pass feeding WindowServer's final display copy.

The final ``TextureCopy`` draw is not necessarily the compositor itself.  This
probe follows its source texture back to the display-sized render encoder that
wrote it, records that encoder's pipelines and fragment textures, and performs
synchronized IOSurface readback after the display completion callback.  It is
address-independent across boots: all breakpoints are resolved from the live
Objective-C runtime or by symbol name.
"""

import hashlib
import lldb
import os
import re


OUTPUT_DIR = "/tmp/pvg-compositor-graph"
MANIFEST = os.path.join(OUTPUT_DIR, "manifest.txt")
TEXTURE_COPY_FRAGMENT_SHA256 = (
    "02cc334837e6cac588ac7b5394397bd7758690e75baef18129cbb7dce46e7aa5"
)
MIN_DISPLAY_WIDTH = 1000
MIN_DISPLAY_HEIGHT = 700
MAX_RETAINED_TEXTURES = 128

target = None
pipeline_info = {}
encoder_info = {}
retained_textures = set()
final_encoder = 0
final_source_texture = 0
final_source_surface = 0
completion_count = 0


def _register(frame, name):
    return frame.FindRegister(name).GetValueAsUnsigned()


def _expression(text, language=lldb.eLanguageTypeC):
    options = lldb.SBExpressionOptions()
    options.SetLanguage(language)
    options.SetIgnoreBreakpoints(True)
    options.SetTimeoutInMicroSeconds(5_000_000)
    return target.EvaluateExpression(text, options)


def _message(obj, selector):
    if not obj:
        return 0
    return _expression(
        "(unsigned long long)((void *(*)(void *, void *))objc_msgSend)"
        "((void *)0x{:x}, ((void *(*)(const char *))sel_registerName)"
        "(\"{}\"))".format(obj, selector)
    ).GetValueAsUnsigned()


def _indexed_message(obj, selector, index):
    if not obj:
        return 0
    return _expression(
        "(unsigned long long)((void *(*)(void *, void *, unsigned long long))"
        "objc_msgSend)((void *)0x{:x}, ((void *(*)(const char *))"
        "sel_registerName)(\"{}\"), {})".format(obj, selector, index)
    ).GetValueAsUnsigned()


def _unsigned_property(obj, selector):
    return _message(obj, selector)


def _surface_property(surface, function):
    if not surface:
        return 0
    return _expression(
        "(unsigned long long)((unsigned long long (*)(void *)){})"
        "((void *)0x{:x})".format(function, surface)
    ).GetValueAsUnsigned()


def _surface_for_texture(texture):
    return _unsigned_property(texture, "iosurface")


def _retain(texture):
    if not texture or texture in retained_textures:
        return
    if len(retained_textures) >= MAX_RETAINED_TEXTURES:
        return
    _expression(
        "(void)[(id)0x{:x} retain]".format(texture),
        lldb.eLanguageTypeObjC_plus_plus,
    )
    retained_textures.add(texture)


def _release_all():
    for texture in retained_textures:
        _expression(
            "(void)[(id)0x{:x} release]".format(texture),
            lldb.eLanguageTypeObjC_plus_plus,
        )
    retained_textures.clear()


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
    result = target.GetProcess().ReadCStringFromMemory(address, 256, error)
    return result if error.Success() else ""


def _fragment_resource_digest(pipeline):
    resource = _expression(
        "(unsigned long long)[(id)0x{:x} valueForKey:@\"_resource2\"]".format(
            pipeline
        ),
        lldb.eLanguageTypeObjC_plus_plus,
    ).GetValueAsUnsigned()
    if not resource:
        return ""
    address = _unsigned_property(resource, "contents")
    length = _unsigned_property(resource, "length")
    if not address or not length or length > (1 << 20):
        return ""
    error = lldb.SBError()
    data = target.GetProcess().ReadMemory(address, length, error)
    if not error.Success() or len(data) != length:
        return ""
    return hashlib.sha256(data).hexdigest()


def _pipeline(pipeline):
    pipeline_ref = _unsigned_property(pipeline, "pipelineRef")
    key = pipeline_ref or pipeline
    if key not in pipeline_info:
        name = _fragment_name(pipeline)
        digest = _fragment_resource_digest(pipeline)
        if not name and digest == TEXTURE_COPY_FRAGMENT_SHA256:
            name = "TextureCopy"
        pipeline_info[key] = {
            "object": pipeline,
            "ref": pipeline_ref,
            "name": name,
            "digest": digest,
        }
    return key, pipeline_info[key]


def _texture_description(texture):
    surface = _surface_for_texture(texture)
    return {
        "object": texture,
        "surface": surface,
        "surface_id": _surface_property(surface, "IOSurfaceGetID"),
        "width": _unsigned_property(texture, "width"),
        "height": _unsigned_property(texture, "height"),
        "format": _unsigned_property(texture, "pixelFormat"),
        "storage": _unsigned_property(texture, "storageMode"),
        "usage": _unsigned_property(texture, "usage"),
    }


def _append(line):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    with open(MANIFEST, "a") as output:
        output.write(line + "\n")


def on_encoder_init(frame, _location, _dict):
    encoder = _register(frame, "rdi")
    descriptor = _register(frame, "rcx")
    attachments = _message(descriptor, "colorAttachments")
    attachment = _indexed_message(attachments, "objectAtIndexedSubscript:", 0)
    texture = _message(attachment, "texture")
    if not texture:
        return False
    texture_info = _texture_description(texture)
    if (
        texture_info["width"] < MIN_DISPLAY_WIDTH
        or texture_info["height"] < MIN_DISPLAY_HEIGHT
    ):
        return False
    _retain(texture)
    encoder_info[encoder] = {
        "target": texture_info,
        "load": _unsigned_property(attachment, "loadAction"),
        "store": _unsigned_property(attachment, "storeAction"),
        "pipeline": 0,
        "pipelines": [],
        "inputs": [],
    }
    _append(
        "encoder=0x{:x} targetTexture=0x{:x} targetSurfaceID={} dimensions={}x{} "
        "format=0x{:x} storage={} usage=0x{:x} load={} store={}".format(
            encoder,
            texture,
            texture_info["surface_id"],
            texture_info["width"],
            texture_info["height"],
            texture_info["format"],
            texture_info["storage"],
            texture_info["usage"],
            encoder_info[encoder]["load"],
            encoder_info[encoder]["store"],
        )
    )
    return False


def on_pipeline(frame, _location, _dict):
    encoder = _register(frame, "rdi")
    if encoder not in encoder_info:
        return False
    pipeline = _register(frame, "rdx")
    key, info = _pipeline(pipeline)
    record = encoder_info[encoder]
    record["pipeline"] = key
    if key not in record["pipelines"]:
        record["pipelines"].append(key)
        _append(
            "encoder=0x{:x} pipelineRef=0x{:x} object=0x{:x} fragment={} "
            "resource2={}".format(
                encoder,
                info["ref"],
                info["object"],
                info["name"] or "(unknown)",
                info["digest"] or "(unavailable)",
            )
        )
    return False


def on_fragment_texture(frame, _location, _dict):
    global final_encoder, final_source_texture, final_source_surface

    encoder = _register(frame, "rdi")
    if encoder not in encoder_info:
        return False
    texture = _register(frame, "rdx")
    index = _register(frame, "rcx")
    if not texture:
        return False
    _retain(texture)
    texture_info = _texture_description(texture)
    pipeline_key = encoder_info[encoder]["pipeline"]
    encoder_info[encoder]["inputs"].append(
        {"index": index, "pipeline": pipeline_key, "texture": texture_info}
    )
    pipeline = pipeline_info.get(pipeline_key, {})
    _append(
        "encoder=0x{:x} inputIndex={} pipelineRef=0x{:x} texture=0x{:x} "
        "surfaceID={} dimensions={}x{} format=0x{:x} storage={} usage=0x{:x}".format(
            encoder,
            index,
            pipeline.get("ref", 0),
            texture,
            texture_info["surface_id"],
            texture_info["width"],
            texture_info["height"],
            texture_info["format"],
            texture_info["storage"],
            texture_info["usage"],
        )
    )
    if (
        pipeline.get("name") == "TextureCopy"
        and index == 0
        and texture_info["width"] >= MIN_DISPLAY_WIDTH
        and texture_info["height"] >= MIN_DISPLAY_HEIGHT
    ):
        final_encoder = encoder
        final_source_texture = texture
        final_source_surface = texture_info["surface"]
        _append(
            "finalCopy encoder=0x{:x} sourceTexture=0x{:x} sourceSurfaceID={}".format(
                encoder, texture, texture_info["surface_id"]
            )
        )
    return False


def _capture(label, texture_info):
    surface = texture_info["surface"]
    if not surface:
        _append("capture label={} hasNoIOSurface texture=0x{:x}".format(
            label, texture_info["object"]
        ))
        return
    surface_id = _surface_property(surface, "IOSurfaceGetID")
    width = _surface_property(surface, "IOSurfaceGetWidth")
    height = _surface_property(surface, "IOSurfaceGetHeight")
    bpr = _surface_property(surface, "IOSurfaceGetBytesPerRow")
    size = _surface_property(surface, "IOSurfaceGetAllocSize")
    lock = _expression(
        "(int)((int (*)(void *, unsigned int, unsigned int *))IOSurfaceLock)"
        "((void *)0x{:x}, 1, (unsigned int *)0)".format(surface)
    ).GetValueAsSigned()
    base = _surface_property(surface, "IOSurfaceGetBaseAddress")
    safe_label = re.sub(r"[^A-Za-z0-9_.-]", "-", label)
    path = os.path.join(
        OUTPUT_DIR,
        "{}-id{}-{}x{}-bpr{}.bin".format(
            safe_label, surface_id, width, height, bpr
        ),
    )
    copied = 0
    error_text = ""
    if lock == 0 and base and size:
        error = lldb.SBError()
        data = target.GetProcess().ReadMemory(base, size, error)
        if error.Success() and len(data) == size:
            with open(path, "wb") as output:
                output.write(data)
            copied = len(data)
        else:
            error_text = error.GetCString() or "short read"
    if lock == 0:
        _expression(
            "(int)((int (*)(void *, unsigned int, unsigned int *))"
            "IOSurfaceUnlock)((void *)0x{:x}, 1, (unsigned int *)0)".format(
                surface
            )
        )
    _append(
        "capture label={} texture=0x{:x} surfaceID={} dimensions={}x{} bpr={} "
        "format=0x{:x} storage={} usage=0x{:x} lock={} base=0x{:x} size={} "
        "copied={} path={} error={}".format(
            label,
            texture_info["object"],
            surface_id,
            width,
            height,
            bpr,
            texture_info["format"],
            texture_info["storage"],
            texture_info["usage"],
            lock,
            base,
            size,
            copied,
            path,
            error_text or "none",
        )
    )


def on_display_completion(frame, _location, _dict):
    global completion_count
    if not final_source_texture:
        return False
    completion_count += 1
    producers = []
    for encoder, record in encoder_info.items():
        texture = record["target"]
        if encoder == final_encoder:
            continue
        if texture["object"] == final_source_texture or (
            final_source_surface and texture["surface"] == final_source_surface
        ):
            producers.append((encoder, record))
    if not producers and completion_count < 8:
        _append(
            "completion={} final source has no traced render producer yet; waiting".format(
                completion_count
            )
        )
        return False

    _append(
        "completion={} finalEncoder=0x{:x} producerCount={}".format(
            completion_count, final_encoder, len(producers)
        )
    )
    final_record = encoder_info.get(final_encoder)
    if final_record:
        _capture("final-target", final_record["target"])
    _capture("final-source", _texture_description(final_source_texture))
    for producer_index, (encoder, record) in enumerate(producers):
        _append(
            "producer index={} encoder=0x{:x} pipelineCount={} inputCount={}".format(
                producer_index,
                encoder,
                len(record["pipelines"]),
                len(record["inputs"]),
            )
        )
        _capture("producer-{}-target".format(producer_index), record["target"])
        seen = set()
        for input_index, entry in enumerate(record["inputs"]):
            texture_info = entry["texture"]
            identity = texture_info["surface"] or texture_info["object"]
            if identity in seen:
                continue
            seen.add(identity)
            _capture(
                "producer-{}-input-{}-slot{}".format(
                    producer_index, input_index, entry["index"]
                ),
                texture_info,
            )
    _release_all()
    return True


def install():
    global target
    target = lldb.debugger.GetSelectedTarget()
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    try:
        os.unlink(MANIFEST)
    except OSError:
        pass

    def implementation(class_name, selector):
        return _expression(
            "(unsigned long long)((void *(*)(void *, void *))"
            "class_getMethodImplementation)(((void *(*)(const char *))"
            "objc_getClass)(\"{}\"), ((void *(*)(const char *))"
            "sel_registerName)(\"{}\"))".format(class_name, selector)
        ).GetValueAsUnsigned()

    callbacks = (
        (
            "MTLSerializerRenderCommandEncoder",
            "initWithCommandBuffer:descriptor:",
            "pvggraph.on_encoder_init",
        ),
        (
            "MTLSerializerRenderCommandEncoder",
            "setRenderPipelineState:",
            "pvggraph.on_pipeline",
        ),
        (
            "MTLSerializerRenderCommandEncoder",
            "setFragmentTexture:atIndex:",
            "pvggraph.on_fragment_texture",
        ),
    )
    installed = []
    for class_name, selector, callback in callbacks:
        address = implementation(class_name, selector)
        breakpoint = target.BreakpointCreateByAddress(address)
        breakpoint.SetScriptCallbackFunction(callback)
        installed.append((selector, address, breakpoint.GetNumLocations()))
    completion = target.BreakpointCreateByName(
        "_ioAccelDisplayPipeTransactionNotifyCallback"
    )
    completion.SetScriptCallbackFunction("pvggraph.on_display_completion")
    print(
        "PVG compositor graph installed {} completion={}".format(
            installed, completion.GetNumLocations()
        )
    )
