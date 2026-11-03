/* A guest IOSurface snapshot becomes a real native host IOSurface texture.
 * This is a GL/CGL contract, not compositor/application-specific knowledge.
 * Retain bounded backing with the GL share group, reuse it on CPU updates,
 * and retire it on successful texture redefinition/delete or disconnect.
 * In particular RGB sampling is opaque while native attachment reads retain
 * backing alpha; glTexImage2D(GL_RGB8) alone cannot represent that contract.
 */
#include "iosurface-element-layout.h"
static int DGIOSurfaceTexture(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    uint64_t width=m->args[1],rows=m->args[2],stride=m->args[3];
    uint64_t height=m->args[4]?m->args[4]:rows,start=m->args[5];
    GLenum internal=(GLenum)m->args[0];
    int packed=m->op==DG_IOSURFACE_TEXTURE_PACKED;
    GLenum format=packed?(GLenum)m->args[6]:DGIOSurfaceInternalGLFormat(internal);
    GLenum type=packed?(GLenum)m->args[7]:DGIOSurfaceGLType(DGIOSurfaceElementBytes(internal));
    unsigned element_bytes=DGIOSurfaceTransferBytes(internal,format,type);
    unsigned pixel_format=DGIOSurfaceTransferPixelFormat(internal,format,type);
    if(m->flags!=(DG_REPLY|DG_INPUT_ONLY) || !width || !height || !rows || width>8192 || height>8192 ||
       start>=height || rows>height-start || (!m->args[4] && start) ||
       !element_bytes || (packed&&(!DGIOSurfacePacked422(internal,format,type)||(width&1))) ||
       stride<width*element_bytes || stride>DG_MAX_BYTES || stride%element_bytes ||
       (rows-1)*stride+width*element_bytes!=m->bytes)return -1;
    if(c->inside_begin){reply->error=GL_INVALID_OPERATION;return 0;}
    GLint texture=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    struct DGAliasGroup *g=c->alias_group;
    if(!texture || !g){reply->error=GL_INVALID_OPERATION;return 0;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    struct DGAlias *alias=g->aliases;
    for(;alias && alias->texture!=(GLuint)texture;alias=alias->next){}
    IOSurfaceRef surface=alias?alias->owned_storage:NULL;int reuse=surface &&
        IOSurfaceGetWidth(surface)==width && IOSurfaceGetHeight(surface)==height &&
        IOSurfaceGetBytesPerElement(surface)==element_bytes&&IOSurfaceGetPixelFormat(surface)==pixel_format;
    if(!reuse){
        uint64_t pitch=(width*element_bytes+63)&~63ull,bytes=pitch*height;
        if(bytes>DG_IOSURFACE_MAX_BYTES || bytes>DG_IOSURFACE_GROUP_BYTES-g->owned_surface_bytes ||
           (!(c->client_features&DG_FEATURE_LARGE_IOSURFACE_WRITES)&&g->owned_surface_count>=DG_IOSURFACE_INLINE_WRITES) || start){
            reply->error=GL_OUT_OF_MEMORY;return 0;
        }
        CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        if(!props){reply->error=GL_OUT_OF_MEMORY;return 0;}
        const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
        int64_t values[]={width,height,element_bytes,pitch,bytes,
            pixel_format};int good=1;
        for(unsigned i=0;i<6;++i){
            CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,values+i);
            if(!n){good=0;break;}CFDictionarySetValue(props,keys[i],n);CFRelease(n);
        }
        surface=good?IOSurfaceCreate(props):NULL;CFRelease(props);
        if(!surface || IOSurfaceGetAllocSize(surface)>DG_IOSURFACE_MAX_BYTES ||
           IOSurfaceGetAllocSize(surface)>DG_IOSURFACE_GROUP_BYTES-g->owned_surface_bytes){
            if(surface)CFRelease(surface);reply->error=GL_OUT_OF_MEMORY;return 0;
        }
    }
    glFlush(); // Native CPU ownership rule: submit producer before CPU lock.
    kern_return_t error=IOSurfaceLock(surface,0,NULL);
    if(!error){
        unsigned char *out=IOSurfaceGetBaseAddress(surface);
        size_t pitch=IOSurfaceGetBytesPerRow(surface),allocation=IOSurfaceGetAllocSize(surface);
        if(!out || pitch<width*element_bytes || pitch>allocation/height)error=kIOReturnBadArgument;
        else {
            if(!reuse)memset(out,0,allocation);
            const unsigned char *in=(const void *)(m+1);
            for(size_t y=0;y<rows;++y)memcpy(out+(start+y)*pitch,in+y*stride,(size_t)width*element_bytes);
        }
        kern_return_t unlock=IOSurfaceUnlock(surface,0,NULL);if(!error)error=unlock;
    }
    if(!error)error=CGLTexImageIOSurface2D(c->cgl,GL_TEXTURE_RECTANGLE_ARB,internal,
        (GLsizei)width,(GLsizei)height,format,type,surface,0);
    GLenum glError=glGetError();
    if(!error && !glError){
        // Retain before replacing alias metadata; replacement may release the
        // previous owned reference to this very allocation on the reuse path.
        CFRetain(surface);
        if(DGRegisterAlias(c,(GLuint)texture,surface)){
            alias=g->aliases;while(alias->texture!=(GLuint)texture)alias=alias->next;
            alias->owned_storage=surface;g->owned_surface_bytes+=IOSurfaceGetAllocSize(surface);
            alias->iosurface_import=1;++g->iosurface_import_count;
            ++g->owned_surface_count;reply->result=1;
        }else {CFRelease(surface);reply->error=GL_OUT_OF_MEMORY;}
    }else reply->error=glError?glError:GL_INVALID_OPERATION;
    if(!reuse)CFRelease(surface);
    return 0;
}
