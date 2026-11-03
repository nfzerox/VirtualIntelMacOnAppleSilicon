/* Native Darwin8 adapters for our owned2D user-client table. No fallback
 * retries: a kernel method may have side effects before returning an error. */
#ifndef VIM_TIGER_IOKIT_COMPAT_H
#define VIM_TIGER_IOKIT_COMPAT_H
#if !defined(__i386__) || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ != 1040
#error Tiger's native MIG adapter requires its measured i386 ABI
#endif
#include <IOKit/IOKitLib.h>
#include <stdint.h>
#include "tiger-iokit-methods-generated.inc"
/* Exact10.4 iokitmig_c.h prototypes; scalar slots are32 bits, not the
 * later64-bit IOConnectCallMethod slots. Structures retain their own ABI. */
#ifdef __cplusplus
extern "C" {
#endif
extern kern_return_t io_connect_method_scalarI_scalarO(mach_port_t,int,uint32_t *,
    mach_msg_type_number_t,uint32_t *,mach_msg_type_number_t *);
extern kern_return_t io_connect_method_scalarI_structureO(mach_port_t,int,uint32_t *,
    mach_msg_type_number_t,char *,mach_msg_type_number_t *);
extern kern_return_t io_connect_method_scalarI_structureI(mach_port_t,int,uint32_t *,
    mach_msg_type_number_t,char *,mach_msg_type_number_t);
extern kern_return_t io_connect_method_structureI_structureO(mach_port_t,int,char *,
    mach_msg_type_number_t,char *,mach_msg_type_number_t *);
/* Exact IOKitUser297 native wrapper and measured Rosetta shim export. */
extern kern_return_t io_connect_unmap_memory(io_connect_t,uint32_t,task_t,vm_address_t);
#ifdef __cplusplus
}
#endif
static inline kern_return_t VIMTigerCallMethod(io_connect_t connection,uint32_t selector,
    const uint64_t *scalar_input,uint32_t input_count,const void *input,size_t input_size,
    uint64_t *scalar_output,uint32_t *output_count,void *output,size_t *output_size)
{
    uint32_t in[16]={0},out[16]={0};
    mach_msg_type_number_t out_count=output_count?*output_count:0;
    mach_msg_type_number_t out_bytes=output_size?(mach_msg_type_number_t)*output_size:0;
    if(input_count>16 || out_count>16 || (input_count&&!scalar_input) ||
       (out_count&&!scalar_output) || input_size>4096 || (input_size&&!input) ||
       (output_size&&*output_size>4096) || (out_bytes&&!output))return kIOReturnBadArgument;
    for(unsigned i=0;i<input_count;++i){
        if(scalar_input[i]>UINT32_MAX)return kIOReturnBadArgument;
        in[i]=(uint32_t)scalar_input[i];
    }
    kern_return_t error;
    switch(VIMTigerMethodKind(selector)){
    case 0:
        if(input_size||out_bytes)return kIOReturnBadArgument;
        error=io_connect_method_scalarI_scalarO(connection,(int)selector,in,input_count,out,&out_count);
        if(!error){
            if(out_count>16 || (out_count&&!scalar_output) || (output_count&&out_count>*output_count))return kIOReturnOverrun;
            for(unsigned i=0;i<out_count;++i)scalar_output[i]=out[i];
            if(output_count)*output_count=out_count;
        }
        return error;
    case 2:
        if(input_size||out_count)return kIOReturnBadArgument;
        error=io_connect_method_scalarI_structureO(connection,(int)selector,in,input_count,(char *)output,&out_bytes);
        if(output_size)*output_size=out_bytes;return error;
    case 3:
        if(input_count||out_count)return kIOReturnBadArgument;
        error=io_connect_method_structureI_structureO(connection,(int)selector,(char *)input,
            (mach_msg_type_number_t)input_size,(char *)output,&out_bytes);
        if(output_size)*output_size=out_bytes;return error;
    case 4:
        if(out_count||out_bytes)return kIOReturnBadArgument;
        return io_connect_method_scalarI_structureI(connection,(int)selector,in,input_count,
            (char *)input,(mach_msg_type_number_t)input_size);
    default:return kIOReturnUnsupported;
    }
}
static inline kern_return_t VIMTigerCallStruct(io_connect_t connection,uint32_t selector,
    const void *input,size_t input_size,void *output,size_t *output_size)
{return VIMTigerCallMethod(connection,selector,NULL,0,input,input_size,NULL,NULL,output,output_size);}
static inline kern_return_t VIMTigerCallScalar(io_connect_t connection,uint32_t selector,
    const uint64_t *input,uint32_t input_count,uint64_t *output,uint32_t *output_count)
{return VIMTigerCallMethod(connection,selector,input,input_count,NULL,0,output,output_count,NULL,NULL);}
#define IOConnectCallMethod VIMTigerCallMethod
#define IOConnectCallStructMethod VIMTigerCallStruct
#define IOConnectCallScalarMethod VIMTigerCallScalar
static inline kern_return_t VIMTigerMapMemory(io_connect_t connection,uint32_t type,
    task_t task,mach_vm_address_t *address,mach_vm_size_t *size,uint32_t options)
{
    if(!address || !size || *address>UINT32_MAX || *size>UINT32_MAX)return kIOReturnBadArgument;
    vm_address_t start=(vm_address_t)*address;vm_size_t length=(vm_size_t)*size;
    kern_return_t error=IOConnectMapMemory(connection,type,task,&start,&length,options);
    if(!error){*address=start;*size=length;}return error;
}
static inline kern_return_t VIMTigerUnmapMemory(io_connect_t connection,uint32_t type,
    task_t task,mach_vm_address_t address)
{return address>UINT32_MAX?kIOReturnBadArgument:io_connect_unmap_memory(connection,type,task,(vm_address_t)address);}
#endif
