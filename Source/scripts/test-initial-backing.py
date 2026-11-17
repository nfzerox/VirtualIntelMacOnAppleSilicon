#!/usr/bin/env python3
"""Actual policy/import/upload tests using RAM and GL call stubs; no graphics or VM.

Adapted from LightClick (MIT), commit52d12048. Its notice is bundled with
HybridHypervisor; these tests exercise our production functions, not copies.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def function(text, name):
    m = re.search(r"(?m)^static [^\n]*\b" + re.escape(name) + r"\([^;]*?\)\s*\{", text)
    assert m, name
    at = m.end()
    depth = 1
    while depth:
        depth += (text[at] == "{") - (text[at] == "}")
        at += 1
    return text[m.start():at]

def source_checks():
    host = (ROOT / "PassthroughOpenGL/passthrough-opengl-host.c").read_text()
    alias = function(host, "DGBindDrawableTexture")
    assert alias.index("VIMInitialBackingExpose") < re.search(r"if\s*\(!c->drawable", alias).start()
    pixels = function(host, "DGDrawablePixels")
    assert pixels.index("VIMInitialBackingClaim") < pixels.index("DGImportDrawableSurface")
    compact = re.sub(r"\s+", "", pixels)
    assert compact.index("DGInitialBackingUpload") < compact.index("glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture)", compact.index("DGInitialBackingUpload"))
    helper = (ROOT / "PassthroughOpenGL/initial-backing-host.c").read_text()
    for forbidden in ("glGet", "glRead", "glFlush", "glFinish", "fprintf", "UNPROVEN", "2550", "494"):
        assert forbidden not in helper.replace("glGetError", "existing_error_query_comment"), forbidden
    policy = (ROOT / "PassthroughOpenGL/initial-backing-policy.h").read_text()
    assert "2550" not in policy and "494" not in policy
    # New attachments import synchronously before returning to the client.
    guest = (ROOT / "PassthroughOpenGL/cgs-drawable.c").read_text()
    at = re.search(r"CGLError\s+e\s*=\s*VIMSetPrivateDrawableMode\(c,\s*d->width,\s*d->height,\s*pixels,\s*prepared\)", guest).start()
    transaction = guest[at:guest.index("static CGLError VIMCreateNativeWindowDrawable", at)]
    assert transaction.index("VIMReplaceDrawable") < transaction.index("return VIMImportAcceleratorBacking")

PREFIX = r'''
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include POLICY
#define DG_MAX_BYTES 100000000u
#define kIOReturnSuccess 0
#define GL_TEXTURE_RECTANGLE_ARB 1
#define GL_FALSE 0
#define GL_BGRA 2
#define GL_UNSIGNED_INT_8_8_8_8_REV 3
#define GL_INVALID_OPERATION 4
#define GL_OUT_OF_MEMORY 5
#define GL_PIXEL_PACK_BUFFER 6
#define GL_PIXEL_UNPACK_BUFFER 7
#define GL_PIXEL_PACK_BUFFER_BINDING 8
#define GL_PIXEL_UNPACK_BUFFER_BINDING 9
#define GL_PACK_ROW_LENGTH 10
#define GL_UNPACK_ROW_LENGTH 11
#define GL_PACK_ALIGNMENT 12
#define GL_UNPACK_ALIGNMENT 13
#define GL_PACK_SKIP_ROWS 14
#define GL_UNPACK_SKIP_ROWS 15
#define GL_PACK_SKIP_PIXELS 16
#define GL_UNPACK_SKIP_PIXELS 17
#define GL_PACK_ROW_BYTES_APPLE 18
#define GL_UNPACK_ROW_BYTES_APPLE 19
#define GL_PACK_SWAP_BYTES 20
#define GL_UNPACK_SWAP_BYTES 21
#define GL_PACK_LSB_FIRST 22
#define GL_UNPACK_LSB_FIRST 23
#define GL_READ_FRAMEBUFFER_BINDING_EXT 24
#define GL_READ_FRAMEBUFFER_EXT 25
#define GL_READ_BUFFER 26
#define GL_COLOR_ATTACHMENT0_EXT 27
#define GL_TEXTURE_BINDING_RECTANGLE_ARB 28
#define VIM_TRANSITION_FRONT_SEED 1
#define DG_READ_DRAWABLE 1
#define DG_READ_DRAWABLE_RECT 2
#define DG_IMPORT_DRAWABLE_PATCH 3
#define DG_IMPORT_DRAWABLE_VIEW 4
#define VIM_GL_PIXEL_BGRA8 0
#define VIM_GL_PIXEL_RGBA16 1
typedef int kern_return_t;
typedef int GLint;
typedef unsigned GLuint;
typedef int GLsizei;
typedef unsigned GLenum;
struct Surface {size_t w,h,pitch,bytes;unsigned char data[64];int lock,unlock,missing_base;unsigned planes;};
typedef struct Surface *IOSurfaceRef;
struct DGHostDrawable {unsigned name;int width,height,alias_dirty;uint64_t write_serial;struct VIMInitialBackingState initial_backing;};
struct DGHostContext {struct DGHostDrawable *drawable;IOSurfaceRef color_storage,back_storage;unsigned color,back,framebuffer,inside_begin,read_debug_count;int double_buffer,error;};
struct DGMessage {unsigned op,bytes;uint64_t args[16];};
struct DGColorTransfer {int unused;};
static int dg_surface_import=1,dg_transfer_trace=0;
static unsigned texture=99,upload_count,flush_count;
static GLint state[40],saved_state[40];
static int alloc_fail,back_upload_error,gl_error,restored;
static struct DGHostContext *active;
static void *test_malloc(size_t bytes){return alloc_fail?NULL:malloc(bytes);}
#define malloc test_malloc
static unsigned IOSurfaceGetPlaneCount(IOSurfaceRef s){return s->planes;}
static unsigned IOSurfaceGetBytesPerElement(IOSurfaceRef s){(void)s;return 4;}
static unsigned IOSurfaceGetPixelFormat(IOSurfaceRef s){(void)s;return 0x42475241;}
static size_t IOSurfaceGetWidth(IOSurfaceRef s){return s->w;}
static size_t IOSurfaceGetHeight(IOSurfaceRef s){return s->h;}
static size_t IOSurfaceGetBytesPerRow(IOSurfaceRef s){return s->pitch;}
static size_t IOSurfaceGetAllocSize(IOSurfaceRef s){return s->bytes;}
static int IOSurfaceLock(IOSurfaceRef s,unsigned f,void *v){(void)f;(void)v;return s->lock;}
static int IOSurfaceUnlock(IOSurfaceRef s,unsigned f,void *v){(void)f;(void)v;return s->unlock;}
static void *IOSurfaceGetBaseAddress(IOSurfaceRef s){return s->missing_base?NULL:s->data;}
static void glFlush(void){++flush_count;}
static void glGetIntegerv(unsigned k,GLint *v){*v=k==GL_TEXTURE_BINDING_RECTANGLE_ARB?(int)texture:state[k];}
static void glBindTexture(unsigned t,unsigned n){(void)t;texture=n;}
static void glBindBuffer(unsigned t,unsigned n){state[t==GL_PIXEL_UNPACK_BUFFER?GL_PIXEL_UNPACK_BUFFER_BINDING:GL_PIXEL_PACK_BUFFER_BINDING]=(GLint)n;}
static void glPixelStorei(unsigned t,int n){state[t]=n;}
static void glBindFramebufferEXT(unsigned t,unsigned n){(void)t;(void)n;}
static void glReadBuffer(unsigned n){(void)n;}
static void glReadPixels(int x,int y,int w,int h,unsigned f,unsigned t,void *p){(void)x;(void)y;(void)w;(void)h;(void)f;(void)t;(void)p;}
static void glTexSubImage2D(unsigned target,int level,int x,int y,int w,int h,unsigned f,unsigned t,const void *p){
 (void)target;(void)level;(void)f;(void)t;++upload_count;
 assert(!state[GL_PIXEL_UNPACK_BUFFER_BINDING] && !state[GL_UNPACK_ROW_LENGTH] && state[GL_UNPACK_ALIGNMENT]==4);
 assert(!state[GL_UNPACK_SKIP_ROWS] && !state[GL_UNPACK_SKIP_PIXELS] && !state[GL_UNPACK_ROW_BYTES_APPLE]);
 assert(!state[GL_UNPACK_SWAP_BYTES] && !state[GL_UNPACK_LSB_FIRST]);
 IOSurfaceRef s=texture==active->back?active->back_storage:active->color_storage;
 if(texture==active->back && back_upload_error){gl_error=1234;return;}
 for(int row=0;row<h;++row)memcpy(s->data+(y+row)*s->pitch+x*4,(const unsigned char *)p+row*w*4,(size_t)w*4);
}
static void DGCanonicalColorTransfer(struct DGHostContext *c,struct DGColorTransfer *s){(void)c;(void)s;}
static void DGRestoreColorTransfer(struct DGColorTransfer *s){(void)s;++restored;}
static unsigned VIMGLPixelBytes(unsigned format){return format?8:4;}
static unsigned DGSurfaceGLFormat(unsigned format){(void)format;return GL_BGRA;}
static unsigned DGSurfaceGLType(unsigned format){(void)format;return GL_UNSIGNED_INT_8_8_8_8_REV;}
#include HELPER
'''

MAIN = r'''
static int claim(struct VIMInitialBackingState *s,int enabled,int dbl,uintptr_t front,uintptr_t back,unsigned ft,unsigned bt,int w,int h,uint64_t serial,int x,int y,int iw,int ih){return VIMInitialBackingClaim(enabled,s,dbl,front,back,ft,bt,w,h,serial,x,y,iw,ih);}
static void guards(void){
 assert(VIMInitialBackingEnabled(NULL));assert(!VIMInitialBackingEnabled("0"));
 assert(VIMInitialBackingEnabled(""));assert(VIMInitialBackingEnabled("false"));assert(VIMInitialBackingEnabled("00"));
 struct VIMInitialBackingState s={0};
 assert(!claim(&s,0,1,1,2,3,4,3,2,0,0,0,3,2));
 assert(!claim(&s,1,0,1,2,3,4,3,2,0,0,0,3,2));
 assert(!claim(&s,1,1,0,2,3,4,3,2,0,0,0,3,2));
 assert(!claim(&s,1,1,1,1,3,4,3,2,0,0,0,3,2));
 assert(!claim(&s,1,1,1,2,0,4,3,2,0,0,0,3,2));
 assert(!claim(&s,1,1,1,2,3,0,3,2,0,0,0,3,2));
 assert(!claim(&s,1,1,1,2,3,3,3,2,0,0,0,3,2));
 assert(!claim(&s,1,1,1,2,3,4,0,2,0,0,0,0,2));
 assert(!claim(&s,1,1,1,2,3,4,3,2,1,0,0,3,2));
 assert(!claim(&s,1,1,1,2,3,4,3,2,0,1,0,2,2));
 assert(!s.attempted);VIMInitialBackingExpose(0,&s);assert(!s.back_exposed);
 VIMInitialBackingExpose(1,&s);assert(!claim(&s,1,1,1,2,3,4,3,2,0,0,0,3,2));
 s=(struct VIMInitialBackingState){0};assert(claim(&s,1,1,1,2,3,4,3,2,0,0,0,3,2));
 /* Failed initialization or serial wrap must not permit another seed. */
 assert(!claim(&s,1,1,1,2,3,4,3,2,0,0,0,3,2));
}
struct Payload {struct DGMessage m;unsigned char pixels[24];};
static struct Surface front,back;
static struct DGHostDrawable drawable;
static struct DGHostContext context;
static struct Payload payload;
static void reset(int reverse){
 front=(struct Surface){.w=2,.h=2,.pitch=16,.bytes=32};back=front;
 memset(front.data,98,sizeof(front.data));memset(back.data,99,sizeof(back.data));
 drawable=(struct DGHostDrawable){.name=7,.width=2,.height=2};
 context=(struct DGHostContext){.drawable=&drawable,.color_storage=&front,.back_storage=&back,.color=10,.back=11,.double_buffer=1,.error=77};active=&context;
 payload=(struct Payload){.m={.op=DG_IMPORT_DRAWABLE_VIEW,.bytes=20,.args={2,2,12,0}}};payload.m.args[3]=reverse;
 for(unsigned i=0;i<24;++i)payload.pixels[i]=(unsigned char)i;
 upload_count=flush_count=alloc_fail=back_upload_error=gl_error=restored=0;texture=99;dg_surface_import=1;
 for(unsigned i=0;i<40;++i)state[i]=(GLint)(100+i);memcpy(saved_state,state,sizeof(state));
}
static void rows_match(struct Surface *s,int reverse){assert(!memcmp(s->data,payload.pixels+(reverse?12:0),8));assert(!memcmp(s->data+16,payload.pixels+(reverse?0:12),8));}
int main(void){
 guards();int enabled=DGInitialBackingEnabled();
 for(int reverse=0;reverse<2;++reverse){
  reset(reverse);assert(!DGDrawablePixels(&context,&payload.m,0));rows_match(&front,reverse);
  if(enabled){rows_match(&back,reverse);assert(drawable.initial_backing.attempted);}else assert(back.data[0]==99);
  assert(front.data[8]==98 && back.data[8]==99 && drawable.write_serial==1 && !upload_count && context.error==77);
  unsigned char saved[64];memcpy(saved,back.data,64);memset(payload.pixels,42,24);
  assert(!DGDrawablePixels(&context,&payload.m,0));assert(!memcmp(saved,back.data,64));assert(drawable.write_serial==2);
 }
 reset(0);VIMInitialBackingExpose(enabled,&drawable.initial_backing);assert(!DGDrawablePixels(&context,&payload.m,0));assert(back.data[0]==99);
 reset(0);context.double_buffer=0;assert(!DGDrawablePixels(&context,&payload.m,0));assert(back.data[0]==99 && !drawable.initial_backing.attempted);
 reset(0);payload.m.op=DG_IMPORT_DRAWABLE_PATCH;payload.m.args[1]=1;payload.m.args[2]=0;payload.m.bytes=8;
 assert(!DGDrawablePixels(&context,&payload.m,0));assert(back.data[0]==99 && !drawable.initial_backing.attempted && drawable.write_serial==1);
 payload.m.op=DG_IMPORT_DRAWABLE_VIEW;payload.m.args[1]=2;payload.m.args[2]=12;payload.m.bytes=20;
 assert(!DGDrawablePixels(&context,&payload.m,0));assert(back.data[0]==99 && !drawable.initial_backing.attempted);
 if(enabled){
  for(int failure=0;failure<4;++failure){
   reset(1);if(failure==0)back.lock=1;if(failure==1)back.unlock=1;if(failure==2)back.planes=1;if(failure==3)back.missing_base=1;
   assert(!DGDrawablePixels(&context,&payload.m,0));rows_match(&front,1);rows_match(&back,1);
   assert(upload_count==2 && texture==99 && restored==1 && drawable.write_serial==1 && context.error==77);assert(!memcmp(state,saved_state,sizeof(state)));
  }
  reset(1);front.lock=1;assert(!DGDrawablePixels(&context,&payload.m,0));rows_match(&front,1);rows_match(&back,1);assert(upload_count==2 && drawable.write_serial==1 && context.error==77);
  reset(0);front.unlock=1;assert(!DGDrawablePixels(&context,&payload.m,0));assert(!upload_count && back.data[0]==99 && context.error==GL_INVALID_OPERATION);
  reset(0);front.missing_base=1;assert(!DGDrawablePixels(&context,&payload.m,0));assert(!upload_count && back.data[0]==99 && context.error==GL_INVALID_OPERATION);
  reset(1);back.lock=1;alloc_fail=1;assert(!DGDrawablePixels(&context,&payload.m,0));rows_match(&front,1);assert(back.data[0]==99 && !upload_count && drawable.write_serial==1 && context.error==77);
  reset(1);back.lock=1;back_upload_error=1;assert(!DGDrawablePixels(&context,&payload.m,0));assert(gl_error==1234 && context.error==77 && texture==99);
  reset(1);dg_surface_import=0;alloc_fail=1;assert(!DGDrawablePixels(&context,&payload.m,0));assert(context.error==GL_OUT_OF_MEMORY && !upload_count);
  reset(1);dg_surface_import=0;assert(!DGDrawablePixels(&context,&payload.m,0));rows_match(&front,1);rows_match(&back,1);assert(upload_count==2 && drawable.write_serial==1 && texture==99);
 }
 puts("PASS initial backing policy and actual import/fallback paths");return 0;
}
'''

def emit(output):
    source_checks()
    import json
    directory=ROOT / "PassthroughOpenGL"
    source=(directory / "passthrough-opengl-host.c").read_text()
    prefix=PREFIX.replace("POLICY",json.dumps(str(directory / "initial-backing-policy.h"))).replace("HELPER",json.dumps(str(directory / "initial-backing-host.c")))
    output.write_text(prefix+"\n"+function(source,"DGImportDrawableSurface")+"\n"+function(source,"DGDrawablePixels")+"\n"+MAIN)

def check(binary):
    source_checks()
    for value in (None,"0","1","00","false"):
        env=dict(os.environ);env.pop("VIM_INITIAL_BACK_SEED",None)
        if value is not None:env["VIM_INITIAL_BACK_SEED"]=value
        result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=10,check=True)
        assert result.stderr=="",result.stderr
        assert result.stdout=="PASS initial backing policy and actual import/fallback paths\n",result.stdout
    print("PASS 5 environment cases; no graphics, stderr or per-event logging")

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit",type=Path);parser.add_argument("--check",type=Path)
    args=parser.parse_args()
    if args.emit:emit(args.emit)
    elif args.check:check(args.check)
    else:source_checks();print("PASS source/order checks")
