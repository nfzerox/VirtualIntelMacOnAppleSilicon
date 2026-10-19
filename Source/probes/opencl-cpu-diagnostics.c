// Isolate CPU compute from any GPU, image decoder or application cache.
#include <OpenCL/opencl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define CHECK(call) do {cl_int r=(call);printf("%s = %d\n",#call,r);fflush(stdout);if(r)return 1;}while(0)
int main(void) {
    alarm(30);
    cl_platform_id platform;cl_device_id device;cl_uint count=0;cl_int error=0;
    CHECK(clGetPlatformIDs(1,&platform,&count));
    CHECK(clGetDeviceIDs(platform,CL_DEVICE_TYPE_CPU,1,&device,&count));
    char name[256]={0};CHECK(clGetDeviceInfo(device,CL_DEVICE_NAME,sizeof(name),name,NULL));
    printf("CPU: %s\n",name);
    cl_context context=clCreateContext(NULL,1,&device,NULL,NULL,&error);CHECK(error);
    cl_command_queue queue=clCreateCommandQueue(context,device,0,&error);CHECK(error);
    const char *source="__kernel void probe(__global int *p){p[get_global_id(0)]=42;}";
    cl_program program=clCreateProgramWithSource(context,1,&source,NULL,&error);CHECK(error);
    error=clBuildProgram(program,1,&device,NULL,NULL,NULL);
    if(error){char log[8192]={0};clGetProgramBuildInfo(program,device,CL_PROGRAM_BUILD_LOG,sizeof(log),log,NULL);puts(log);}
    CHECK(error);
    cl_kernel kernel=clCreateKernel(program,"probe",&error);CHECK(error);
    int pixels[32];memset(pixels,0xa5,sizeof(pixels));
    cl_mem buffer=clCreateBuffer(context,CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR,sizeof(pixels),pixels,&error);CHECK(error);
    CHECK(clSetKernelArg(kernel,0,sizeof(buffer),&buffer));
    size_t size=32;CHECK(clEnqueueNDRangeKernel(queue,kernel,1,NULL,&size,NULL,0,NULL,NULL));
    CHECK(clEnqueueReadBuffer(queue,buffer,CL_TRUE,0,sizeof(pixels),pixels,0,NULL,NULL));
    unsigned wrong=0;for(unsigned i=0;i<32;i++)wrong+=pixels[i]!=42;
    printf("first=%d wrong=%u/32\n",pixels[0],wrong);
    clReleaseMemObject(buffer);clReleaseKernel(kernel);clReleaseProgram(program);
    clReleaseCommandQueue(queue);clReleaseContext(context);
    return wrong?2:0;
}
