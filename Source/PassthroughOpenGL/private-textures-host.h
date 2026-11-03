/* Renderer-owned GL objects are not application names. Keep stable, typed
 * private handles; a public bind may claim ANY unused name in legacy GL. */
#ifndef VIM_PRIVATE_TEXTURES_HOST_H
#define VIM_PRIVATE_TEXTURES_HOST_H
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOSurface/IOSurface.h>
struct DGPrivateTextures;
struct DGPrivateTexture;
struct DGPrivateTextureWatch {
    struct DGPrivateTexture *texture;
    struct DGPrivateTextureWatch *previous,*next;
    GLuint *value;
};
struct DGPrivateTextures *DGPrivateTexturesCreate(void);
struct DGPrivateTextures *DGPrivateTexturesCurrentRegistry(void);
struct DGPrivateTexture *DGPrivateTextureLookup(struct DGPrivateTextures *,GLuint);
void DGPrivateTexturesRetain(struct DGPrivateTextures *);
void DGPrivateTexturesRelease(struct DGPrivateTextures *);
struct DGPrivateTexture *DGPrivateTextureRegister(struct DGPrivateTextures *,GLuint,
    IOSurfaceRef,GLenum,GLsizei,GLsizei,GLenum,GLenum);
GLuint DGPrivateTextureName(const struct DGPrivateTexture *);
void DGPrivateTextureDestroy(struct DGPrivateTexture *);
int DGPrivateTextureAttach(struct DGPrivateTexture *,GLenum,GLenum,GLint);
void DGPrivateTexturesForgetFramebuffer(struct DGPrivateTextures *,GLuint);
int DGPrivateTextureContains(struct DGPrivateTextures *,GLuint);
GLenum DGPrivateTextureClaim(struct DGPrivateTextures *,GLuint);
void DGPrivateTextureWatchSet(struct DGPrivateTextureWatch *,struct DGPrivateTexture *,GLuint *);
void DGPrivateTextureWatchForget(struct DGPrivateTextureWatch *);
/* Internal executor calls only, never public GL dispatch. */
CGLError DGPrivateTextureImport(CGLContextObj,GLenum,GLenum,GLsizei,GLsizei,GLenum,GLenum,IOSurfaceRef,GLuint);
void DGPrivateDeleteTextures(GLsizei,const GLuint *);
void DGPrivateDeleteFramebuffers(GLsizei,const GLuint *);
void DGPrivateAttachTexture(GLenum,GLenum,GLenum,GLuint,GLint);
void DGPrivateTextureWatchName(struct DGPrivateTextureWatch *,GLuint *);
#endif
