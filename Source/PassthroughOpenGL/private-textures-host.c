/* A private texture may occupy a native GL name which the application has
 * never generated or bound. Evict that registration BEFORE a public implicit
 * bind creates the application object. Guest names remain native names: no
 * translation of queries, shared objects or compiled display-list commands.
 *
 * Qualified storage is IOSurface-backed rectangle level0. Relocation reimports
 * the SAME storage and updates only registered PRIVATE framebuffer attachments.
 * No pixel readback, precision conversion, new IOSurface or per-draw copying.
 * A producer finish is restricted to the exceptional first-name collision.
 * Callers serialize one GL share group as the existing endpoint already does.
 * This module is bring-up infrastructure until integrated and qualified by the
 * executor; its standalone probe is not desktop conformance evidence.
 */
#include "private-textures-host.h"
#include <OpenGL/CGLIOSurface.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>

struct DGPrivateAttachment {
    CGLContextObj context;GLuint framebuffer;GLenum attachment;GLint level;
    struct DGPrivateAttachment *next;
};
struct DGPrivateTexture {
    struct DGPrivateTextures *registry;GLuint name;CGLContextObj producer;
    IOSurfaceRef storage;GLenum internal,format,type;GLsizei width,height;
    struct DGPrivateAttachment *attachments;
    struct DGPrivateTextureWatch *watches;
    struct DGPrivateTexture *next;
};
struct DGPrivateTextures {
    unsigned refs,count,failures;int tracking_failed;CGLShareGroupObj group;CGLContextObj relocator;
    struct DGPrivateTexture *buckets[128];
    struct DGPrivateTextures *next;
};
static pthread_mutex_t dg_private_registry_lock;
static pthread_once_t dg_private_registry_once=PTHREAD_ONCE_INIT;
static void DGPrivateRegistryInitialize(void)
{
    pthread_mutexattr_t attributes;pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&dg_private_registry_lock,&attributes);pthread_mutexattr_destroy(&attributes);
}
static void DGPrivateGuardRelease(int *unused)
{(void)unused;pthread_mutex_unlock(&dg_private_registry_lock);}
#define DG_PRIVATE_GUARD pthread_once(&dg_private_registry_once,DGPrivateRegistryInitialize); \
    pthread_mutex_lock(&dg_private_registry_lock); \
    int dg_private_guard __attribute__((cleanup(DGPrivateGuardRelease)))=0
static struct DGPrivateTextures *dg_private_registries;
/* A CGL group belongs to one serialized endpoint. The list lock protects
 * different endpoints creating/retiring groups, not application drawing. */
struct DGPrivateTextures *DGPrivateTexturesCurrentRegistry(void)
{
    DG_PRIVATE_GUARD;
    CGLContextObj c=CGLGetCurrentContext();if(!c)return NULL;
    CGLShareGroupObj group=CGLGetShareGroup(c);
    pthread_mutex_lock(&dg_private_registry_lock);
    struct DGPrivateTextures *r=dg_private_registries;
    while(r&&r->group!=group)r=r->next;
    pthread_mutex_unlock(&dg_private_registry_lock);return r;
}
static unsigned DGPrivateTextureBucket(GLuint name)
{return (unsigned)((name*UINT32_C(2654435761))>>25);}
static int DGPrivateTexturesCurrent(const struct DGPrivateTextures *r)
{CGLContextObj c=CGLGetCurrentContext();return r&&c&&r->group==CGLGetShareGroup(c);}
static struct DGPrivateTexture *DGPrivateTextureFind(struct DGPrivateTextures *r,GLuint name)
{
    if(!r||!name)return NULL;
    for(struct DGPrivateTexture *p=r->buckets[DGPrivateTextureBucket(name)];p;p=p->next)
        if(p->name==name)return p;
    return NULL;
}
struct DGPrivateTexture *DGPrivateTextureLookup(struct DGPrivateTextures *r,GLuint name)
{DG_PRIVATE_GUARD;return DGPrivateTextureFind(r,name);}
struct DGPrivateTextures *DGPrivateTexturesCreate(void)
{
    DG_PRIVATE_GUARD;
    CGLContextObj c=CGLGetCurrentContext();if(!c)return NULL;
    struct DGPrivateTextures *r=calloc(1,sizeof(*r));
    if(r){r->refs=1;r->group=CGLGetShareGroup(c);
        pthread_mutex_lock(&dg_private_registry_lock);
        r->next=dg_private_registries;dg_private_registries=r;
        pthread_mutex_unlock(&dg_private_registry_lock);}return r;
}
void DGPrivateTexturesRetain(struct DGPrivateTextures *r){DG_PRIVATE_GUARD;if(r)++r->refs;}
void DGPrivateTexturesRelease(struct DGPrivateTextures *r)
{
    DG_PRIVATE_GUARD;
    if(!r||--r->refs)return;
    // Textures retain their registry; this cannot free live registrations.
    pthread_mutex_lock(&dg_private_registry_lock);
    struct DGPrivateTextures **link=&dg_private_registries;
    while(*link&&*link!=r)link=&(*link)->next;
    if(*link)*link=r->next;
    pthread_mutex_unlock(&dg_private_registry_lock);
    if(r->relocator)CGLReleaseContext(r->relocator);free(r);
}
struct DGPrivateTexture *DGPrivateTextureRegister(struct DGPrivateTextures *r,GLuint name,
    IOSurfaceRef storage,GLenum internal,GLsizei width,GLsizei height,GLenum format,GLenum type)
{
    DG_PRIVATE_GUARD;
    if(!DGPrivateTexturesCurrent(r)||!name||!storage||width<1||height<1||
       DGPrivateTextureFind(r,name)||r->count>=16384)return NULL;
    struct DGPrivateTexture *p=calloc(1,sizeof(*p));if(!p)return NULL;
    p->registry=r;p->name=name;p->producer=CGLRetainContext(CGLGetCurrentContext());
    p->storage=(IOSurfaceRef)CFRetain(storage);p->internal=internal;p->width=width;p->height=height;
    p->format=format;p->type=type;
    unsigned bucket=DGPrivateTextureBucket(name);p->next=r->buckets[bucket];r->buckets[bucket]=p;
    ++r->count;DGPrivateTexturesRetain(r);return p;
}
GLuint DGPrivateTextureName(const struct DGPrivateTexture *p){return p?p->name:0;}
void DGPrivateTextureWatchForget(struct DGPrivateTextureWatch *w)
{
    DG_PRIVATE_GUARD;
    if(!w||!w->texture)return;
    if(w->previous)w->previous->next=w->next;else w->texture->watches=w->next;
    if(w->next)w->next->previous=w->previous;
    w->texture=NULL;w->previous=w->next=NULL;w->value=NULL;
}
void DGPrivateTextureWatchSet(struct DGPrivateTextureWatch *w,struct DGPrivateTexture *p,GLuint *value)
{
    DG_PRIVATE_GUARD;
    if(!w)return;
    if(w->texture==p&&w->value==value)return;
    DGPrivateTextureWatchForget(w);if(!p||!value)return;
    w->texture=p;w->value=value;*value=p->name;w->next=p->watches;
    if(w->next)w->next->previous=w;p->watches=w;
}
void DGPrivateTextureDestroy(struct DGPrivateTexture *p)
{
    DG_PRIVATE_GUARD;
    if(!p)return;
    struct DGPrivateTextures *r=p->registry;
    struct DGPrivateTexture **link=&r->buckets[DGPrivateTextureBucket(p->name)];
    while(*link&&*link!=p)link=&(*link)->next;
    if(*link)*link=p->next;
    CGLContextObj previous=CGLGetCurrentContext();
    int own_current=previous&&CGLGetShareGroup(previous)==r->group;
    if(!own_current)CGLSetCurrentContext(p->producer);
    glDeleteTextures(1,&p->name);if(!own_current)CGLSetCurrentContext(previous);
    while(p->attachments){struct DGPrivateAttachment *a=p->attachments;p->attachments=a->next;
        CGLReleaseContext(a->context);free(a);}
    while(p->watches){struct DGPrivateTextureWatch *w=p->watches;
        if(w->value)*w->value=0;DGPrivateTextureWatchForget(w);}
    CFRelease(p->storage);CGLReleaseContext(p->producer);free(p);--r->count;DGPrivateTexturesRelease(r);
}
int DGPrivateTextureContains(struct DGPrivateTextures *r,GLuint name)
{DG_PRIVATE_GUARD;return DGPrivateTextureFind(r,name)!=NULL;}
void DGPrivateTexturesForgetFramebuffer(struct DGPrivateTextures *r,GLuint framebuffer)
{
    DG_PRIVATE_GUARD;
    if(!r)return;CGLContextObj c=CGLGetCurrentContext();
    for(unsigned bucket=0;bucket<128;++bucket)for(struct DGPrivateTexture *p=r->buckets[bucket];p;p=p->next){
        struct DGPrivateAttachment **link=&p->attachments;
        while(*link){struct DGPrivateAttachment *a=*link;
            if(a->context==c&&a->framebuffer==framebuffer){*link=a->next;CGLReleaseContext(a->context);free(a);}
            else link=&a->next;
        }
    }
}
int DGPrivateTextureAttach(struct DGPrivateTexture *p,GLenum target,GLenum attachment,GLint level)
{
    DG_PRIVATE_GUARD;
    if(!p||!DGPrivateTexturesCurrent(p->registry)||level||
       (target!=GL_FRAMEBUFFER_EXT&&target!=GL_DRAW_FRAMEBUFFER_EXT&&target!=GL_READ_FRAMEBUFFER_EXT))return 0;
    GLint framebuffer=0;glGetIntegerv(target==GL_READ_FRAMEBUFFER_EXT?GL_READ_FRAMEBUFFER_BINDING_EXT:
        GL_DRAW_FRAMEBUFFER_BINDING_EXT,&framebuffer);
    if(!framebuffer)return 0;
    CGLContextObj c=CGLGetCurrentContext();struct DGPrivateAttachment *a=p->attachments;
    while(a&&(a->context!=c||a->framebuffer!=(GLuint)framebuffer||a->attachment!=attachment))a=a->next;
    int fresh=!a;if(fresh){a=calloc(1,sizeof(*a));if(!a){p->registry->tracking_failed=1;return 0;}}
    glFramebufferTexture2DEXT(target,attachment,GL_TEXTURE_RECTANGLE_ARB,p->name,level);
    GLint actual=0;glGetFramebufferAttachmentParameterivEXT(target,attachment,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&actual);
    if((GLuint)actual!=p->name){if(fresh)free(a);return 0;}
    /* One attachment can name exactly one private texture, including after
     * a drawable replaces its original backing with a retained lease. */
    for(unsigned bucket=0;bucket<128;++bucket)
        for(struct DGPrivateTexture *q=p->registry->buckets[bucket];q;q=q->next){
            if(q==p)continue;struct DGPrivateAttachment **link=&q->attachments;
            while(*link){struct DGPrivateAttachment *old=*link;
                if(old->context==c&&old->framebuffer==(GLuint)framebuffer&&old->attachment==attachment){
                    *link=old->next;CGLReleaseContext(old->context);free(old);
                }else link=&old->next;
            }
        }
    if(fresh){a->context=CGLRetainContext(c);a->framebuffer=(GLuint)framebuffer;a->attachment=attachment;
        a->next=p->attachments;p->attachments=a;}
    return 1;
}
static GLenum DGPrivateTextureReattach(struct DGPrivateAttachment *a,GLuint name)
{
    if(CGLSetCurrentContext(a->context))return GL_INVALID_OPERATION;
    GLint read=0,draw=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,a->framebuffer);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,a->attachment,GL_TEXTURE_RECTANGLE_ARB,name,a->level);
    GLint attached=0;glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,a->attachment,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&attached);
    /* An application may already have a pending error here. Observe the
     * actual attachment instead of consuming/misattributing its error. */
    GLenum error=(GLuint)attached==name?GL_NO_ERROR:GL_INVALID_OPERATION;
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);return error;
}
GLenum DGPrivateTextureClaim(struct DGPrivateTextures *r,GLuint name)
{
    DG_PRIVATE_GUARD;
    struct DGPrivateTexture *p=DGPrivateTextureFind(r,name);if(!p)return GL_NO_ERROR;
    if(r->tracking_failed)return GL_OUT_OF_MEMORY;
    if(!DGPrivateTexturesCurrent(r))return GL_INVALID_OPERATION;
    CGLContextObj previous=CGLGetCurrentContext();
    if(!r->relocator && CGLCreateContext(CGLGetPixelFormat(p->producer),p->producer,&r->relocator))
        return GL_OUT_OF_MEMORY;
    if(CGLSetCurrentContext(p->producer))return GL_INVALID_OPERATION;
    // Existing commands can still refer to the old registration. Complete
    // those commands before importing its real IOSurface under a new name.
    glFinish();
    /* Never issue private texture setters in an application's context. In
     * particular a display-list compilation would record those setters, and
     * an unrelated pending error would falsely reject a successful import. */
    if(CGLSetCurrentContext(r->relocator)){CGLSetCurrentContext(previous);return GL_INVALID_OPERATION;}
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,p->name);
    const GLenum keys[]={GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T};
    GLint values[4];for(unsigned i=0;i<4;++i)glGetTexParameteriv(GL_TEXTURE_RECTANGLE_ARB,keys[i],values+i);
    GLuint fresh=0;glGenTextures(1,&fresh);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,fresh);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    CGLError imported=fresh?CGLTexImageIOSurface2D(r->relocator,GL_TEXTURE_RECTANGLE_ARB,p->internal,
        p->width,p->height,p->format,p->type,p->storage,0):kCGLBadAlloc;
    if(!imported)for(unsigned i=0;i<4;++i)glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,keys[i],values[i]);
    GLenum error=glGetError();if(imported&&!error)error=GL_INVALID_OPERATION;
    if(error && r->failures++<16 && getenv("VIM_PASSTHROUGH_OPENGL_DIAGNOSTICS"))
        fprintf(stderr,"PassthroughOpenGL private import failed name=%u internal=%x size=%d,%d cgl=%d gl=%x\n",
            name,p->internal,p->width,p->height,imported,error);
    if(!error)for(struct DGPrivateAttachment *a=p->attachments;a;a=a->next){
        error=DGPrivateTextureReattach(a,fresh);if(error)break;
    }
    if(error)for(struct DGPrivateAttachment *a=p->attachments;a;a=a->next)
        DGPrivateTextureReattach(a,p->name);
    CGLSetCurrentContext(r->relocator);
    if(!error){
        struct DGPrivateTexture **link=&r->buckets[DGPrivateTextureBucket(p->name)];
        while(*link&&*link!=p)link=&(*link)->next;
        if(*link)*link=p->next;
        GLuint old=p->name;p->name=fresh;
        for(struct DGPrivateTextureWatch *w=p->watches;w;w=w->next)*w->value=fresh;
        unsigned bucket=DGPrivateTextureBucket(fresh);p->next=r->buckets[bucket];r->buckets[bucket]=p;
        glDeleteTextures(1,&old);
    }else if(fresh)glDeleteTextures(1,&fresh);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
    CGLSetCurrentContext(previous);return error;
}
CGLError DGPrivateTextureImport(CGLContextObj c,GLenum target,GLenum internal,GLsizei width,GLsizei height,
    GLenum format,GLenum type,IOSurfaceRef storage,GLuint plane)
{
    DG_PRIVATE_GUARD;
    CGLError error=CGLTexImageIOSurface2D(c,target,internal,width,height,format,type,storage,plane);
    if(error)return error;
    struct DGPrivateTextures *r=DGPrivateTexturesCurrentRegistry();
    /* Dedicated copy contexts have no public objects and need no isolation. */
    if(!r)return kCGLNoError;
    GLint name=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&name);
    if(target!=GL_TEXTURE_RECTANGLE_ARB||plane||!DGPrivateTextureRegister(r,(GLuint)name,storage,
        internal,width,height,format,type))return kCGLBadAlloc;
    return kCGLNoError;
}
void DGPrivateDeleteTextures(GLsizei n,const GLuint *names)
{
    DG_PRIVATE_GUARD;
    struct DGPrivateTextures *r=DGPrivateTexturesCurrentRegistry();
    for(GLsizei i=0;i<n;++i){struct DGPrivateTexture *p=DGPrivateTextureFind(r,names[i]);
        if(p)DGPrivateTextureDestroy(p);else glDeleteTextures(1,names+i);}
}
void DGPrivateDeleteFramebuffers(GLsizei n,const GLuint *names)
{
    DG_PRIVATE_GUARD;
    struct DGPrivateTextures *r=DGPrivateTexturesCurrentRegistry();
    for(GLsizei i=0;i<n;++i)DGPrivateTexturesForgetFramebuffer(r,names[i]);
    glDeleteFramebuffersEXT(n,names);
}
void DGPrivateAttachTexture(GLenum target,GLenum attachment,GLenum textarget,GLuint name,GLint level)
{
    DG_PRIVATE_GUARD;
    struct DGPrivateTexture *p=DGPrivateTextureFind(DGPrivateTexturesCurrentRegistry(),name);
    if(!p||textarget!=GL_TEXTURE_RECTANGLE_ARB||level){
        glFramebufferTexture2DEXT(target,attachment,textarget,name,level);
        /* Empty reusable copy FBOs must stay empty after a later relocation. */
        if(!name&&(target==GL_FRAMEBUFFER_EXT||target==GL_DRAW_FRAMEBUFFER_EXT||target==GL_READ_FRAMEBUFFER_EXT)){
            GLint f=0,actual=-1;glGetIntegerv(target==GL_READ_FRAMEBUFFER_EXT?GL_READ_FRAMEBUFFER_BINDING_EXT:
                GL_DRAW_FRAMEBUFFER_BINDING_EXT,&f);
            if(f)glGetFramebufferAttachmentParameterivEXT(target,attachment,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&actual);
            if(f&&!actual){
                struct DGPrivateTextures *r=DGPrivateTexturesCurrentRegistry();
                if(r)for(unsigned bucket=0;bucket<128;++bucket)
                    for(struct DGPrivateTexture *q=r->buckets[bucket];q;q=q->next){
                        struct DGPrivateAttachment **link=&q->attachments;
                        while(*link){struct DGPrivateAttachment *a=*link;
                            if(a->context==CGLGetCurrentContext()&&a->framebuffer==(GLuint)f&&a->attachment==attachment){
                                *link=a->next;CGLReleaseContext(a->context);free(a);
                            }else link=&a->next;
                        }
                    }
            }
        }
        return;
    }
    /* Does not consume native errors. Metadata allocation failure makes a
     * later relocation fail closed, never reuse an incompletely tracked name. */
    DGPrivateTextureAttach(p,target,attachment,level);
}
void DGPrivateTextureWatchName(struct DGPrivateTextureWatch *watch,GLuint *name)
{DG_PRIVATE_GUARD;DGPrivateTextureWatchSet(watch,DGPrivateTextureFind(DGPrivateTexturesCurrentRegistry(),*name),name);}
