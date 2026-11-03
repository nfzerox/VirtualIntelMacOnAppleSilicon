/* Finite own-connection diagnostic. Uses the production codec/device path;
 * never reads another client's stream or changes a global renderer policy. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
int main(int argc,char **argv)
{
    if(argc>2||(argc==2&&strcmp(argv[1],"--method")))return 64;
    if(argc==2)dg_exchange_trap_override=0;
    setbuf(stdout,NULL);
    struct DGContext context={0};
    struct DGConnection *connection=calloc(1,sizeof(*connection));
    if(!connection)return 1;
    context.connection=connection;connection->refs=1;
    pthread_mutex_init(&connection->lock,NULL);
    pthread_mutex_init(&connection->map_lock,NULL);
    pthread_mutex_init(&connection->backing_lock,NULL);
    int status=DGConnect(connection);
    printf("connect=%d device=%u exchange=%d trap=%d pointer-bits=%u\n",
        status,connection->device,connection->exchange_supported,
        connection->exchange_trap_supported,(unsigned)(sizeof(void*)*8));
    if(status){DGConnectionRelease(connection);return 2;}
    struct DGPacket hello=DGPacketNew(&context,DG_HELLO);
    hello.message.args[0]=DG_MAGIC;hello.message.args[1]=DG_VERSION;
    uuid_t identity;int namespaced=VIMGetResourceNamespace(identity);
    if(namespaced)memcpy(&hello.message.args[3],identity,sizeof(identity));
    DGPacketPointer(&hello,2,DG_LAYOUT,sizeof(DG_LAYOUT),0);
    struct DGReply reply=DGSubmit(&context,&hello,1);
    printf("hello namespace=%d valid=%d error=%x reply-error=%x capabilities=%llx dead=%d\n",
        namespaced,hello.valid_reply,context.error,reply.error,
        (unsigned long long)reply.result,connection->dead);
    connection->capabilities=reply.result;DGPacketFree(&hello);
    if(context.error){DGConnectionRelease(connection);return 3;}
    struct DGPacket create=DGPacketNew(&context,DG_CREATE);
    reply=DGSubmit(&context,&create,1);
    printf("create valid=%d error=%x reply-error=%x context=%llu dead=%d\n",
        create.valid_reply,context.error,reply.error,
        (unsigned long long)reply.result,connection->dead);
    DGPacketFree(&create);
    int ok=!context.error&&reply.result;
    /* Closing our owned transport releases its contexts on the executor. */
    DGConnectionRelease(connection);return ok?0:4;
}
