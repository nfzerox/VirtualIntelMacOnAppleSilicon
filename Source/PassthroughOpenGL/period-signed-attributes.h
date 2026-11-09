/* Period GMA950 current/immediate attributes, measured using public setters
 * and shader pixels. On 10.4.11 colors and normalized generic setters use
 * (2*c+1)/(2^bits-1); normals use c/(2^(bits-1)-1), without clamping.
 * Its actual array draws use the newer symmetric rule, so do not apply this
 * conversion to pointer declarations, array packets or ArrayElement values.
 * On 10.5.8 byte/short colors and normalized generic setters use the biased
 * rule; int setters retain the native conversion. Leopard immediate normals
 * are NOT adapted: its scalar, vector, list and Rosetta paths differ. Reusing
 * the color policy for them regresses scalar and compiled normal setters.
 * Array rendering is a separate contract on both systems.
 * Payloads newer than Leopard retain their existing command behavior.
 */
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040 || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
static GLfloat DGPeriodSignedAttribute(GLdouble value,unsigned bits,int normal)
{
    GLdouble maximum=bits==8?127.0:bits==16?32767.0:2147483647.0;
    return (GLfloat)(normal?value/maximum:(value*2.0+1.0)/(maximum*2.0+1.0));
}
#endif

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
/* A draw-time copy, not a change to the caller's pointer/type/stride state.
 * Only Leopard's measured signed byte/short normalized CPU attributes need
 * this policy. VBO ownership and immediate normal commands are separate.
 */
static int DGPeriodSignedCPUArray(const struct DGVertexArray *a)
{
    return !a->buffer && (a->type==GL_BYTE || a->type==GL_SHORT) &&
        (a->slot==1 || a->slot==2 || a->slot==3 ||
         (a->slot>=16 && a->slot<32 && a->normalized==1));
}
static void DGPeriodSignedArrayCopy(void *destination,const void *source,
    const struct DGVertexArray *a,uint64_t count,int big_endian)
{
    unsigned bytes=a->type==GL_BYTE?1:2;
    size_t stride=a->stride?a->stride:a->size*bytes;
    unsigned char *output=destination;
    for(uint64_t vertex=0;vertex<count;++vertex)
        for(unsigned component=0;component<a->size;++component){
            const unsigned char *p=(const unsigned char *)source+(size_t)vertex*stride+component*bytes;
            double value;
            if(bytes==1){int8_t v;memcpy(&v,p,1);value=v;}
            else {uint16_t v;memcpy(&v,p,2);if(big_endian)v=__builtin_bswap16(v);value=(int16_t)v;}
            GLfloat converted=DGPeriodSignedAttribute(value,bytes*8,0);
            uint32_t bits;memcpy(&bits,&converted,4);if(big_endian)bits=__builtin_bswap32(bits);
            memcpy(output,&bits,4);output+=4;
        }
}
#endif
