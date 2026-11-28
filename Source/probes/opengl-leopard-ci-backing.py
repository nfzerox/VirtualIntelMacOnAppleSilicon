#!/usr/bin/python
"""Run with Leopard's system Python/PyObjC; no SDK or third-party modules.

Tests public CGL backing-size state and actual Core Image GPU pixels. This
does not qualify window presentation, fullscreen scaling, or DVDPlayback's
additional selector-298 flag; those need separate integration testing.
"""
import ctypes as C
import AppKit as A
import Quartz as Q

pool = A.NSAutoreleasePool.alloc().init()
pixel_format = A.NSOpenGLPixelFormat.alloc().initWithAttributes_([73, 8, 24, 0])
assert pixel_format is not None, 'No accelerated pixel format'
context = A.NSOpenGLContext.alloc().initWithFormat_shareContext_(pixel_format, None)
assert context is not None, 'No accelerated context'
context.makeCurrentContext()
gl = C.CDLL('/System/Library/Frameworks/OpenGL.framework/OpenGL')
gl.CGLGetCurrentContext.restype = C.c_void_p
gl.glGetString.restype = C.c_char_p
handle = gl.CGLGetCurrentContext()
print('renderer: %s' % gl.glGetString(0x1f01))

# The two dimensions and one enable bit must round-trip independently. The
# sentinel catches a driver copying the three-word setter record to a smaller
# public getter buffer. These getters use selectors 304/305, not selector 298.
dimensions = (C.c_int * 2)(720, 480)
assert gl.CGLSetParameter(handle, 304, dimensions) == 0
assert gl.CGLEnable(handle, 305) == 0
observed = (C.c_int * 3)(0, 0, 0x12345678)
assert gl.CGLGetParameter(handle, 304, observed) == 0
assert list(observed) == [720, 480, 0x12345678], list(observed)
enabled = (C.c_int * 2)(0, 0x12345678)
assert gl.CGLIsEnabled(handle, 305, enabled) == 0
assert list(enabled) == [1, 0x12345678], list(enabled)
assert gl.CGLDisable(handle, 305) == 0
assert gl.CGLIsEnabled(handle, 305, enabled) == 0
assert list(enabled) == [0, 0x12345678], list(enabled)
print('PASS CGL backing-size set/get/enable/disable and getter sentinels')

# Core Image consumes legacy renderer property 120 as BYTES. The original
# erroneous 2047-byte budget produced "ROI is not tilable" and left the blue
# clear color untouched, even though framebuffer status and glGetError passed.
space = Q.CGColorSpaceCreateDeviceRGB()
bitmap = Q.CGBitmapContextCreate(None, 720, 480, 8, 720 * 4, space,
                               Q.kCGImageAlphaPremultipliedLast)
Q.CGContextSetRGBFillColor(bitmap, 1, 0, 0, 1)
Q.CGContextFillRect(bitmap, ((0, 0), (360, 480)))
Q.CGContextSetRGBFillColor(bitmap, 0, 1, 0, 1)
Q.CGContextFillRect(bitmap, ((360, 0), (360, 480)))
image = Q.CIImage.imageWithCGImage_(Q.CGBitmapContextCreateImage(bitmap))
ci = Q.CIContext.contextWithCGLContext_pixelFormat_options_(
    context.CGLContextObj(), pixel_format.CGLPixelFormatObj(), None)
assert ci is not None, 'No GPU CI context'
texture = C.c_uint()
framebuffer = C.c_uint()
gl.glClearColor.argtypes = [C.c_float] * 4
gl.glOrtho.argtypes = [C.c_double] * 6
try:
    gl.glGenTextures(1, C.byref(texture))
    gl.glBindTexture(0x84f5, texture)
    gl.glTexImage2D(0x84f5, 0, 0x8058, 1280, 720, 0, 0x1908, 0x1401, None)
    gl.glGenFramebuffersEXT(1, C.byref(framebuffer))
    gl.glBindFramebufferEXT(0x8d40, framebuffer)
    gl.glFramebufferTexture2DEXT(0x8d40, 0x8ce0, 0x84f5, texture, 0)
    assert gl.glCheckFramebufferStatusEXT(0x8d40) == 0x8cd5
    gl.glViewport(0, 0, 1280, 720)
    gl.glMatrixMode(0x1701)
    gl.glLoadIdentity()
    gl.glOrtho(0, 1280, 0, 720, -1, 1)
    gl.glMatrixMode(0x1700)
    gl.glLoadIdentity()
    gl.glClearColor(0, 0, 1, 1)
    gl.glClear(0x4000)
    ci.drawImage_inRect_fromRect_(image, ((0, 0), (1280, 720)),
                                ((0, 0), (720, 480)))
    gl.glFinish()
    for x in [40, 600, 800, 1240]:
        pixel = (C.c_ubyte * 4)()
        gl.glReadPixels(x, 360, 1, 1, 0x1908, 0x1401, pixel)
        assert gl.glGetError() == 0
        expected = [255, 0, 0, 255] if x < 640 else [0, 255, 0, 255]
        assert all(abs(int(pixel[i]) - expected[i]) <= 3 for i in range(4)), \
            'x=%d pixel=%s expected=%s' % (x, list(pixel), expected)
        print('PASS Core Image x=%d pixel=%s' % (x, list(pixel)))
finally:
    gl.glBindFramebufferEXT(0x8d40, 0)
    gl.glDeleteFramebuffersEXT(1, C.byref(framebuffer))
    gl.glDeleteTextures(1, C.byref(texture))
    A.NSOpenGLContext.clearCurrentContext()
